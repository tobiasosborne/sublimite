#!/usr/bin/env python3
"""Contract runner: invoke each due case script once, reconcile its records with the manifest.

The manifest (tests/contract/expectations.json, format in docs/decisions/v2-he7.16.md) names
every case and assertion with its evidence kind, first required wave and per-target
expectation, and binds each target to an exact identity (pin, patch digest, executable
digests). The runner owns no case logic: it hashes the target, picks the reviewed target whose
identity matches, hands each invocation a free private display and the selected target's
executable digests (records.DIGESTS_ENV), re-hashes those executables, runs it exactly once
under its watchdog, parses the RECORD lines it printed (tests/contract/records.py) and
compares by the (case_id, assertion_id) pair. Green means every required assertion is PASS
or XFAIL with its reviewed signature; a required ID without a record is NOT-RUN only after a
FAIL or ERROR earlier in the same case, otherwise MISSING.

First failure is final: there is no rerun of any kind. Exit status (records.EXIT): PASS 0
(every required assertion PASS, or XFAIL with its reviewed signature), FAIL 3, ERROR 4,
XPASS 5; ERROR dominates FAIL dominates XPASS. ERROR covers: an invalid manifest (including
a duplicate key or an XFAIL without signature and witness), an unreviewed identity or wrong
role, an executable changed since selection, zero records, a duplicate record, a record for
an assertion not required of the target, a missing required ID, a timeout, no free display,
a missing script, an abnormal or inconsistent exit, a malformed record (a repeated JSON key
included) and a wrong XFAIL signature. A gate record (gate.json and summary.txt) is written
under --out for every run.
"""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import records  # noqa: E402
from sandbox import CONTRACT_DISPLAY  # noqa: E402
from test_xdriver import available  # noqa: E402

ROOT = HERE.parents[1]
MANIFEST = HERE / "expectations.json"
ROLES = ("harness", "v1", "v2")
ERRORS = ("ERROR", "MISSING", "WRONG-SIGNATURE")
ACCEPTED = ("PASS", "XFAIL")  # the only outcomes a green run is made of
STOPPING = ("FAIL", "ERROR", "WRONG-SIGNATURE")  # observed failures after which a case stops
PLACEHOLDER = re.compile(r"\{([a-z_]+)(?::([A-Za-z0-9_.-]+))?\}")
# The child must never reach the desktop session; cases set what they need explicitly.
DROPPED_ENV = ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY")
RESERVED_ENV = DROPPED_ENV + (records.DIGESTS_ENV,)  # no manifest env may set these


class Invalid(Exception):
    """The manifest or the run's inputs cannot produce a trustworthy record."""


def utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def power():
    try:
        battery = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    except OSError:
        battery = "BAT0 unavailable"
    return {"utc": utc(), "bat0": battery, "loadavg": os.getloadavg()}


# ---- manifest ----

def expectation_problems(where, expect):
    if expect == "PASS":
        return []
    if not isinstance(expect, dict) or expect.get("status") != "XFAIL":
        return [f"{where}: expectation must be \"PASS\" or an XFAIL object, got {expect!r}"]
    found = []
    if set(expect) != {"status", "signature", "witness"}:
        found.append(f"{where}: an XFAIL names exactly status, signature and witness")
    for key in ("signature", "witness"):
        if not isinstance(expect.get(key), str) or not expect.get(key):
            found.append(f"{where}: XFAIL without its exact {key}")
    return found


def assertions_of(manifest):
    """Yield (id, case, assertion, spec) with case-level keys merged per assertion; the
    (case, assertion) pair is the key, the joined id only its unique display name."""
    for case_id, case in manifest["cases"].items():
        for assertion_id, override in case["assertions"].items():
            expect = dict(case.get("expect", {}))
            expect.update(override.get("expect", {}))
            yield f"{case_id}.{assertion_id}", case_id, assertion_id, {
                "invocation": case["invocation"],
                "evidence": override.get("evidence", case.get("evidence")),
                "first_required_wave": override.get("first_required_wave",
                                                    case.get("first_required_wave")),
                "expect": expect,
            }


