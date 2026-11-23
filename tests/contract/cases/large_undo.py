#!/usr/bin/env python3
"""X3: delete an entire over-cap file, undo, save, compare bytes (Tier A, file bytes only).

One invocation is one run. The run owns a private Xvfb on an explicit display in :140..:149,
launches every editor through tests/contract/sandbox.py and drives input through
tests/contract/xdriver.py. Every wait is woken by an event: an inotify watch armed before the
keys whose effect it observes, or the editor's pidfd. The only timers are the hang detectors
below; an expired one makes the run invalid (ERROR) and never decides a verdict.

Fences (v1 at 6fec2e8, the only target this case drives today):
  - load: the first edit keys of a session are followed by the journal commit they cause
    (IN_MOVED_TO session-* in $XDG_DATA_HOME/sublimite). v1 parks edit keys while a file is
    opening but lets Ctrl+S overtake them (editor.c queue_pick), and saving a still-opening
    buffer is a silent no-op (session.c request_buffer). Invariant: an idle v1 writes nothing
    there, so a commit after the keys implies an applied edit, hence an adopted buffer.
  - save: IN_MOVED_TO of the destination name (v1 renames a temporary over it, file.c:1006).

Per variant (twin first, then over), each session in a fresh sandbox with a fresh copy:
  control  ctl.load_save   type "x", load fence, Ctrl+S: bytes == "x" + fixture
  control  ctl.restore     BackSpace, Ctrl+S: bytes == fixture
  control  ctl.delete      Ctrl+A, Delete, Ctrl+S: bytes == b"" (sister of the scored run)
  control  ctl.exit        Ctrl+Q: exit status 0
  scored   roundtrip       Ctrl+A, Delete, Ctrl+Z, load fence, Ctrl+S: bytes == fixture
A control or harness failure is final: it records ERROR and the run stops.

Scored outcomes (classify): PASS (saved, bytes equal, Ctrl+Q exits 0); FAIL (saved, bytes
differ, including a 0 B save); XFAIL only for the reviewed signature bound to the target's
SHA-256 (EXPECTATIONS, amendment A8): the editor exits with status 1, stderr contains
"error -105", and the destination's bytes and inode metadata are unchanged; XPASS (bytes
round-trip where XFAIL is expected; fails the run); ERROR for every other editor exit, a
changed input fixture or an expired hang detector.
Exit status: 0 all PASS/XFAIL, 1 a FAIL or XPASS, 2 an ERROR.
Derivations: docs/decisions/v2-he7.15.md.
"""

import argparse
import contextlib
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import shutil
import socket
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from sandbox import RunTarget, Sandbox  # noqa: E402
from xdriver import XDriver, XDriverError  # noqa: E402
import large_undo_fixture as fixture  # noqa: E402

ROOT = HERE.parents[2]
BUILD = ROOT / "build"
DRIVER = BUILD / "xdriver"
CASE = "X3"

# Pinned oracle: v1 6fec2e8, /home/tobias/Projects/sublimite-v1-oracle/build/sublimite.
# Reviewed expectation, docs/decisions/wave0-plan-amendments.md A8.
V1_PIN_SHA256 = "64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115"
XFAIL_EXIT_105 = "exit-1-error-105-destination-untouched"
EXPECTATIONS = {
    "X3.twin.roundtrip": {},
    "X3.over.roundtrip": {V1_PIN_SHA256: XFAIL_EXIT_105},
}
V1_CAPACITY_ERROR = "error -105"  # v1 main.c:342 prints EDITOR_ERR_CAPACITY (editor.h:27)

# One deadline per class of wait. These are a hang detector, not a gate: expiry is ERROR
# (run invalid), never PASS, FAIL or XFAIL. All are (E): at least 2x the slowest step
# measured (M) on 2026-11-23, ThinkPad, BAT0 Discharging [bat], 12 CPUs. The measurements
# and their loads are in docs/decisions/v2-he7.15.md "Deadlines".
XVFB_READY_S = 15         # (M) 0.18-0.26 s, load 1-17
WINDOW_MAP_MS = 30_000    # (M) 0.12-0.29 s launch -> mapped window, load 1-17
JOURNAL_COMMIT_S = 120    # (M) 0.97-4.18 s keys -> session-* commit, load 11-13
SAVE_RENAME_S = 400       # (M) 0.15-0.93 s; after Ctrl+A+Delete 0.78-49.4 s at load 1-13, 188.6 s at load 34
EDITOR_EXIT_S = 60        # (M) 0.12-0.18 s after Ctrl+Q, or after Ctrl+Z on the pin
DRIVER_PIPE_S = 60        # xdriver pipe watchdog; must exceed WINDOW_MAP_MS (v2-he7.9)

