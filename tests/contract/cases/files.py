"""Run the stock-v1, no-snapshot portions of file contracts R3 and R5."""

import argparse
from contextlib import ExitStack, contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

from files_fixtures import BINARY_SEED, SEARCH_FILES, binary_bytes, search_results, search_tree
from sandbox import Refused, RunTarget, Sandbox
from xdriver import XDriver, XDriverError


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
ORACLE_BUILD = Path("/home/tobias/Projects/sublimite-v1-oracle/build")
PIN = "6fec2e8"
ORACLE_SHA256 = "64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115"
# Reuse the X driver conformance fixture's operation and wrapper watchdogs.
WATCHDOG = 15
WAIT_MS = WATCHDOG * 1000
GEOMETRY = "1280x1024x24"  # Xvfb(1)'s default screen geometry.
WAITING = "waiting: requires snapshot adapter (v2-he7.11)"
TIER_A_ASSERTIONS = {
    "R3": ("save-b-bytes", "save-a-bytes", "other-files-unchanged"),
    "R5": ("open-alive", "open-exit", "reopen-alive", "reopen-exit", "bytes-unchanged"),
}
WAITING_ASSERTIONS = {
    "R3": ("results-between-a-and-b", "results-text-still-visible"),
    "R5": ("header-painted", "settled-binary-tab-revisit", "adjacent-text-and-sibling-save"),
}
NEGATIVES = tuple(f"{case}.{assertion}" for case, assertions in TIER_A_ASSERTIONS.items()
                  for assertion in assertions) + ("R3.save-signal",)
IN_MOVED_TO = 0x00000080
IN_ACCESS = 0x00000001
IN_Q_OVERFLOW = 0x00004000
IN_IGNORED = 0x00008000
INOTIFY_EVENT = struct.Struct("=iIII")


class Mismatch(Exception):
    """An observed value contradicted the assertion."""


def describe(value):
    if isinstance(value, bytes):
        # Full binary evidence stays in the retained fixture.
        if len(value) <= 1024:
            return {"hex": value.hex(), "size": len(value)}
        return {"sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
    raise TypeError(f"cannot encode {type(value).__name__}")


def display_ready(fd, number):
    expected = f"{number}\n".encode()
    response = bytearray()
    deadline = time.monotonic() + WATCHDOG
    with selectors.DefaultSelector() as ready:
        ready.register(fd, selectors.EVENT_READ)
        # The display number and newline can arrive in separate pipe writes.
        while len(response) < len(expected) and ready.select(max(0, deadline - time.monotonic())):
            chunk = os.read(fd, len(expected) - len(response))
            if not chunk:
                break
            response.extend(chunk)
    return bytes(response)


@contextmanager
def private_display(display, artifacts):
    number = display[1:]
    socket_path = f"/tmp/.X11-unix/X{number}"
    if os.path.lexists(socket_path) or os.path.lexists(f"/tmp/.X{number}-lock"):
        raise Refused(f"display {display} is occupied")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        probe.bind("\0" + socket_path)

    auth = artifacts / "Xauthority"
    # FamilyLocal and a 128-bit cookie, as in the X driver conformance fixture.
    fields = (os.fsencode(socket.gethostname()), number.encode(), b"MIT-MAGIC-COOKIE-1",
              secrets.token_bytes(16))
    with os.fdopen(os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as file:
        file.write(struct.pack("!H", 256))
        for field in fields:
            file.write(struct.pack("!H", len(field)) + field)
    with ExitStack() as stack:
        read_fd, write_fd = os.pipe()
        stack.callback(os.close, read_fd)
        with os.fdopen(write_fd, "wb") as ready, (artifacts / "Xvfb.log").open("wb") as log:
            server = subprocess.Popen(
                ["Xvfb", display, "-displayfd", str(ready.fileno()), "-noreset",
                 "-nolisten", "tcp", "-auth", str(auth), "-screen", "0", GEOMETRY],
                env={"PATH": os.environ["PATH"], "TMPDIR": str(artifacts)},
                cwd=artifacts, pass_fds=(ready.fileno(),), stdout=log, stderr=log,
                stdin=subprocess.DEVNULL,
            )
        try:
            (artifacts / "Xvfb.pid").write_text(f"{server.pid}\n", encoding="ascii")
            response = display_ready(read_fd, number)
            if response != f"{number}\n".encode() or server.poll() is not None:
                raise Refused(f"Xvfb PID {server.pid} did not claim {display}: {response!r}")
            yield auth
        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=WATCHDOG)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=WATCHDOG)