def validate(manifest):
    """Every reason the manifest cannot be used (empty when it can)."""
    if not isinstance(manifest, dict) or manifest.get("format") != 1:
        return ["manifest format must be 1"]
    found = []
    targets = manifest.get("targets")
    invocations = manifest.get("invocations")
    cases = manifest.get("cases")
    if not all(isinstance(part, dict) and part for part in (targets, invocations, cases)):
        return ["manifest needs non-empty targets, invocations and cases objects"]
    identities = []
    for name, target in targets.items():
        if not isinstance(target, dict) or target.get("role") not in ROLES:
            found.append(f"target {name}: role must be one of {ROLES}")
            continue
        identity = target.get("identity")
        if identity is not None and not isinstance(identity, dict):
            found.append(f"target {name}: identity must be an object or null (unbound)")
        elif identity is not None:
            if identity in identities:
                found.append(f"target {name}: identity is bound to two targets")
            identities.append(identity)
    for name, invocation in invocations.items():
        argv = invocation.get("argv") if isinstance(invocation, dict) else None
        if (not isinstance(argv, list) or not argv
                or not all(isinstance(arg, str) for arg in argv) or argv[0].startswith("/")):
            found.append(f"invocation {name}: argv must start with a repo-relative script")
            continue
        if invocation.get("displays") not in (0, 1):
            found.append(f"invocation {name}: displays must be 0 or 1")
        timeout = invocation.get("timeout_s")
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or timeout <= 0:
            found.append(f"invocation {name}: timeout_s must be positive")
        if not isinstance(invocation.get("timeout_derivation"), str) or not invocation["timeout_derivation"]:
            found.append(f"invocation {name}: timeout_s needs its derivation (Law 4)")
        env = invocation.get("env", {})
        if not isinstance(env, dict) or not all(isinstance(v, str) for v in env.values()):
            found.append(f"invocation {name}: env must map names to strings")
        elif set(env) & set(RESERVED_ENV):
            found.append(f"invocation {name}: env may not set {sorted(set(env) & set(RESERVED_ENV))}")
    for case_id, case in cases.items():
        if not isinstance(case, dict) or case.get("invocation") not in invocations:
            found.append(f"case {case_id}: unknown invocation")
            return found
        if not isinstance(case.get("assertions"), dict) or not case["assertions"]:
            found.append(f"case {case_id}: needs at least one assertion")
            return found
        if not all(isinstance(value, dict) for value in case["assertions"].values()):
            found.append(f"case {case_id}: each assertion maps to an object")
            return found
    seen = set()
    for full, case_id, assertion_id, spec in assertions_of(manifest):
        if not case_id or not assertion_id or full in seen:
            found.append(f"assertion {full}: empty or duplicate ID")
        seen.add(full)
        if spec["evidence"] not in records.EVIDENCE:
            found.append(f"assertion {full}: evidence must be one of {records.EVIDENCE}")
        wave = spec["first_required_wave"]
        if not isinstance(wave, int) or isinstance(wave, bool) or wave < 0:
            found.append(f"assertion {full}: first_required_wave must be a non-negative integer")
        if not spec["expect"]:
            found.append(f"assertion {full}: no target expectation")
        for target, expect in spec["expect"].items():
            if target not in targets:
                found.append(f"assertion {full}: expectation for unknown target {target}")
            found += expectation_problems(f"assertion {full} on {target}", expect)
    return found


def load(path):
    try:
        manifest = json.loads(Path(path).read_text(encoding="utf-8"),
                              object_pairs_hook=records.unique_object)
    except (OSError, ValueError) as exc:
        raise Invalid(f"manifest {path}: {exc}") from None
    found = validate(manifest)
    if found:
        raise Invalid("; ".join(found))
    return manifest


# ---- target identity ----

def identity_of(exes, oracle_build):
    identity = {}
    if exes:
        identity["executables"] = {name: sha256(path) for name, path in sorted(exes.items())}
    if oracle_build is not None:
        stamp = json.loads((oracle_build / "manifest.json").read_text(encoding="utf-8"),
                           object_pairs_hook=records.unique_object)
        built = {name: sha256(oracle_build / name) for name in sorted(stamp["executables"])}
        if built != stamp["executables"]:
            raise Invalid(f"{oracle_build}: executables differ from their build stamp")
        identity.update(pin=stamp["pin"], patch_sha256=stamp["patch_sha256"], oracle_build=built)
    return identity


def select_target(manifest, identity, role):
    matches = [name for name, target in manifest["targets"].items()
               if target.get("identity") == identity]
    if not matches:
        raise Invalid(f"no reviewed target has identity {json.dumps(identity, sort_keys=True)}")
    name = matches[0]
    bound = manifest["targets"][name]["role"]
    if bound != role:
        raise Invalid(f"identity is bound to target {name} with role {bound}; this run requested {role}")
    return name