IN_CLOSE_WRITE = 0x00000008
IN_MOVED_FROM = 0x00000040
IN_MOVED_TO = 0x00000080
IN_CREATE = 0x00000100
IN_DELETE = 0x00000200
IN_Q_OVERFLOW = 0x00004000
_EVENT = struct.Struct("iIII")
_STAT_FIELDS = ("st_dev", "st_ino", "st_mode", "st_nlink", "st_uid", "st_gid", "st_size",
                "st_mtime_ns", "st_ctime_ns")  # atime excluded: reading a file may advance it


class HarnessError(RuntimeError):
    """A control or runner step failed; the run records ERROR."""

    def __init__(self, assertion, predicate, observed, expected):
        super().__init__(f"{assertion}: {predicate}")
        self.assertion, self.predicate = assertion, predicate
        self.observed, self.expected = observed, expected


def utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def power_stamp():
    status = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    return {"utc": utc(), "bat0": status, "loadavg": Path("/proc/loadavg").read_text().split()[:3]}


def sha256_file(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def describe(path):
    """Size and digest of a file, or its absence."""
    try:
        return {"bytes": os.stat(path).st_size, "sha256": sha256_file(path)}
    except FileNotFoundError:
        return {"absent": True}


def identity(path):
    """Bytes plus the inode metadata a write or rename would change."""
    info = os.stat(path, follow_symlinks=False)
    return {**{name: getattr(info, name) for name in _STAT_FIELDS}, "sha256": sha256_file(path)}


class DirWatch:
    """inotify on named directories plus the editor's pidfd, armed before the observed keys."""

    def __init__(self, directories, pidfd):
        self._libc = ctypes.CDLL(None, use_errno=True)
        self.fd = self._libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if self.fd < 0:
            raise OSError(ctypes.get_errno(), "inotify_init1")
        self.pidfd = pidfd
        self._labels = {}
        mask = IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE | IN_DELETE
        for label, directory in directories.items():
            wd = self._libc.inotify_add_watch(self.fd, os.fsencode(directory), mask)
            if wd < 0:
                err = ctypes.get_errno()
                os.close(self.fd)
                raise OSError(err, f"inotify_add_watch {directory}")
            self._labels[wd] = label
        self.transcript = []

    def drain(self):
        """Read queued events into the transcript and return them."""
        events = []
        while True:
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                return events
            offset = 0
            while offset < len(data):
                wd, mask, _cookie, length = _EVENT.unpack_from(data, offset)
                name = os.fsdecode(data[offset + _EVENT.size: offset + _EVENT.size + length].rstrip(b"\0"))
                offset += _EVENT.size + length
                label = self._labels.get(wd, "?")
                events.append((label, mask, name))
                self.transcript.append({"t": round(time.monotonic(), 6), "dir": label,
                                        "mask": hex(mask), "name": name})

    def wait(self, what, matches, deadline_s, assertion):
        """Return "event" when matches(label, mask, name) holds, "exited" when the editor
        exits first. Expiry of the hang detector raises HarnessError (ERROR)."""
        deadline = time.monotonic() + deadline_s
        with selectors.DefaultSelector() as selector:
            selector.register(self.fd, selectors.EVENT_READ)
            selector.register(self.pidfd, selectors.EVENT_READ)
            while True:
                for _label, mask, _name in (events := self.drain()):
                    if mask & IN_Q_OVERFLOW:
                        raise HarnessError(assertion, "inotify queue overflow", "overflow", what)
                if any(matches(*event) for event in events):
                    return "event"
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise HarnessError(assertion, f"hang detector expired waiting for {what}",
                                       {"deadline_s": deadline_s}, what)
                ready = {key.fd for key, _ in selector.select(remaining)}
                if self.pidfd in ready and self.fd not in ready:
                    return "exited"

    def close(self):
        os.close(self.fd)


def display_free(number):
    path = f"/tmp/.X11-unix/X{number}"
    if os.path.lexists(path) or os.path.lexists(f"/tmp/.X{number}-lock"):
        return False
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        try:
            probe.bind("\0" + path)
        except OSError:
            return False
    return True


@contextlib.contextmanager
def private_xvfb(display, run_dir):
    """Start Xvfb on exactly `display` with a fresh MIT cookie; stop it by its own pid."""
    match = re.fullmatch(r":(14[0-9])", display)
    if match is None:
        raise HarnessError("X3.harness.display", "display must be :140 through :149", display, ":140..:149")
    number = int(match[1])
    if not display_free(number):
        raise HarnessError("X3.harness.display", "display is in use", display, "free")
    auth = run_dir / "xauthority"
    # Xauthority FamilyLocal record (network order) with a fresh 128-bit MIT cookie.
    fields = (os.fsencode(socket.gethostname()), str(number).encode(),
              b"MIT-MAGIC-COOKIE-1", secrets.token_bytes(16))
    record = struct.pack("!H", 256) + b"".join(struct.pack("!H", len(f)) + f for f in fields)
    fd = os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(record)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "XAUTHORITY": str(auth)}
    read_fd, write_fd = os.pipe()
    with (run_dir / "Xvfb.log").open("wb") as log:
        server = subprocess.Popen(
            ["Xvfb", display, "-displayfd", str(write_fd), "-noreset", "-nolisten", "tcp",
             "-auth", str(auth)],
            env=env, pass_fds=(write_fd,), stdin=subprocess.DEVNULL, stdout=log, stderr=log)
    os.close(write_fd)
    try:
        with os.fdopen(read_fd, "rb") as ready, selectors.DefaultSelector() as selector:
            selector.register(ready, selectors.EVENT_READ)
            if not selector.select(XVFB_READY_S):
                raise HarnessError("X3.harness.display", "hang detector expired: Xvfb readiness",
                                   "silent", display)
            answer = ready.readline()
        if answer != f"{number}\n".encode() or server.poll() is not None:
            raise HarnessError("X3.harness.display", "Xvfb did not claim the display",
                               answer.decode(errors="replace"), str(number))
        yield {"pid": server.pid, "env": env, "record": record}
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=XVFB_READY_S)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


