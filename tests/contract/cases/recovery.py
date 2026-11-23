"""R31 process-death recovery controls against the pinned v1 oracle.

One record per control: case R31, the control as its assertion, and the step that
was running when it failed in "step". Every editor launch gets a private X server no
earlier launch used (tests/contract/xserver.py); a server that outlives its stop adds an
ERROR record "<control>.teardown". Records and exit statuses (PASS 0, FAIL 3, ERROR 4)
follow tests/contract/records.py.
"""

import argparse
from contextlib import ExitStack, contextmanager
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_oracle import EDITOR
from cases.navigation import document_text
import records
from sandbox import Refused, RunTarget, Sandbox
import snapwire as wire
from test_oracle_inspector import GEOMETRY, Inspector, law2_count
from xdriver import XDriverError
from xserver import TeardownError, XServerError, fresh_display, stop


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
FILE_TEXT = b"durable source bytes"
# Use regular key events to seed this tab before the preparatory hot exit.
UNTITLED_TEXT = b"untitledseed"
CLIPBOARD = "CLIPBOARD"
# Match the operation and pipe watchdogs in the X driver conformance fixture.
WATCHDOG = 15
X_TIMEOUT_MS = WATCHDOG * 1000
CASES = {"pre-ack": ("pre-ack-file", "pre-ack-untitled"),
         "post-ack": ("post-ack-file", "post-ack-untitled")}


class Mismatch(Exception):
    def __init__(self, observed, *expected):
        self.observed = observed
        self.expected = expected
        super().__init__(f"expected {expected!r}, got {observed!r}")


def compare_recovery(observed, before, whole):
    if observed == before:
        return "pre-edit"
    if observed == whole:
        return "whole-edit"
    raise Mismatch(observed, before, whole)


def hexed(value):
    return value.hex() if isinstance(value, bytes) else value


def expect_exact(observed, expected):
    if observed != expected:
        raise Mismatch(observed, expected)


def wait_readable(fd, timeout=WATCHDOG):
    with selectors.DefaultSelector() as selector:
        selector.register(fd, selectors.EVENT_READ)
        if timeout <= 0 or not selector.select(timeout):
            raise TimeoutError("readiness deadline expired")


def source_bytes(path, artifact):
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        raise Mismatch(None, FILE_TEXT) from None
    artifact.write_bytes(data)
    return data


def copied_text(driver, window):
    # A private sentinel ensures a stale copy cannot be mistaken for this one.
    sentinel = driver.own(CLIPBOARD, b"copy observation pending")
    driver.chord("Control_L", "a")
    driver.chord("Control_L", "c")
    deadline = time.monotonic() + WATCHDOG
    while time.monotonic() < deadline:
        owner = driver.owner(CLIPBOARD)
        if owner == window:
            return driver.read(CLIPBOARD, window, X_TIMEOUT_MS)
        if owner not in (0, sentinel):
            raise Refused(f"unexpected clipboard owner {owner}, expected window {window}")
        # Observe ownership without reissuing Copy.
        time.sleep(0.01)
    raise TimeoutError("editor did not claim CLIPBOARD after Select All and Copy")


@contextmanager
def launch(step, start, args, sandbox, record, options, post):
    """One editor on a fresh private server, inspected when `post`; yields (child, window,
    inspector, driver). The server, driver and inspector end with the block."""
    child_status = record["children"][step] = {"pid": None, "returncode": None}
    artifacts = Path(record["artifacts"])
    with ExitStack() as stack:
        server, driver = stack.enter_context(fresh_display(
            options.display, artifacts, f"Xvfb-{step}", sandbox.env, timeout=2 * WATCHDOG,
            geometry=GEOMETRY if post else None))
        record["xvfb"][step] = server.pid
        inspector = Inspector(stack) if post else None
        with (artifacts / f"{step}.log").open("wb") as out:
            child = start((*inspector.args, *args) if post else args, stdout=out,
                          stderr=subprocess.STDOUT, pass_fds=inspector.fds if post else ())
        child_status["pid"] = child.pid
        if post:
            inspector.attach(child)
        try:
            window = driver.find(child.pid, X_TIMEOUT_MS)
            sandbox.record_server()
            driver.focus()
        finally:
            child_status["returncode"] = child.poll()
        yield child, window, inspector, driver