def digests_of(identity, exes, oracle_build):
    """{absolute path: sha256} of the selected target's identity, the one binding every
    invocation gets (records.DIGESTS_ENV) and the runner re-checks before each launch."""
    digests = {str(exes[name]): digest for name, digest in identity.get("executables", {}).items()}
    for name, digest in identity.get("oracle_build", {}).items():
        path = str(oracle_build / name)
        if digests.setdefault(path, digest) != digest:
            raise Invalid(f"{path} is bound to two digests")
    return digests


def changed(digests):
    """Paths whose bytes are no longer the selected ones (unreadable counts as changed)."""
    found = []
    for path, digest in digests.items():
        try:
            if sha256(path) != digest:
                found.append(path)
        except OSError:
            found.append(path)
    return found


# ---- displays ----

def display_pool(spec):
    if spec == "none":
        return []
    first, _, last = spec.partition("-")
    bounds = [CONTRACT_DISPLAY.fullmatch(part) for part in (first, last or first)]
    if not all(bounds) or int(bounds[0][1]) > int(bounds[1][1]):
        raise argparse.ArgumentTypeError(f"displays must be none, :N or :N-:M within :120-:199: {spec!r}")
    return list(range(int(bounds[0][1]), int(bounds[1][1]) + 1))


# ---- one invocation ----

def expand(text, values):
    def replace(match):
        key = match[1] if match[2] is None else f"{match[1]}:{match[2]}"
        if key not in values:
            raise Invalid(f"no value for placeholder {{{key}}}")
        return values[key]
    return PLACEHOLDER.sub(replace, text)


def execute(name, invocation, values, digests, pool, used, artifacts):
    """Run once; return the invocation's receipt (records parsed, problems found)."""
    receipt = {"name": name, "argv": invocation["argv"], "exit": None,
               "seconds": None, "display": None, "artifacts": str(artifacts),
               "stdout": str(artifacts / "stdout.log"), "stderr": str(artifacts / "stderr.log"),
               "records": [], "problems": []}
    script = ROOT / invocation["argv"][0]
    if not script.is_file():
        receipt["problems"].append(f"case script missing: {invocation['argv'][0]}")
        return receipt
    values = dict(values, artifacts=str(artifacts))
    if invocation["displays"]:
        number = next((n for n in pool if n not in used and available(n)), None)
        if number is None:
            receipt["problems"].append("no free display in the runner's pool")
            return receipt
        used.add(number)
        values["display"] = receipt["display"] = f":{number}"
    try:
        argv = [expand(arg, values) for arg in invocation["argv"][1:]]
        extra = {key: expand(value, values) for key, value in invocation.get("env", {}).items()}
    except Invalid as exc:
        receipt["problems"].append(str(exc))
        return receipt
    env = {key: value for key, value in os.environ.items() if key not in RESERVED_ENV}
    env.update(extra, **{records.DIGESTS_ENV: json.dumps(digests, sort_keys=True)})
    # Narrows, never closes, the window to the launch: the case checks the fd it executes.
    replaced = changed(digests)
    if replaced:
        receipt["problems"].append(f"executable changed since target selection: {replaced}")
        return receipt
    command = [sys.executable, "-B", str(script), *argv]
    receipt["command"] = command  # an attempted launch: the receipt's count derives from it
    started = time.monotonic()
    timed_out = False
    with open(receipt["stdout"], "wb") as out, open(receipt["stderr"], "wb") as err:
        child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                 stdout=out, stderr=err, start_new_session=True)
        try:
            receipt["exit"] = child.wait(timeout=invocation["timeout_s"])
        except subprocess.TimeoutExpired:
            # The case's own hang detectors should have fired first; take its whole group.
            os.killpg(child.pid, signal.SIGKILL)
            receipt["exit"] = child.wait()
            receipt["problems"].append(f"timeout: no exit within {invocation['timeout_s']} s")
            timed_out = True
    receipt["seconds"] = round(time.monotonic() - started, 3)
    for number, line in enumerate(Path(receipt["stdout"]).read_text(errors="replace").splitlines(), 1):
        if not line.startswith(records.PREFIX):
            continue
        try:
            record = records.parse(line)
        except ValueError as exc:
            receipt["problems"].append(f"stdout line {number}: malformed record ({exc})")
            continue
        found = records.problems(record)
        if found:
            receipt["problems"].append(f"stdout line {number}: malformed record ({'; '.join(found)})")
            continue
        receipt["records"].append(record)
    if not timed_out:
        statuses = [record["status"] for record in receipt["records"]]
        if not statuses:
            receipt["problems"].append("zero records collected")
        elif receipt["exit"] not in (0, 3, 4):
            receipt["problems"].append(f"abnormal exit {receipt['exit']}")
        elif receipt["exit"] != records.exit_status(statuses):
            receipt["problems"].append(f"exit {receipt['exit']} disagrees with its records "
                                       f"(expected {records.exit_status(statuses)})")
    return receipt