class Run:
    def __init__(self, target, display, run_dir, fixtures):
        self.target, self.display, self.dir, self.fixtures = target, display, run_dir, fixtures
        self.records = []
        self.steps = []
        self.t0 = time.monotonic()

    def step(self, what, **extra):
        entry = {"t": round(time.monotonic() - self.t0, 3), "step": what, **extra}
        self.steps.append(entry)
        print(f"[{entry['t']:8.3f}] {what} {json.dumps(extra) if extra else ''}", flush=True)

    @contextlib.contextmanager
    def editor(self, x, label, variant):
        """One sandbox, one fixture copy, one editor, one driver; yields a Session."""
        source = self.fixtures[variant]
        base = self.dir / "cases"
        base.mkdir(mode=0o700, exist_ok=True)
        with contextlib.ExitStack() as stack:
            out = stack.enter_context((self.dir / f"{label}.stdout").open("wb"))
            err = stack.enter_context((self.dir / f"{label}.stderr").open("wb"))
            box = stack.enter_context(Sandbox(base, self.target, display=self.display))
            # The runner populates the case's private Xauthority with the run cookie.
            with open(box.root / "Xauthority", "r+b") as auth:
                auth.write(x["record"])
            path = box.work / Path(source["path"]).name
            shutil.copyfile(source["path"], path)
            before = identity(path)
            if before["sha256"] != source["sha256"]:
                raise HarnessError(f"X3.{variant}.{label}", "fixture copy differs", before, source)
            child = box.start([str(path)], stdout=out, stderr=err)
            pidfd = os.pidfd_open(child.pid)
            stack.callback(os.close, pidfd)
            self.step("launch", label=label, pid=child.pid, path=str(path))
            driver = stack.enter_context(XDriver(DRIVER, self.display, env=x["env"], timeout=DRIVER_PIPE_S))
            window = driver.find(child.pid, WINDOW_MAP_MS)
            driver.focus()
            self.step("window", label=label, xid=window)
            # v1 creates its session journal before mapping the window (main.c).
            if not box.data_dir.is_dir():
                raise HarnessError(f"X3.{variant}.{label}", "journal directory absent after map",
                                   str(box.data_dir), "directory")
            watch = DirWatch({"work": box.work, "journal": box.data_dir}, pidfd)
            stack.callback(watch.close)
            session = Session(self, box, child, driver, watch, path, label, before)
            try:
                yield session
            finally:
                session.finish_artifacts()

    def record(self, assertion, status, predicate, observed, expected, **extra):
        entry = {"case": CASE, "assertion": assertion, "status": status, "predicate": predicate,
                 "observed": observed, "expected": expected, **extra}
        self.records.append(entry)
        print("RESULT " + json.dumps(entry, default=str), flush=True)
        return entry