def allocation_count(log, record, step):
    """The passive Law-2 count of a clean inspected exit; v1 measured 0 for these windows,
    so the count rule asserts 0 here too."""
    count = law2_count(log.read_bytes())
    record["law2"][step] = count._asdict()
    expect_exact(count.allocations, 0)


def inspect(inspector, driver, operation):
    """Send one marker; its reply must arrive before the watchdog or editor exit."""
    inspector.deadline = time.monotonic() + WATCHDOG
    previous = inspector.serial if operation == wire.Operation.NEXT else 0
    return inspector.send(driver, operation, previous)


def oracle_target(options, post):
    """The inspected build's editor (post-ACK) or the stock oracle, bound to the runner's
    reviewed digest (--digests); an unbound path is ERROR before any launch."""
    base = options.build_dir if post else options.oracle
    if base is None or not base.is_absolute():
        raise Refused("post-ACK needs an absolute --build-dir, pre-ACK an absolute --oracle")
    path = base / EDITOR if post else base
    if str(path) not in options.digests:
        raise Refused(f"--digests binds no reviewed digest for {path}")
    return RunTarget("v1", path, path.parent, options.digests[str(path)])


def recovered(observed, before, whole, post):
    """The recovered buffer's outcome; post-ACK requires the whole edit."""
    if post:
        expect_exact(observed, whole)
        return "whole-edit"
    return compare_recovery(observed, before, whole)


def paste(driver, inspector, clipboard):
    """Paste at the end. An inspected paste returns its pending NEXT request."""
    driver.chord("Control_L", "End")
    if inspector:
        # The settled pre-paste serial is NEXT's lower bound.
        inspector.receive(inspect(inspector, driver, wire.Operation.SNAP))
    driver.own(CLIPBOARD, clipboard)
    # Checked XTest and UngrabServer requests flush the connection.
    driver.chord("Control_L", "v")
    return inspector and inspect(inspector, driver, wire.Operation.NEXT)


def acknowledged(inspector, request, packet, marker, copies, record, key):
    """Require the reply's frame to draw the unique token once per pasted copy.

    Only a paste places the random token, and a paste is one command, so a drawn
    token is a drawn whole edit. Counting tokens tolerates a soft wrap at the space."""
    reply = wire.decode_reply(packet, inspector.mapping, request, after_serial=inspector.serial)
    inspector.serial = reply.submit_serial
    rows = document_text(reply.snapshot.settled)
    record[key] = {"previous_serial": request.previous_serial,
                   "submit_serial": reply.submit_serial, "rows": rows}
    token = marker.decode("ascii").strip()
    if sum(row.count(token) for row in rows) != copies:
        raise Refused("missing acknowledgment witness: no submitted frame shows the paste")


