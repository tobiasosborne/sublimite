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
  - delete: the scored session's load fence is the journal commit whose checkpoint holds
    DELETE(offset 0, length L) for the buffer, which v1 writes for an emptied buffer
    (checkpoint.c:124 editor_text_ops). Only then is Ctrl+Z sent: an acknowledged key proves
    delivery, not that the editor finished the previous key, so an exit after this fence is
    not an exit in Delete.
  - undo: after Ctrl+Z, whichever comes first: the editor's exit, or the appended journal group
    (GROUP_BEGIN, INSERTs covering [0, L), GROUP_COMMIT) that v1 writes for the undo
    (input.c replay_slice stage_tree; editor_journal_staged groups an insert over one 16 KiB
    slice). Ctrl+S is sent only after that commit and only to a live editor, so an exit
    observed in this wait is an exit in Undo.
  - save: IN_MOVED_TO of the destination name (v1 renames a temporary over it, file.c:1006).

Per variant (twin first, then over), each session in a fresh sandbox with a fresh copy:
  control  ctl.load_save   type "x", load fence, Ctrl+S: bytes == "x" + fixture
  control  ctl.restore     BackSpace, Ctrl+S: bytes == fixture
  control  ctl.delete      Ctrl+A, Delete, Ctrl+S: bytes == b"" (sister of the scored run)
  control  ctl.exit        Ctrl+Q: exit status 0
  scored   roundtrip       Ctrl+A, Delete, delete fence, Ctrl+Z, undo fence or exit,
                           Ctrl+S only to a live editor: bytes == fixture
A control or harness failure is final: it records ERROR and the run stops. A key counts as
injected only on xdriver's acknowledgement while the editor's pidfd is not readable (inject).
A failed session keeps its destination's bytes when they differ from the fixture's.