def journal_commit(label, mask, name):
    return label == "journal" and bool(mask & IN_MOVED_TO) and name.startswith("session-")


class Session:
    def __init__(self, run, box, child, driver, watch, path, label, before):
        self.run, self.box, self.child, self.driver = run, box, child, driver
        self.watch, self.path, self.label, self.before = watch, path, label, before
        self.exit_status = None

    def keys(self, *actions):
        for action in actions:
            if isinstance(action, tuple):
                self.driver.chord(*action)
            else:
                self.driver.key(action)
        self.run.step("keys", label=self.label, actions=[list(a) if isinstance(a, tuple) else a
                                                         for a in actions])

    def reap(self, assertion):
        """The pidfd reported exit: collect the status without another timer."""
        self.exit_status = self.child.wait(timeout=EDITOR_EXIT_S)
        self.run.step("exited", label=self.label, assertion=assertion, status=self.exit_status)
        return self.exit_status

    def edits_applied(self, assertion, *actions):
        """Send edit keys and wait for the journal commit they cause (load fence).
        Returns "event" or "exited"."""
        self.watch.drain()
        started = time.monotonic()
        try:
            self.keys(*actions)
        except XDriverError:
            if self.wait_exit(assertion) is None:
                raise
            return "exited"
        result = self.watch.wait("journal commit", journal_commit, JOURNAL_COMMIT_S, assertion)
        if result == "exited":
            self.reap(assertion)
        self.run.step("journaled" if result == "event" else "exit-before-commit", label=self.label,
                      assertion=assertion, seconds=round(time.monotonic() - started, 3))
        return result

    def wait_exit(self, assertion):
        """After the driver lost the window: exit status if the editor exits in time."""
        try:
            return self.reap(assertion)
        except subprocess.TimeoutExpired:
            return None

    def save(self, assertion):
        """Ctrl+S, then the save fence. Returns ("saved", describe, seconds) or ("exited", ...)."""
        self.watch.drain()
        started = time.monotonic()
        try:
            self.driver.chord("Control_L", "s")
        except XDriverError:
            if self.wait_exit(assertion) is None:
                raise
            return "exited", None, None
        result = self.watch.wait(f"IN_MOVED_TO {self.path.name}",
                                 lambda label, mask, name: label == "work" and bool(mask & IN_MOVED_TO)
                                 and name == self.path.name, SAVE_RENAME_S, assertion)
        if result == "exited":
            self.reap(assertion)
            return "exited", None, None
        took = time.monotonic() - started
        observed = describe(self.path)
        self.run.step("saved", label=self.label, assertion=assertion, seconds=round(took, 3), **observed)
        return "saved", observed, took

    def quit(self, assertion):
        self.driver.chord("Control_L", "q")
        try:
            return self.reap(assertion)
        except subprocess.TimeoutExpired:
            raise HarnessError(assertion, "hang detector expired: no exit after Ctrl+Q",
                               "running", {"exit": 0}) from None

    def stderr(self):
        return (self.run.dir / f"{self.label}.stderr").read_text(errors="replace")

    def finish_artifacts(self):
        """Keep the transcript, the child status and the destination bytes of this session."""
        self.watch.drain()
        (self.run.dir / f"{self.label}.inotify.json").write_text(json.dumps(self.watch.transcript, indent=1) + "\n")
        if self.exit_status is None and self.child.poll() is not None:
            self.exit_status = self.child.returncode
        final = {"label": self.label, "destination": describe(self.path), "exit": self.exit_status,
                 "running": self.child.poll() is None,
                 "journal_dir": sorted(os.listdir(self.box.data_dir))}
        if self.path.exists():
            kept = self.run.dir / f"{self.label}.destination"
            shutil.copyfile(self.path, kept)
            final["preserved"] = str(kept)
        (self.run.dir / f"{self.label}.final.json").write_text(json.dumps(final, indent=1) + "\n")
        self.run.step("session-end", **{k: v for k, v in final.items() if k != "journal_dir"})