def verdict(expect, record):
    if record["status"] == "ERROR":
        return "ERROR"
    if expect == "PASS":
        return record["status"]
    if record["status"] == "PASS":
        return "XPASS"
    return "XFAIL" if record.get("signature") == expect["signature"] else "WRONG-SIGNATURE"


def reconcile(receipt, owned, target):
    """Outcome per required assertion of one invocation; problems go into the receipt.

    `owned` maps (case_id, assertion_id) to its spec in manifest order, which is each
    fail-fast case's execution order. Observed records are judged first. A required ID with
    no record is NOT-RUN only when a STOPPING record of its own case precedes it (an
    unlisted harness ERROR stops its case at the point it was printed); otherwise MISSING."""
    order = {key: index for index, key in enumerate(owned)}
    seen, observed, stops, reached = set(), {}, [], {}
    for record in receipt["records"]:
        key = case_id, assertion_id = record["case_id"], record["assertion_id"]
        if key in seen:
            receipt["problems"].append(f"duplicate record {case_id} / {assertion_id}")
            continue
        seen.add(key)
        spec = owned.get(key)
        if spec is None or target not in spec["expect"]:
            if record["status"] == "ERROR":
                receipt["problems"].append(f"harness error at {case_id} / {assertion_id}: "
                                           f"{record['predicate']}")
                stops.append((case_id, reached.get(case_id, -1)))
            else:
                receipt["problems"].append(f"record for an ID not required of target {target}: "
                                           f"{case_id} / {assertion_id}")
            continue
        if (record["evidence"], record["first_required_wave"]) != (spec["evidence"],
                                                                    spec["first_required_wave"]):
            receipt["problems"].append(
                f"{case_id} / {assertion_id}: record keys (evidence {record['evidence']}, wave "
                f"{record['first_required_wave']}) differ from the manifest "
                f"({spec['evidence']}, {spec['first_required_wave']})")
        outcome = verdict(spec["expect"][target], record)
        observed[key] = {"outcome": outcome, "observed": record["status"],
                         "predicate": record["predicate"], "signature": record.get("signature"),
                         "artifacts": record.get("artifacts")}
        reached[case_id] = max(reached.get(case_id, -1), order[key])
        if outcome in STOPPING:
            stops.append((case_id, order[key]))
    outcomes = {}
    for key, spec in owned.items():
        if target not in spec["expect"]:
            continue
        entry = {"case_id": key[0], "assertion_id": key[1], "invocation": receipt["name"],
                 "evidence": spec["evidence"], "first_required_wave": spec["first_required_wave"],
                 "expected": spec["expect"][target]}
        if key in observed:
            entry.update(observed[key])
        else:
            ran_out = any(case == key[0] and at < order[key] for case, at in stops)
            entry["outcome"] = "NOT-RUN" if ran_out else "MISSING"
        outcomes[f"{key[0]}.{key[1]}"] = entry
    return outcomes


def aggregate(problems, receipts, outcomes, required):
    kinds = {entry["outcome"] for entry in outcomes.values()}
    if (problems or any(r["problems"] for r in receipts) or kinds & set(ERRORS) or not outcomes):
        return "ERROR"
    for status in ("FAIL", "XPASS"):
        if status in kinds:
            return status
    # The green invariant: every required ID has an outcome, and each is PASS or matched XFAIL.
    if set(outcomes) != set(required) or not kinds <= set(ACCEPTED):
        return "ERROR"
    return "PASS"


# ---- gate record ----