def recovery(options, artifacts, record):
    record["step"] = "private-oracle"
    post = record["assertion_id"].startswith("post-ack")
    target = oracle_target(options, post)
    untitled = record["assertion_id"].endswith("-untitled")
    before = UNTITLED_TEXT if untitled else FILE_TEXT
    # mkdtemp's random suffix makes the marker unique; it fits one document row.
    marker = f" [{record['run_id'].removeprefix('recovery-')}]".encode("ascii")
    whole = before + marker
    record.update(marker_hex=marker.hex(), target={"role": "v1", "path": str(target.executable),
                                                    "sha256": target.sha256})

    with Sandbox(artifacts, target, display=options.display) as sandbox:
        source = sandbox.fixtures / "source.txt"
        record["step"] = "source"
        # The editor is SIGKILLed, the kernel stays up: a closed write is what it reads back.
        with source.open("xb") as stream:
            stream.write(FILE_TEXT)
        initial = source.read_bytes()
        (artifacts / "source.initial").write_bytes(initial)
        expect_exact(initial, FILE_TEXT)

        record["step"] = "initial-launch"
        if untitled:
            with launch("setup", sandbox.start, (str(source),), sandbox, record, options,
                        post) as (child, window, _, driver):
                record["step"] = "initial-file-tab"
                expect_exact(copied_text(driver, window), FILE_TEXT)
                record["step"] = "untitled-durable-seed"
                driver.chord("Control_L", "n")
                for char in UNTITLED_TEXT.decode("ascii"):
                    driver.key(char)
                expect_exact(copied_text(driver, window), before)
                # Establish the durable baseline before the scored edit exists.
                driver.chord("Control_L", "q")
                status = record["children"]["setup"]["returncode"] = child.wait(timeout=WATCHDOG)
                if status != 0:
                    raise Refused(f"untitled seed hot exit returned {status}")
            if post:
                allocation_count(artifacts / "setup.log", record, "setup")
        start, args = (sandbox.restart, ()) if untitled else (sandbox.start, (str(source),))
        with launch("original", start, args, sandbox, record, options, post) as (
                child, window, inspector, driver):
            record["step"] = "untitled-durable-seed" if untitled else "initial-file-tab"
            expect_exact(copied_text(driver, window), before)
            record["step"] = "paste-and-sigkill"
            with os.fdopen(os.pidfd_open(child.pid), "rb") as pidfd:
                request = paste(driver, inspector, b" [decoy]" if options.decoy_paste else marker)
                if post:
                    # Kill on the reply's arrival; its frame is checked once the editor is dead.
                    packet = inspector.packet()
                signal.pidfd_send_signal(pidfd.fileno(), signal.SIGKILL)
                wait_readable(pidfd)
                _, status = os.waitpid(child.pid, 0)
                child.returncode = os.waitstatus_to_exitcode(status)
            signaled = os.WIFSIGNALED(status)
            term_signal = os.WTERMSIG(status) if signaled else None
            record["children"]["original"].update(
                returncode=child.returncode, wait_status=status, wifsignaled=signaled,
                signal=term_signal,
            )
            if not signaled or term_signal != signal.SIGKILL:
                raise Refused(f"expected WIFSIGNALED/SIGKILL, got wait status {status}")
            if post:
                record["step"] = "submitted-action-frame"
                acknowledged(inspector, request, packet, marker, 1, record, "witness")

        record["step"] = "source-after-crash"
        expect_exact(source_bytes(source, artifacts / "source.after-crash"), FILE_TEXT)

        record["step"] = "same-root-restart"
        # Opening by path activates an existing recovered file buffer.
        # It also handles a clean tab omitted from the crashed session.
        with launch("restarted", sandbox.restart, () if untitled else (str(source),), sandbox,
                    record, options, post) as (restarted, window, inspector, driver):
            if restarted.pid == child.pid:
                raise Refused("restarted editor reused the killed editor's PID")
            record["step"] = "recovered-buffer"
            observed = copied_text(driver, window)
            (artifacts / "recovered.txt").write_bytes(observed)
            record["outcome"] = recovered(observed, before, whole, post)
            if options.wrong_expectation:
                # Credited only from legal bytes: the ordinary expectation held just above.
                record["ordinary"] = {"expected": hexed(before if record["outcome"] == "pre-edit"
                                                        else whole), "observed": hexed(observed)}
                wrong = b"intentionally wrong starting bytes"
                recovered(observed, wrong, wrong + marker, post)

            record["step"] = "source-after-restart"
            expect_exact(source_bytes(source, artifacts / "source.after-restart"), FILE_TEXT)
            if restarted.poll() is not None:
                record["children"]["restarted"]["returncode"] = restarted.returncode
                raise Refused(f"restarted editor exited during observation: {restarted.returncode}")
            if post:
                # SIGKILL discards the scored window's count. Repeat the same paste in the
                # restarted editor and count that window at a clean close (count rule).
                record["step"] = "law2-paste-twin"
                request = paste(driver, inspector, marker)
                acknowledged(inspector, request, inspector.packet(), marker, 2, record, "twin")
                # v1's signalfd turns SIGTERM into an ordinary close, which prints the count.
                status = record["children"]["restarted"]["returncode"] = stop(restarted, WATCHDOG)
                if status != 0:
                    raise Refused(f"restarted editor clean close returned {status}")
        if post:
            allocation_count(artifacts / "restarted.log", record, "restarted")