@contextmanager
def directory_watch(path, mask):
    libc = ctypes.CDLL(None, use_errno=True)
    fd = libc.inotify_init1(os.O_CLOEXEC | os.O_NONBLOCK)
    if fd < 0:
        raise OSError(os.strerror(ctypes.get_errno()))
    try:
        if libc.inotify_add_watch(fd, os.fsencode(path), mask) < 0:
            raise OSError(f"inotify watch {path}: {os.strerror(ctypes.get_errno())}")
        yield fd
    finally:
        os.close(fd)


def wait_for_file_event(fd, path, mask, child):
    deadline = time.monotonic() + WATCHDOG
    # Linux pads each event's NUL-terminated name to struct alignment.
    event_bytes = os.pathconf(path.parent, "PC_NAME_MAX") + 2 * INOTIFY_EVENT.size
    pidfd = os.pidfd_open(child.pid)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(fd, selectors.EVENT_READ)
            ready.register(pidfd, selectors.EVENT_READ)
            while time.monotonic() < deadline:
                pending_fds = {key.fd for key, _ in ready.select(max(0, deadline - time.monotonic()))}
                if pidfd in pending_fds:
                    raise Refused(f"editor exited while waiting for {path.name}: {child.wait()}")
                if fd not in pending_fds:
                    break
                pending = os.read(fd, event_bytes)
                matched = False
                offset = 0
                while offset < len(pending):
                    _, event_mask, _, length = INOTIFY_EVENT.unpack_from(pending, offset)
                    offset += INOTIFY_EVENT.size
                    name = pending[offset:offset + length].rstrip(b"\0")
                    offset += length
                    if event_mask & (IN_Q_OVERFLOW | IN_IGNORED):
                        raise Refused(f"inotify watch lost for {path.parent}: {event_mask:#x}")
                    matched |= bool(event_mask & mask and name == os.fsencode(path.name))
                if matched:
                    return
    finally:
        os.close(pidfd)
    raise TimeoutError(f"no {mask:#x} file event for {path.name} before deadline")


class Case:
    def __init__(self, case_id, artifacts, negative):
        self.id = case_id
        self.artifacts = artifacts / case_id
        self.artifacts.mkdir(mode=0o700)
        self.records = artifacts / "results.jsonl"
        self.run_id = artifacts.name
        self.seed = BINARY_SEED if case_id == "R5" else None
        self.negative = negative
        self.children = []
        self.asserted = []
        self.failed = False
        self.step("setup", "private sandbox and authenticated X driver")

    def note(self, message):
        with (self.artifacts / "steps.log").open("a", encoding="utf-8") as file:
            print(message, file=file)

    def step(self, assertion, expected):
        self.assertion = assertion
        self.expected = expected
        self.observed = None
        self.note(assertion)

    def emit(self, status, *, assertion=None, evidence="A", reason=None):
        assertion = assertion or self.assertion
        record = {
            "run_id": self.run_id, "case_id": self.id, "assertion_id": assertion,
            "evidence": evidence, "first_required_wave": 3 if self.id == "R3" else 2,
            "status": status, "predicate": reason or assertion,
            "observed": self.observed if evidence == "A" else None,
            "expected": self.expected if evidence == "A" else "snapshot adapter available",
            "seed": self.seed, "negative": self.negative,
            "child_status": [{"pid": child.pid, "exit": child.poll()} for child in self.children],
            "artifacts": str(self.artifacts),
        }
        line = json.dumps(record, default=describe)
        with self.records.open("a", encoding="utf-8") as file:
            print(line, file=file)
        print(line, flush=True)
        if status != "PASS":
            print(f"{self.id}.{assertion}: {status} {record['predicate']}", file=sys.stderr)
        self.failed |= status != "PASS" and evidence == "A"

    def check(self, assertion, observed, expected, wrong):
        self.step(assertion, wrong if self.negative == f"{self.id}.{assertion}" else expected)
        self.observed = observed
        if assertion in self.asserted or assertion not in TIER_A_ASSERTIONS[self.id]:
            self.step("assertion-inventory", "each known Tier A assertion executes once")
            raise Refused(f"duplicate or unknown assertion {assertion}")
        self.asserted.append(assertion)
        if observed != self.expected:
            raise Mismatch(f"{assertion}: observed value differs from expectation")
        self.emit("PASS")

    def failure(self, exc):
        if not self.failed:
            self.emit("FAIL" if isinstance(exc, Mismatch) else "ERROR",
                      reason=f"{type(exc).__name__}: {exc}")

    def bind(self, sandbox, driver, child):
        window = driver.find(child.pid, WAIT_MS)
        driver.focus()
        sandbox.record_server()
        self.note(f"focused PID {child.pid} window {window:#x}")
        return window

    def start(self, start, path):
        with (self.artifacts / f"editor-{len(self.children) + 1}.log").open("wb") as log:
            child = start([str(path)], stdout=log, stderr=log)
        self.children.append(child)
        return child