def control(run, session, assertion, saved, expected):
    """A control must save exactly `expected` ((bytes, sha256)); anything else is ERROR."""
    kind, observed, took = saved
    want = {"bytes": expected[0], "sha256": expected[1]}
    if kind != "saved":
        raise HarnessError(assertion, "editor exited during a control", {"exit": session.exit_status,
                           "stderr": session.stderr()[-400:]}, want)
    if observed != want:
        raise HarnessError(assertion, "saved bytes differ from the control expectation", observed, want)
    run.record(assertion, "PASS", "saved bytes equal the control expectation", observed, want,
               kind="control", save_s=round(took, 3))


def digest_of(*parts):
    """(length, sha256) of the concatenation of byte strings and fixture paths."""
    digest, length = hashlib.sha256(), 0
    for part in parts:
        if isinstance(part, bytes):
            digest.update(part)
            length += len(part)
            continue
        with open(part, "rb") as stream:
            while block := stream.read(1 << 20):
                digest.update(block)
                length += len(block)
    return length, digest.hexdigest()


def classify(assertion, target_sha, original, outcome, input_after):
    """Return (status, predicate, expected) for a scored assertion.

    outcome is {"kind": "saved", "output": describe(), "quit_exit": int} or
    {"kind": "exited", "exit": int, "stderr": str, "before": identity(), "after": identity()}.
    """
    want = {"bytes": original["bytes"], "sha256": original["sha256"]}
    signature = EXPECTATIONS[assertion].get(target_sha)
    if {k: input_after.get(k) for k in want} != want:
        return "ERROR", "input fixture changed during the run", {"input": want}
    if signature is not None and signature != XFAIL_EXIT_105:
        return "ERROR", f"unknown expectation signature {signature}", want
    xfail = {"exit": 1, "stderr_contains": V1_CAPACITY_ERROR, "destination": "bytes and inode metadata unchanged"}
    if outcome["kind"] == "exited":
        matched = (outcome["exit"] == 1 and V1_CAPACITY_ERROR in outcome["stderr"]
                   and outcome["after"] == outcome["before"])
        if signature is not None and matched:
            return "XFAIL", "editor exited 1 with error -105, destination untouched (pinned v1, A8)", xfail
        return "ERROR", "editor exited during the scored run", xfail if signature else want
    if outcome["quit_exit"] != 0:
        return "ERROR", "editor did not exit 0 after Ctrl+Q", {"exit": 0}
    if outcome["output"] == want:
        if signature is not None:
            return "XPASS", "bytes round-tripped where the pinned binary is expected to exit", xfail
        return "PASS", "saved bytes equal the original", want
    return "FAIL", "saved bytes differ from the original", want