def run_case(case, options):
    artifacts = Path(tempfile.mkdtemp(prefix="recovery-", dir=BUILD))
    # Tier A: disk and clipboard after a relaunch; post-ACK adds the submitted-frame witness (B).
    evidence = {"torn-prefix": "C", "pre-ack-file": "A", "pre-ack-untitled": "A"}.get(case, "A+B")
    record = {"case_id": "R31", "assertion_id": case, "step": "setup", "run_id": artifacts.name,
              "evidence": evidence, "first_required_wave": 2,
              "artifacts": str(artifacts), "display": options.display,
              "children": {}, "law2": {}, "xvfb": {}, "wrong_expectation": options.wrong_expectation,
              "decoy_paste": options.decoy_paste}
    teardown = None
    try:
        battery = Path("/sys/class/power_supply/BAT0/status")
        record["power"] = battery.read_text().strip() if battery.exists() else "BAT0 unavailable"
        record["loadavg"] = Path("/proc/loadavg").read_text().strip()
        if case == "torn-prefix":
            record["step"] = "recovered-buffer"
            marker = b" [whole paste]"
            compare_recovery(FILE_TEXT + marker[:len(marker) // 2], FILE_TEXT, FILE_TEXT + marker)
        else:
            try:
                recovery(options, artifacts, record)
            except TeardownError as exc:
                # Raised over the control's own failure: record that, then this ERROR beside
                # it. Raised alone, it is the control's ERROR: the control did not finish.
                if exc.pending is None:
                    raise
                teardown = exc
                raise exc.pending from None
    except Mismatch as exc:
        record.update(status="FAIL", predicate=f"{record['step']}: {exc}",
                      observed_hex=hexed(exc.observed),
                      expected_hex=[hexed(value) for value in exc.expected])
    except (OSError, Refused, XDriverError, XServerError, TeardownError, wire.ProtocolError,
            subprocess.SubprocessError) as exc:
        record.update(status="ERROR", predicate=f"{record['step']}: {type(exc).__name__}: {exc}")
    else:
        record.update(status="PASS", step="all",
                      predicate=f"recovered buffer is {record.get('outcome', 'exact')}; disk intact")

    (artifacts / "result.json").write_text(json.dumps(record, sort_keys=True) + "\n",
                                           encoding="utf-8")
    records.emit(record)
    if record["status"] != "PASS":
        print(f"R31.{case} {record['status']} {record['predicate']}", file=sys.stderr, flush=True)
    if teardown is not None:
        records.emit({"case_id": "R31", "assertion_id": f"{case}.teardown", "status": "ERROR",
                      "evidence": evidence, "first_required_wave": 2, "run_id": artifacts.name,
                      "predicate": f"TeardownError: {teardown}", "artifacts": str(artifacts)})
        return records.EXIT["ERROR"]
    return records.EXIT[record["status"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", type=Path, help="stock v1 executable (pre-ACK)")
    parser.add_argument("--build-dir", type=Path,
                        help="inspected v1 build/contract-oracle directory (post-ACK)")
    parser.add_argument("--display", default="")
    parser.add_argument("--case", default="pre-ack",
                        choices=(*CASES, *(c for group in CASES.values() for c in group),
                                 "all", "torn-prefix"))
    parser.add_argument("--wrong-expectation", action="store_true",
                        help="intentionally reject the legal recovery bytes for a red control")
    parser.add_argument("--decoy-paste", action="store_true",
                        help="paste other bytes so no frame can witness the marker (red control)")
    records.digests_argument(parser)
    options = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    cases = (sum(CASES.values(), ()) if options.case == "all" else
             CASES.get(options.case, (options.case,)))
    for case in cases:
        status = run_case(case, options)
        if status:
            return status
    return 0


if __name__ == "__main__":
    sys.exit(main())