def search_copy(case, driver, child, window, expected):
    case.step("search-readiness", "completed Find Results containing the B match")
    # Own a sentinel first so the observed data comes from an editor copy command.
    sentinel = driver.own("CLIPBOARD", b"files-contract clipboard sentinel")
    deadline = time.monotonic() + WATCHDOG
    while time.monotonic() < deadline:
        driver.chord("Control_L", "a")
        driver.chord("Control_L", "c")
        owner = driver.owner("CLIPBOARD")
        if owner == sentinel:
            if child.poll() is not None:
                raise Refused("editor exited before Find Results was readable")
            continue  # Empty results have nothing to copy.
        if owner != window:
            raise Refused(f"unexpected clipboard owner {owner:#x}")
        copied = driver.read("CLIPBOARD", window, WAIT_MS)
        if copied != case.observed:
            case.note(f"Find Results clipboard: {copied!r}")
        case.observed = copied
        if copied == expected:
            return
        if not expected.startswith(copied):
            raise Mismatch("Find Results clipboard diverged from the fixture result")
        sentinel = driver.own("CLIPBOARD", b"files-contract clipboard sentinel")
    raise TimeoutError("Find Results completion was not observed")


def save(case, driver, child, path, original):
    case.step(f"save-{path.stem}-signal", f"IN_MOVED_TO for {path.name}")
    with directory_watch(path.parent, IN_MOVED_TO) as watch:
        case.observed = path.read_bytes()
        if case.observed != original:
            raise Mismatch(f"{path.name} changed before its save")
        if case.negative != "R3.save-signal":
            driver.chord("Control_L", "s")
        else:
            case.note("negative control withheld Ctrl+S")
        wait_for_file_event(watch, path, IN_MOVED_TO, child)
    case.note(f"IN_MOVED_TO {path.name}")
    return path.read_bytes()


def unrelated_files(root, edited):
    files = {}
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        info = path.lstat()
        if name not in edited and not stat.S_ISDIR(info.st_mode):
            data = path.read_bytes() if stat.S_ISREG(info.st_mode) else None
            files[name] = [data, info.st_ino, info.st_mode, info.st_mtime_ns]
    return files


def quit_editor(driver, child):
    if child.poll() is not None:
        raise Refused(f"editor exited before deliberate quit: {child.returncode}")
    driver.chord("Control_L", "q")
    status = child.wait(timeout=WATCHDOG)
    if status != 0:
        raise Refused(f"editor exited abnormally after Ctrl+Q: {status}")
    return status


def r3(case, sandbox, driver):
    root = sandbox.fixtures
    search_tree(root)
    a, b = "a.txt", "nested/b.c"
    others = unrelated_files(root, {a, b})
    child = case.start(sandbox.start, root / a)
    window = case.bind(sandbox, driver, child)
    case.step("search-start", "Ctrl+Shift+F, cat, Tab, private fixture root, Return")
    driver.chord("Control_L", "Shift_L", "f")
    for key in "cat":
        driver.key(key)
    driver.key("Tab")
    for byte in os.fsencode(root):
        driver.key(byte)
    driver.key("Return")
    search_copy(case, driver, child, window, search_results(root))
    case.step("open-b-match", "Return on the scatter result")
    driver.chord("Control_L", "g")
    driver.key("8")  # One-based form of the reference's zero-based scatter row 7.
    driver.key("Return")
    driver.key("Return")
    driver.key("b")
    expected_b = b"cat\nsbcatter\ncat!\n"
    expected_a = b"a" + SEARCH_FILES[a]
    saved_b = save(case, driver, child, root / b, SEARCH_FILES[b])
    case.check("save-b-bytes", saved_b, expected_b, expected_a)
    after_b = unrelated_files(root, {a, b})

    case.step("edit-a", "Alt+1, Ctrl+Home, type a")
    driver.chord("Alt_L", "1")
    driver.chord("Control_L", "Home")
    driver.key("a")
    saved_a = save(case, driver, child, root / a, SEARCH_FILES[a])
    case.check("save-a-bytes", (saved_a, (root / b).read_bytes()),
               (expected_a, expected_b), (expected_b, expected_b))
    case.step("quit", "Ctrl+Q exits 0")
    quit_editor(driver, child)
    case.check("other-files-unchanged", (after_b, unrelated_files(root, {a, b})),
               (others, others), ({}, {}))