def variant_run(run, x, variant):
    source = run.fixtures[variant]
    original = (source["bytes"], source["sha256"])
    with run.editor(x, f"{variant}-control", variant) as s:
        if s.edits_applied(f"X3.{variant}.ctl.load_save", "x") == "exited":
            raise HarnessError(f"X3.{variant}.ctl.load_save", "editor exited during a control",
                               {"exit": s.exit_status, "stderr": s.stderr()[-400:]}, "running")
        control(run, s, f"X3.{variant}.ctl.load_save", s.save(f"X3.{variant}.ctl.load_save"),
                digest_of(b"x", source["path"]))
        s.keys("BackSpace")
        control(run, s, f"X3.{variant}.ctl.restore", s.save(f"X3.{variant}.ctl.restore"), original)
        s.keys(("Control_L", "a"), "Delete")
        control(run, s, f"X3.{variant}.ctl.delete", s.save(f"X3.{variant}.ctl.delete"), digest_of(b""))
        status = s.quit(f"X3.{variant}.ctl.exit")
        if status != 0:
            raise HarnessError(f"X3.{variant}.ctl.exit", "editor exit status after Ctrl+Q",
                               {"exit": status}, {"exit": 0})
        run.record(f"X3.{variant}.ctl.exit", "PASS", "editor exited 0 after Ctrl+Q", {"exit": 0},
                   {"exit": 0}, kind="control")
    assertion = f"X3.{variant}.roundtrip"
    with run.editor(x, f"{variant}-scored", variant) as s:
        result = s.edits_applied(assertion, ("Control_L", "a"), "Delete", ("Control_L", "z"))
        took = None
        if result == "event":
            kind, output, took = s.save(assertion)
        else:
            kind = "exited"
        if kind == "saved":
            outcome = {"kind": "saved", "output": output, "quit_exit": s.quit(assertion)}
        else:
            outcome = {"kind": "exited", "exit": s.exit_status,
                       "stderr": s.stderr(), "before": s.before, "after": identity(s.path)}
    input_after = describe(source["path"])
    status, predicate, expected = classify(assertion, run.target.sha256, source, outcome, input_after)
    if outcome["kind"] == "exited":
        outcome["stderr"] = outcome["stderr"][-400:]
    run.record(assertion, status, predicate, {**outcome, "input_after": input_after}, expected,
               kind="scored", save_s=None if took is None else round(took, 3))
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description="X3 large delete + undo contract case")
    parser.add_argument("--target", required=True, help="absolute v1 editor executable")
    parser.add_argument("--display", required=True, help="explicit :140 .. :149")
    parser.add_argument("--fixtures", default=str(BUILD / "large-undo" / "fixtures"))
    parser.add_argument("--runs", default=str(BUILD / "large-undo" / "runs"))
    args = parser.parse_args(argv)

    executable = Path(args.target)
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{os.getpid()}"
    run_dir = Path(args.runs).resolve() / run_id
    run_dir.mkdir(parents=True, mode=0o700)
    header = {"case": CASE, "run_id": run_id, "role": "v1", "target": str(executable),
              "display": args.display, "argv": sys.argv, "power_start": power_stamp()}
    print("RUN " + json.dumps(header), flush=True)
    run = Run(None, args.display, run_dir, {})
    exit_code = 2
    try:
        if not executable.is_absolute():
            raise HarnessError("X3.harness.target", "target must be absolute", str(executable), "absolute")
        header["target_sha256"] = sha256_file(executable)
        header["fixtures"] = {v: fixture.write(args.fixtures, v) for v in ("twin", "over")}
        run.target = RunTarget("v1", executable, executable.parent, header["target_sha256"])
        run.fixtures = header["fixtures"]
        statuses = []
        with private_xvfb(args.display, run_dir) as x:
            run.step("xvfb", pid=x["pid"], display=args.display)
            for variant in ("twin", "over"):
                statuses.append(variant_run(run, x, variant))
        exit_code = 2 if "ERROR" in statuses else 1 if {"FAIL", "XPASS"} & set(statuses) else 0
    except HarnessError as exc:
        run.record(exc.assertion, "ERROR", exc.predicate, exc.observed, exc.expected, kind="harness")
    except Exception as exc:  # any other runner failure is an ERROR, never a pass
        run.record("X3.harness", "ERROR", f"{type(exc).__name__}: {exc}", None, None, kind="harness")
    header["power_end"] = power_stamp()
    header["exit"] = exit_code
    summary = {**header, "records": run.records, "steps": run.steps, "artifacts": str(run_dir)}
    (run_dir / "record.json").write_text(json.dumps(summary, indent=2, default=str) + "\n")
    print(f"RUN-END exit={exit_code} record={run_dir / 'record.json'}", flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