Scored outcomes (classify), the same on every target: PASS (saved, bytes equal, Ctrl+Q exits
0); FAIL (saved, bytes differ, including a 0 B save); FAIL with signature SIGNATURE_EXIT_105
when the editor exits in Undo (after the delete fence and the injected Ctrl+Z, before the
undo's journal group and before any Ctrl+S) with status 1, its last stderr line is v1's
terminal diagnostic "sublimite: error -105; journal: <its session journal>", and the
destination's bytes and inode metadata are unchanged (amendment A8); ERROR for every other
editor exit (including one before the Ctrl+Z or after the undo's journal group), a changed
input fixture or an expired hang detector. Whether that signature is the reviewed expected
failure of a target is decided by the runner against tests/contract/expectations.json, never
here. Records and exit statuses (PASS 0, FAIL 3, ERROR 4) follow tests/contract/records.py.
Derivations: docs/decisions/v2-he7.15.md.
"""

import argparse
import contextlib
import ctypes
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import select
import selectors
import shutil
import socket
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import records  # noqa: E402
from sandbox import CONTRACT_DISPLAY, RunTarget, Sandbox  # noqa: E402
from xdriver import XDriver, XDriverError  # noqa: E402
import large_undo_fixture as fixture  # noqa: E402

ROOT = HERE.parents[2]
BUILD = ROOT / "build"
DRIVER = BUILD / "xdriver"
CASE = "X3"

# The A8 failure mode, observed on pinned v1 (docs/decisions/wave0-plan-amendments.md A8).
SIGNATURE_EXIT_105 = "exit-1-error-105-destination-untouched"
EVIDENCE = "A"  # Tier A: file bytes and exit status only.
FIRST_REQUIRED_WAVE = 2  # The public GUI case; W1 owns the headless index-limit test.
SCORED_KEYS = (("Control_L", "a"), "Delete", ("Control_L", "z"))

# One deadline per class of wait. These are a hang detector, not a gate: expiry is ERROR
# (run invalid), never PASS or FAIL. Each is the slowest measurement (M) of that wait times
# WATCHDOG_FACTOR, rounded up to a whole second (precedent: R4, 183 s (M) x 10). Measured on
# the ThinkPad, 12 CPUs, 2026-11-23, in the X3 run records named in docs/decisions/v2-he7.15.md
# "Deadlines".
WATCHDOG_FACTOR = 10
XVFB_READY_S = math.ceil(0.26 * WATCHDOG_FACTOR)            # (M) Xvfb readiness, load 1-17
WINDOW_MAP_MS = 1000 * math.ceil(0.481 * WATCHDOG_FACTOR)   # (M) launch -> window, [bat] load 17.18
JOURNAL_COMMIT_S = math.ceil(4.178 * WATCHDOG_FACTOR)       # (M) "x" -> session-* commit, [bat] load 13.28
SAVE_RENAME_S = math.ceil(49.408 * WATCHDOG_FACTOR)         # (M) Ctrl+S -> rename after Ctrl+A + Delete,
                                                            #     [bat] load 13.28; other saves <= 1.712 s
# The delete's commit is measured at 1.013 s quiet ([AC] load 2.34) but waits behind the same
# UI-thread caret follow as the save after Ctrl+A + Delete, which the 49.408 s save includes: in
# the quiet ctl.delete timeline the commit lands 0.013 s before the rename (v2-he7.15).
DELETE_JOURNAL_S = SAVE_RENAME_S
UNDO_JOURNAL_S = math.ceil(2.21 * WATCHDOG_FACTOR)          # (M) Ctrl+Z -> undo group commit, twin,
                                                            #     AC "Not charging", load 4.77
EDITOR_EXIT_S = math.ceil(0.273 * WATCHDOG_FACTOR)          # (M) Ctrl+Z -> exit on the pin;
                                                            #     Ctrl+Q -> exit <= 0.125 s
DRIVER_PIPE_S = 2 * WINDOW_MAP_MS // 1000  # (P) above xdriver's longest command, find (WINDOW_MAP_MS)

IN_MODIFY = 0x00000002
IN_CLOSE_WRITE = 0x00000008
IN_MOVED_FROM = 0x00000040
IN_MOVED_TO = 0x00000080
IN_CREATE = 0x00000100
IN_DELETE = 0x00000200
IN_Q_OVERFLOW = 0x00004000
_EVENT = struct.Struct("iIII")
# v1 journal record (6fec2e8 journal.c valid_record, journal.h:29): a 32 B header of magic,
# length, type, crc, sequence and buffer id; a DELETE payload is offset and length (u64 LE).
V1_JOURNAL_MAGIC = 0x314C4E4A
V1_JOURNAL_INSERT, V1_JOURNAL_DELETE = 2, 3
V1_JOURNAL_GROUP_BEGIN, V1_JOURNAL_GROUP_COMMIT = 9, 10
# Where a scored exit happened, by what the case observed before it (classify).
BEFORE_UNDO = "before-undo"  # before the delete fence held or Ctrl+Z was acknowledged
IN_UNDO = "in-undo"          # after Ctrl+Z was acknowledged, before the undo's journal group
AFTER_UNDO = "after-undo"    # after the undo's journal group (Ctrl+S may have been sent)
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
            # v1 appends journal records in place: only the journal needs IN_MODIFY.
            extra = IN_MODIFY if label == "journal" else 0
            wd = self._libc.inotify_add_watch(self.fd, os.fsencode(directory), mask | extra)
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
    match = CONTRACT_DISPLAY.fullmatch(display)
    if match is None:
        raise HarnessError("X3.harness.display", "display must be a contract display",
                           display, ":120..:199")
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
            except BaseException:
                session.failed = True
                raise
            finally:
                session.finish_artifacts()

    def record(self, assertion, status, predicate, observed, expected, **extra):
        entry = {"case_id": CASE, "assertion_id": assertion.removeprefix(f"{CASE}."),
                 "status": status, "evidence": EVIDENCE, "first_required_wave": FIRST_REQUIRED_WAVE,
                 "predicate": predicate, "observed": observed, "expected": expected,
                 "run_id": self.dir.name, "artifacts": str(self.dir), **extra}
        records.emit(entry)
        self.records.append(entry)
        return entry


def inject(driver, actions, alive):
    """Send `actions` in order; return (acknowledged, error). Each acknowledgement is a receipt:
    the editor was alive just before (`alive()`, its pidfd not readable) and xdriver faked the
    key under a server grab after verifying focus on the bound window (xdriver.c guarded_input).
    Injection stops at the first action that meets a dead editor or a refusing driver."""
    for sent, action in enumerate(actions):
        if not alive():
            return sent, None
        try:
            if isinstance(action, tuple):
                driver.chord(*action)
            else:
                driver.key(action)
        except XDriverError as exc:
            return sent, exc
    return len(actions), None


def journal_commit(label, mask, name):
    return label == "journal" and bool(mask & IN_MOVED_TO) and name.startswith("session-")


def journal_deletes(path, size):
    """Whether the v1 session journal at `path` holds a DELETE(0, size) record."""
    try:
        data = Path(path).read_bytes()
    except FileNotFoundError:  # already replaced by a later commit, which a later event names
        return False
    header = struct.pack("<III", V1_JOURNAL_MAGIC, 48, V1_JOURNAL_DELETE)  # 32 B header + 16 B payload
    pattern = re.escape(header) + b".{20}" + re.escape(struct.pack("<QQ", 0, size))
    return re.search(pattern, data, re.DOTALL) is not None


class JournalTail:
    """The records of one v1 session journal file, read incrementally as v1 appends to it; a
    new inode (a rename commit) is read from its start. Each record is (type, offset, length)
    for INSERT and DELETE, (type, None, None) otherwise. An unparsable tail is left unread."""

    def __init__(self):
        self.inode, self.offset, self.records = None, 0, []

    def read(self, path):
        try:
            stream = open(path, "rb")
        except FileNotFoundError:
            return self.records
        with stream:
            inode = os.fstat(stream.fileno()).st_ino
            if inode != self.inode:
                self.inode, self.offset, self.records = inode, 0, []
            stream.seek(self.offset)
            data = stream.read()
        at = 0
        while len(data) - at >= 32:
            magic, length, kind = struct.unpack_from("<III", data, at)
            if (magic != V1_JOURNAL_MAGIC or length < 32 or length > len(data) - at
                    or (kind == V1_JOURNAL_INSERT and length < 40)
                    or (kind == V1_JOURNAL_DELETE and length != 48)):
                break
            if kind == V1_JOURNAL_INSERT:
                self.records.append((kind, struct.unpack_from("<Q", data, at + 32)[0], length - 40))
            elif kind == V1_JOURNAL_DELETE:
                self.records.append((kind, *struct.unpack_from("<QQ", data, at + 32)))
            else:
                self.records.append((kind, None, None))
            at += length
        self.offset += at
        return self.records


def undo_journaled(records, size):
    """Whether, after DELETE(0, size), a committed group of INSERTs restores [0, size) in order."""
    deleted, group = False, None
    for kind, offset, length in records:
        if kind == V1_JOURNAL_DELETE and (offset, length) == (0, size):
            deleted, group = True, None
        elif kind == V1_JOURNAL_GROUP_BEGIN:
            group = []
        elif kind == V1_JOURNAL_INSERT and group is not None:
            group.append((offset, length))
        elif kind == V1_JOURNAL_GROUP_COMMIT and group is not None:
            end = 0
            for offset, length in group:
                end = end + length if offset == end else -1
            if deleted and end == size:
                return True
            group = None
    return False


class Session:
    def __init__(self, run, box, child, driver, watch, path, label, before):
        self.run, self.box, self.child, self.driver = run, box, child, driver
        self.watch, self.path, self.label, self.before = watch, path, label, before
        self.exit_status = None
        self.failed = False  # set by a failing scored verdict or an exception (Run.editor)

    def alive(self):
        return not select.select([self.watch.pidfd], [], [], 0)[0]

    def inject(self, actions):
        sent, error = inject(self.driver, actions, self.alive)
        self.run.step("keys", label=self.label, injected=sent,
                      actions=[list(a) if isinstance(a, tuple) else a for a in actions])
        return sent, error

    def keys(self, *actions):
        sent, error = self.inject(actions)
        if sent < len(actions):
            raise error or HarnessError(f"X3.{self.label}", "editor exited before the keys were injected",
                                        {"injected": sent}, {"injected": len(actions)})

    def reap(self, assertion):
        """The pidfd reported exit: collect the status without another timer."""
        self.exit_status = self.child.wait(timeout=EDITOR_EXIT_S)
        self.run.step("exited", label=self.label, assertion=assertion, status=self.exit_status)
        return self.exit_status

    def send(self, assertion, actions):
        """Inject `actions`: True when every one was acknowledged, False when the editor exited
        first. A live editor that refuses a key raises."""
        sent, error = self.inject(actions)
        if sent == len(actions):
            return True
        if self.wait_exit(assertion) is None:
            raise error or HarnessError(assertion, "editor alive but not injectable", {"injected": sent},
                                        {"injected": len(actions)})
        return False

    def edits_applied(self, assertion, actions, what="journal commit", matches=journal_commit,
                      deadline_s=JOURNAL_COMMIT_S):
        """Send edit keys and wait for the journal commit they cause (load fence).
        Returns "event" or "exited"."""
        self.watch.drain()
        started = time.monotonic()
        if not self.send(assertion, actions):
            return "exited"
        result = self.watch.wait(what, matches, deadline_s, assertion)
        if result == "exited":
            self.reap(assertion)
        self.run.step("journaled" if result == "event" else "exit-before-commit", label=self.label,
                      assertion=assertion, fence=what, seconds=round(time.monotonic() - started, 3))
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
        if not self.send(assertion, [("Control_L", "s")]):
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
        """Keep the transcript and the child status of this session, and the destination's bytes
        when the session failed and they are not the fixture's (its digest is in the manifest)."""
        self.watch.drain()
        (self.run.dir / f"{self.label}.inotify.json").write_text(json.dumps(self.watch.transcript, indent=1) + "\n")
        if self.exit_status is None and self.child.poll() is not None:
            self.exit_status = self.child.returncode
        final = {"label": self.label, "destination": describe(self.path), "exit": self.exit_status,
                 "running": self.child.poll() is None,
                 "journal_dir": sorted(os.listdir(self.box.data_dir))}
        if self.failed and final["destination"].get("sha256") not in (None, self.before["sha256"]):
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


def capacity_exit(stderr, journal_dir):
    """Whether the last stderr line is v1's terminal diagnostic for EDITOR_ERR_CAPACITY (-105,
    editor.h:27) naming a session journal in `journal_dir`: main.c:342 `fprintf(stderr,
    "sublimite: error %d; journal: %s\\n", rc, journal_path)` is its last write before exit 1."""
    line = f"sublimite: error -105; journal: {re.escape(journal_dir)}/session-[^/\\n]+\\n"
    return re.search(f"(?:\\A|\\n){line}\\Z", stderr) is not None


def classify(original, outcome, input_after):
    """Return (status, predicate, expected, signature) for a scored assertion, on any target.

    outcome is {"kind": "saved", "output": describe(), "quit_exit": int} or
    {"kind": "exited", "stage": BEFORE_UNDO | IN_UNDO | AFTER_UNDO, "exit": int, "stderr": str,
     "journal_dir": str,
     "before": identity(), "after": identity()}.
    signature is SIGNATURE_EXIT_105 for exactly the A8 failure mode, otherwise None.
    """
    want = {"bytes": original["bytes"], "sha256": original["sha256"]}
    if {k: input_after.get(k) for k in want} != want:
        return "ERROR", "input fixture changed during the run", {"input": want}, None
    if outcome["kind"] == "exited":
        if outcome["stage"] == BEFORE_UNDO:
            return "ERROR", "editor exited before the scored Ctrl+Z was injected", want, None
        if outcome["stage"] == AFTER_UNDO:
            return "ERROR", "editor exited after its Undo was journaled", want, None
        if outcome["stage"] != IN_UNDO:
            raise ValueError(f"unknown scored exit stage {outcome['stage']!r}")
        if (outcome["exit"] == 1 and capacity_exit(outcome["stderr"], outcome["journal_dir"])
                and outcome["after"] == outcome["before"]):
            return ("FAIL", "editor exited 1 with error -105 before saving, destination untouched",
                    want, SIGNATURE_EXIT_105)
        return "ERROR", "editor exited during the scored run", want, None
    if outcome["quit_exit"] != 0:
        return "ERROR", "editor did not exit 0 after Ctrl+Q", {"exit": 0}, None
    if outcome["output"] == want:
        return "PASS", "saved bytes equal the original", want, None
    return "FAIL", "saved bytes differ from the original", want, None


def variant_run(run, x, variant):
    source = run.fixtures[variant]
    original = (source["bytes"], source["sha256"])
    with run.editor(x, f"{variant}-control", variant) as s:
        if s.edits_applied(f"X3.{variant}.ctl.load_save", ["x"]) == "exited":
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
        size, data = source["bytes"], s.box.data_dir
        deleted = s.edits_applied(assertion, SCORED_KEYS[:2], "journal commit holding DELETE(0, L)",
                                  lambda label, mask, name: journal_commit(label, mask, name)
                                  and journal_deletes(data / name, size), DELETE_JOURNAL_S)
        stage = BEFORE_UNDO
        if deleted == "event" and s.send(assertion, SCORED_KEYS[2:]):
            # Ctrl+S waits for the undo's journal group, so an exit before it is an exit in Undo.
            stage, tails = IN_UNDO, {}
            undone = s.watch.wait("journal group restoring [0, L)", lambda label, mask, name:
                                  label == "journal" and name.startswith("session-")
                                  and undo_journaled(tails.setdefault(name, JournalTail()).read(data / name),
                                                     size), UNDO_JOURNAL_S, assertion)
            if undone == "exited":
                s.reap(assertion)
            else:
                stage = AFTER_UNDO
            s.run.step("undo-journaled" if undone == "event" else "exit-in-undo", label=s.label,
                       assertion=assertion)
        kind, output, took = s.save(assertion) if stage == AFTER_UNDO else ("exited", None, None)
        if kind == "saved":
            outcome = {"kind": "saved", "output": output, "quit_exit": s.quit(assertion)}
        else:
            outcome = {"kind": "exited", "stage": stage,
                       "exit": s.exit_status, "stderr": s.stderr(), "journal_dir": str(s.box.data_dir),
                       "before": s.before, "after": identity(s.path)}
        input_after = describe(source["path"])
        status, predicate, expected, signature = classify(source, outcome, input_after)
        s.failed = status != "PASS"
    if outcome["kind"] == "exited":
        outcome["stderr"] = outcome["stderr"][-400:]
    run.record(assertion, status, predicate, {**outcome, "input_after": input_after}, expected,
               kind="scored", save_s=None if took is None else round(took, 3),
               **({"signature": signature} if signature else {}))
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description="X3 large delete + undo contract case")
    parser.add_argument("--target", required=True, help="absolute v1 editor executable")
    parser.add_argument("--display", required=True, help="explicit contract display, :120 .. :199")
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
    try:
        if not executable.is_absolute():
            raise HarnessError("X3.harness.target", "target must be absolute", str(executable), "absolute")
        header["target_sha256"] = sha256_file(executable)
        header["fixtures"] = {v: fixture.write(args.fixtures, v) for v in ("twin", "over")}
        run.target = RunTarget("v1", executable, executable.parent, header["target_sha256"])
        run.fixtures = header["fixtures"]
        with private_xvfb(args.display, run_dir) as x:
            run.step("xvfb", pid=x["pid"], display=args.display)
            for variant in ("twin", "over"):
                variant_run(run, x, variant)
    except HarnessError as exc:
        run.record(exc.assertion, "ERROR", exc.predicate, exc.observed, exc.expected, kind="harness")
    except Exception as exc:  # any other runner failure is an ERROR, never a pass
        run.record("X3.harness", "ERROR", f"{type(exc).__name__}: {exc}", None, None, kind="harness")
    exit_code = records.exit_status(entry["status"] for entry in run.records)
    header["power_end"] = power_stamp()
    header["exit"] = exit_code
    summary = {**header, "records": run.records, "steps": run.steps, "artifacts": str(run_dir)}
    (run_dir / "record.json").write_text(json.dumps(summary, indent=2, default=str) + "\n")
    print(f"RUN-END exit={exit_code} record={run_dir / 'record.json'}", flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