def r5(case, sandbox, driver):
    path = sandbox.fixtures / "fixture.pdf"
    expected = binary_bytes()
    path.write_bytes(expected)
    on_disk = []
    for phase, start in (("open", sandbox.start), ("reopen", sandbox.restart)):
        case.step(f"{phase}-alive", "file opened, mapped owned window, live editor")
        with directory_watch(path.parent, IN_ACCESS) as watch:
            child = case.start(start, path)
            wait_for_file_event(watch, path, IN_ACCESS, child)
        case.note(f"IN_ACCESS {path.name}, PID {child.pid}")
        case.bind(sandbox, driver, child)
        case.check(f"{phase}-alive", child.poll(), None, 0)
        case.step(f"{phase}-exit", "Ctrl+Q exits 0")
        status = quit_editor(driver, child)
        case.check(f"{phase}-exit", status, 0, 1)
        on_disk.append(path.read_bytes())
    case.check("bytes-unchanged", on_disk, [expected, expected], [expected, b"!" + expected[1:]])


def run_case(case, scenario, target, display, auth):
    try:
        with Sandbox(case.artifacts, target, display=display) as sandbox:
            try:
                Path(sandbox.env["XAUTHORITY"]).write_bytes(auth.read_bytes())
                with XDriver(BUILD / "xdriver", display, env=sandbox.env,
                             timeout=WATCHDOG * 2) as driver:
                    try:
                        scenario(case, sandbox, driver)
                        case.step("assertion-inventory", TIER_A_ASSERTIONS[case.id])
                        case.observed = case.asserted
                        if tuple(case.asserted) != TIER_A_ASSERTIONS[case.id]:
                            raise Refused("Tier A assertion inventory incomplete")
                        case.step("teardown", "driver shutdown and fixture retention")
                    except (OSError, RuntimeError, Mismatch, subprocess.TimeoutExpired) as exc:
                        case.failure(exc)
            finally:
                shutil.copytree(sandbox.fixtures, case.artifacts / "fixtures", symlinks=True)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        case.failure(exc)
    for assertion in WAITING_ASSERTIONS[case.id]:
        case.emit("ERROR", assertion=assertion, evidence="B", reason=WAITING)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--display", required=True)
    parser.add_argument("--negative", choices=NEGATIVES)
    args = parser.parse_args()
    if not re.fullmatch(r":16[0-9]", args.display):
        parser.error("--display must be :160 through :169")
    if not args.oracle.is_absolute() or not args.oracle.is_relative_to(ORACLE_BUILD):
        parser.error(f"--oracle must be absolute and under {ORACLE_BUILD}")

    artifacts = Path(tempfile.mkdtemp(prefix="contract-files-", dir=BUILD))
    target = RunTarget("v1", args.oracle, ORACLE_BUILD, ORACLE_SHA256)
    scenarios = {"R3": r3, "R5": r5}
    if args.negative:
        selected = args.negative.split(".", 1)[0]
        scenarios = {selected: scenarios[selected]}
    try:
        battery = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    except OSError:
        battery = None
    manifest = {
        "run_id": artifacts.name, "role": target.role, "oracle": str(target.executable),
        "build_dir": str(target.build_dir), "sha256": target.sha256, "commit": PIN,
        "display": args.display, "geometry": GEOMETRY, "cases": list(scenarios),
        "battery_status": battery, "load_average": os.getloadavg(),
        "negative": args.negative,
    }
    (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"R3/R5 run artifacts in {artifacts}", file=sys.stderr)
    cases = [Case(case_id, artifacts, args.negative) for case_id in scenarios]
    try:
        with private_display(args.display, artifacts) as auth:
            # Evaluate both independent cases once even after an earlier failure.
            for case in cases:
                run_case(case, scenarios[case.id], target, args.display, auth)
    except (OSError, Refused, subprocess.TimeoutExpired, XDriverError) as exc:
        for case in cases:
            case.step("display", "private Xvfb startup and shutdown")
            case.failure(exc)
        return 1
    return int(any(case.failed or WAITING_ASSERTIONS[case.id] for case in cases))


if __name__ == "__main__":
    sys.exit(main())