def summary(gate):
    lines = [f"contract gate {gate['run_id']}: {gate['status']} (exit {gate['exit']})",
             f"target {gate.get('target')} role {gate['role']}; power {gate['power_start']['bat0']}, "
             f"load {gate['power_start']['loadavg'][0]:.2f}"]
    lines += [f"  problem: {problem}" for problem in gate["problems"]]
    for receipt in gate["invocations"].values():
        lines.append(f"  {receipt['name']}: exit {receipt['exit']}, {len(receipt['records'])} records, "
                     f"{receipt['seconds']} s, display {receipt['display']}, invoked {receipt['count']}x")
        lines += [f"    problem: {problem}" for problem in receipt["problems"]]
    counts = {}
    for full, entry in gate["assertions"].items():
        counts[entry["outcome"]] = counts.get(entry["outcome"], 0) + 1
        if entry["outcome"] != "PASS":
            lines.append(f"  {full}: {entry['outcome']} (expected "
                         f"{entry['expected'] if entry['expected'] == 'PASS' else 'XFAIL'})"
                         + (f": {entry['predicate'][:160]}" if entry.get("predicate") else ""))
    lines.append("  outcomes: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return "\n".join(lines) + "\n"


def run(args):
    run_id = f"{args.role}-{datetime.datetime.now(datetime.timezone.utc):%Y%m%dT%H%M%SZ}-{os.getpid()}"
    run_dir = args.out.resolve() / run_id
    run_dir.mkdir(parents=True, mode=0o700)
    gate = {"run_id": run_id, "role": args.role, "argv": sys.argv, "power_start": power(),
            "manifest": {"path": str(args.manifest), "sha256": None},
            "runner_sha256": {p.name: sha256(p) for p in (Path(__file__), HERE / "records.py")},
            "target": None, "identity": None, "digests": None, "problems": [], "invocations": {},
            "assertions": {}}
    required = []
    try:
        gate["manifest"]["sha256"] = sha256(args.manifest)
        manifest = load(args.manifest)
        gate["identity"] = identity_of(dict(args.exe), args.oracle_build)
        gate["target"] = target = select_target(manifest, gate["identity"], args.role)
        gate["digests"] = digests = digests_of(manifest["targets"][target]["identity"],
                                               dict(args.exe), args.oracle_build)
    except (Invalid, OSError, KeyError, ValueError) as exc:
        gate["problems"].append(f"{type(exc).__name__}: {exc}")
    else:
        everything = list(assertions_of(manifest))
        required = [full for full, _, _, spec in everything if target in spec["expect"]]
        values = {f"exe:{name}": str(path) for name, path in args.exe}
        if args.oracle_build is not None:
            values["oracle_build"] = str(args.oracle_build)
        used = set()
        for name, invocation in manifest["invocations"].items():
            owned = {(case_id, assertion_id): spec for _, case_id, assertion_id, spec in everything
                     if spec["invocation"] == name}
            if not any(target in spec["expect"] for spec in owned.values()):
                continue
            artifacts = run_dir / name
            artifacts.mkdir()
            receipt = execute(name, invocation, values, digests, args.displays, used, artifacts)
            receipt["count"] = int("command" in receipt)
            gate["assertions"].update(reconcile(receipt, owned, target))
            gate["invocations"][name] = receipt
            print(f"{name}: exit {receipt['exit']}, {len(receipt['records'])} records, "
                  f"{len(receipt['problems'])} problems, {receipt['seconds']} s", flush=True)
        if not gate["assertions"]:
            gate["problems"].append(f"target {target} has no required assertions")
    gate["status"] = aggregate(gate["problems"], gate["invocations"].values(), gate["assertions"],
                               required)
    gate["exit"] = records.EXIT[gate["status"]]
    gate["power_end"] = power()
    (run_dir / "gate.json").write_text(json.dumps(gate, indent=1, default=str) + "\n")
    text = summary(gate)
    (run_dir / "summary.txt").write_text(text)
    print(text + f"gate record: {run_dir / 'gate.json'}", end="\n", flush=True)
    return gate["exit"]


def exe_pair(text):
    name, sep, path = text.partition("=")
    if not sep or not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or not Path(path).is_absolute():
        raise argparse.ArgumentTypeError("--exe takes NAME=/absolute/path")
    return name, Path(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--validate", action="store_true", help="check the manifest and exit")
    parser.add_argument("--role", choices=ROLES)
    parser.add_argument("--exe", type=exe_pair, action="append", default=[],
                        help="NAME=/abs/path: a target executable, hashed into the identity")
    parser.add_argument("--oracle-build", type=Path,
                        help="absolute inspected v1 build/contract-oracle directory")
    parser.add_argument("--displays", type=display_pool, default=[],
                        help="private display pool: none, :N or :N-:M within :120-:199")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "contract-runs")
    args = parser.parse_args()
    if args.validate:
        try:
            load(args.manifest)
        except Invalid as exc:
            print(f"run.py: invalid manifest: {exc}", file=sys.stderr)
            return records.EXIT["ERROR"]
        print(f"run.py: {args.manifest} is valid")
        return records.EXIT["PASS"]
    if args.role is None:
        parser.error("--role is required")
    if args.oracle_build is not None and not args.oracle_build.is_absolute():
        parser.error("--oracle-build must be absolute")
    if len({name for name, _ in args.exe}) != len(args.exe):
        parser.error("--exe names must be unique")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
