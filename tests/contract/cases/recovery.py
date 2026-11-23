"""R31 process-death recovery controls against the pinned v1 oracle.

Exit status: 0 PASS, 3 FAIL, 4 ERROR. Python reserves 1 for an uncaught
exception and argparse 2 for a usage error, so neither names a case result.
"""

import argparse
from contextlib import ExitStack, contextmanager
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_oracle import EDITOR, PATCH, PIN, STAMP, sha256
from cases.navigation import SnapshotClient, document_text, stop, wait_exit
from sandbox import Refused, RunTarget, Sandbox
import snapwire as wire
from test_oracle_inspector import DEPTH, GUARD, HEIGHT, WIDTH
from xdriver import XDriver, XDriverError


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
ORACLE_SHA256 = "64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115"
FILE_TEXT = b"durable source bytes"
# Use regular key events to seed this tab before the preparatory hot exit.
UNTITLED_TEXT = b"untitledseed"
CLIPBOARD = "CLIPBOARD"
# Match the operation and pipe watchdogs in the X driver conformance fixture.
WATCHDOG = 15
X_TIMEOUT_MS = WATCHDOG * 1000
EXIT = {"PASS": 0, "FAIL": 3, "ERROR": 4}
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


@contextmanager
def private_xvfb(display, artifacts, screen=()):
    if not re.fullmatch(r":15[0-9]", display):
        raise Refused("recovery requires an explicit display from :150 to :159")
    number = display[1:]
    endpoint = f"/tmp/.X11-unix/X{number}"
    if os.path.lexists(endpoint) or os.path.lexists(f"/tmp/.X{number}-lock"):
        raise Refused(f"display {display} is already in use")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        probe.bind("\0" + endpoint)

    # Xauthority FamilyLocal with an owner-only, fresh 128-bit MIT cookie.
    fields = (os.fsencode(socket.gethostname()), number.encode("ascii"),
              b"MIT-MAGIC-COOKIE-1", secrets.token_bytes(16))
    cookie = struct.pack("!H", 256)
    cookie += b"".join(struct.pack("!H", len(field)) + field for field in fields)
    auth = artifacts / "Xauthority"
    with os.fdopen(os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as file:
        file.write(cookie)

    read_fd, write_fd = os.pipe()
    try:
        with os.fdopen(write_fd, "wb") as ready, (artifacts / "Xvfb.log").open("wb") as log:
            server = subprocess.Popen(
                ["Xvfb", display, "-displayfd", str(ready.fileno()), "-noreset",
                 "-nolisten", "tcp", "-auth", str(auth), *screen],
                env={"HOME": str(artifacts), "TMPDIR": str(artifacts)},
                stdin=subprocess.DEVNULL, stdout=log, stderr=log, pass_fds=(ready.fileno(),),
            )
        try:
            expected = f"{number}\n".encode("ascii")
            response = b""
            deadline = time.monotonic() + WATCHDOG
            while len(response) < len(expected):
                wait_readable(read_fd, deadline - time.monotonic())
                data = os.read(read_fd, len(expected) - len(response))
                if not data:
                    break
                response += data
            if response != expected or server.poll() is not None:
                raise Refused(f"Xvfb did not claim {display}: {response!r}")
            yield server.pid, cookie
        finally:
            # Only the Xvfb process launched by this invocation is stopped.
            server.terminate()
            try:
                server.wait(timeout=WATCHDOG)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=WATCHDOG)
                raise
    finally:
        os.close(read_fd)
        auth.unlink()


def durable_source(path):
    with path.open("xb") as source:
        source.write(FILE_TEXT)
        source.flush()
        os.fsync(source.fileno())
    fd = os.open(path.parent, os.O_DIRECTORY | os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)
    return path.read_bytes()


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


def launch(artifacts, step, start, args, sandbox, driver, record, stack):
    """Start one editor; with an ExitStack it is inspected for its lifetime."""
    child_status = record["children"][step] = {"pid": None, "returncode": None}
    inspector = None if stack is None else SnapshotClient(stack)
    if inspector:
        args = (*inspector.args, *args)
    log = artifacts / f"{step}.log"
    with log.open("wb") as out:
        child = start(args, stdout=out, stderr=subprocess.STDOUT,
                      pass_fds=inspector.fds if inspector else ())
    child_status["pid"] = child.pid
    if inspector:
        inspector.writer.close()
        inspector.pidfd = os.pidfd_open(child.pid)
        stack.callback(os.close, inspector.pidfd)
    try:
        window = driver.find(child.pid, X_TIMEOUT_MS)
        sandbox.record_server()
        driver.focus()
    finally:
        child_status["returncode"] = child.poll()
    return child, window, inspector


def allocation_count(log, record, step):
    """Record the passive Law-2 count printed by a clean inspected exit."""
    report = GUARD.search(log.read_bytes())
    if report is None:
        raise Refused(f"{step} editor printed no allocation count")
    active, allocations, scopes = map(int, report.groups())
    record["law2"][step] = {"active": active, "allocations": allocations, "scopes": scopes}
    # Fail closed on an inert guard or a vacuous window (docs/decisions/v2-he7.11.md).
    if active != 1 or scopes == 0:
        raise Refused(f"{step} allocation guard is inert or measured no window: {report.group()!r}")
    # v1 measured 0 for these windows, so the count rule asserts 0 here too.
    expect_exact(allocations, 0)


def inspect(inspector, driver, operation):
    """Send one marker; its reply must arrive before the watchdog or editor exit."""
    inspector.deadline = time.monotonic() + WATCHDOG
    previous = inspector.serial if operation == wire.Operation.NEXT else 0
    return inspector.send(driver, operation, previous)


def oracle_target(options, post):
    if post:
        build = options.build_dir
        if build is None or not build.is_absolute():
            raise Refused("--build-dir must be the absolute inspected v1 contract-oracle directory")
        stamp = json.loads((build / STAMP).read_text(encoding="utf-8"))
        if stamp["pin"] != PIN or stamp["patch_sha256"] != sha256(PATCH):
            raise Refused("oracle manifest differs from the pinned source or patch")
        return RunTarget("v1", build / EDITOR, build, stamp["executables"][EDITOR])
    oracle = options.oracle
    if oracle is None or not oracle.is_absolute():
        raise Refused("--oracle must be the absolute path to the pinned v1 executable")
    return RunTarget("v1", oracle, oracle.parent, ORACLE_SHA256)


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
    record["assertion_id"] = "private-oracle"
    post = record["case_id"].startswith("R31.post-ack")
    target = oracle_target(options, post)
    untitled = record["case_id"].endswith("-untitled")
    before = UNTITLED_TEXT if untitled else FILE_TEXT
    # mkdtemp's random suffix makes the marker unique; it fits one document row.
    marker = f" [{record['run_id'].removeprefix('recovery-')}]".encode("ascii")
    whole = before + marker
    record.update(marker_hex=marker.hex(), target={"role": "v1", "path": str(target.executable),
                                                    "sha256": target.sha256})
    # Inspected runs use the geometry from which the snapshot mapping capacity is derived.
    screen = ("-screen", "0", f"{WIDTH}x{HEIGHT}x{DEPTH}") if post else ()

    with private_xvfb(options.display, artifacts, screen) as (server_pid, cookie), ExitStack() as stack:
        record["xvfb_pid"] = server_pid
        inspected = stack if post else None
        sandbox = stack.enter_context(Sandbox(artifacts, target, display=options.display))
        Path(sandbox.env["XAUTHORITY"]).write_bytes(cookie)
        source = sandbox.fixtures / "source.txt"
        record["assertion_id"] = "durable-source"
        initial = durable_source(source)
        (artifacts / "source.initial").write_bytes(initial)
        expect_exact(initial, FILE_TEXT)

        driver = stack.enter_context(XDriver(BUILD / "xdriver", options.display, env=sandbox.env,
                                             timeout=2 * WATCHDOG))
        record["assertion_id"] = "initial-launch"
        phase = "setup" if untitled else "original"
        child, window, inspector = launch(artifacts, phase, sandbox.start, (str(source),),
                                          sandbox, driver, record, inspected)
        record["assertion_id"] = "initial-file-tab"
        expect_exact(copied_text(driver, window), FILE_TEXT)

        if untitled:
            record["assertion_id"] = "untitled-durable-seed"
            driver.chord("Control_L", "n")
            for char in UNTITLED_TEXT.decode("ascii"):
                driver.key(char)
            expect_exact(copied_text(driver, window), before)
            # Establish the durable baseline before the scored edit exists.
            driver.chord("Control_L", "q")
            status = wait_exit(child)
            record["children"]["setup"]["returncode"] = status
            if status != 0:
                raise Refused(f"untitled seed hot exit returned {status}")
            if post:
                allocation_count(artifacts / "setup.log", record, "setup")
            child, window, inspector = launch(artifacts, "original", sandbox.restart, (),
                                              sandbox, driver, record, inspected)
            expect_exact(copied_text(driver, window), before)

        record["assertion_id"] = "paste-and-sigkill"
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
            record["assertion_id"] = "submitted-action-frame"
            acknowledged(inspector, request, packet, marker, 1, record, "witness")

        record["assertion_id"] = "source-after-crash"
        expect_exact(source_bytes(source, artifacts / "source.after-crash"), FILE_TEXT)

        record["assertion_id"] = "same-root-restart"
        # Opening by path activates an existing recovered file buffer.
        # It also handles a clean tab omitted from the crashed session.
        args = () if untitled else (str(source),)
        restarted, window, inspector = launch(artifacts, "restarted", sandbox.restart, args,
                                      sandbox, driver, record, inspected)
        if restarted.pid == child.pid:
            raise Refused("restarted editor reused the killed editor's PID")
        record["assertion_id"] = "recovered-buffer"
        observed = copied_text(driver, window)
        (artifacts / "recovered.txt").write_bytes(observed)
        if options.wrong_expectation:
            before = b"intentionally wrong starting bytes"
            whole = before + marker
        if post:
            expect_exact(observed, whole)
            record["outcome"] = "whole-edit"
        else:
            record["outcome"] = compare_recovery(observed, before, whole)

        record["assertion_id"] = "source-after-restart"
        expect_exact(source_bytes(source, artifacts / "source.after-restart"), FILE_TEXT)
        if restarted.poll() is not None:
            record["children"]["restarted"]["returncode"] = restarted.returncode
            raise Refused(f"restarted editor exited during observation: {restarted.returncode}")
        if post:
            # SIGKILL discards the scored window's count. Repeat the same paste in the
            # restarted editor and count that window at a clean close (count rule).
            record["assertion_id"] = "law2-paste-twin"
            request = paste(driver, inspector, marker)
            acknowledged(inspector, request, inspector.packet(), marker, 2, record, "twin")
            # v1's signalfd turns SIGTERM into an ordinary close, which prints the count.
            status = stop(restarted, signal.SIGTERM)
            record["children"]["restarted"]["returncode"] = status
            if status != 0:
                raise Refused(f"restarted editor clean close returned {status}")
            allocation_count(artifacts / "restarted.log", record, "restarted")


def run_case(case, options):
    artifacts = Path(tempfile.mkdtemp(prefix="recovery-", dir=BUILD))
    record = {"case_id": f"R31.{case}", "assertion_id": "setup", "run_id": artifacts.name,
              "evidence_kind": "synthetic" if case == "torn-prefix" else "behavioral",
              "first_required_wave": "W0", "artifacts": str(artifacts), "display": options.display,
              "children": {}, "law2": {}, "wrong_expectation": options.wrong_expectation,
              "decoy_paste": options.decoy_paste}
    try:
        battery = Path("/sys/class/power_supply/BAT0/status")
        record["power"] = battery.read_text().strip() if battery.exists() else "BAT0 unavailable"
        record["loadavg"] = Path("/proc/loadavg").read_text().strip()
        if case == "torn-prefix":
            record["assertion_id"] = "recovered-buffer"
            marker = b" [whole paste]"
            compare_recovery(FILE_TEXT + marker[:len(marker) // 2], FILE_TEXT, FILE_TEXT + marker)
        else:
            recovery(options, artifacts, record)
    except Mismatch as exc:
        record.update(result="FAIL", detail=str(exc),
                      observed_hex=hexed(exc.observed),
                      expected_hex=[hexed(value) for value in exc.expected])
    except (OSError, Refused, XDriverError, wire.ProtocolError, subprocess.SubprocessError) as exc:
        record.update(result="ERROR", detail=f"{type(exc).__name__}: {exc}")
    else:
        record.update(result="PASS", assertion_id="all")

    receipt = json.dumps(record, sort_keys=True)
    (artifacts / "result.json").write_text(receipt + "\n", encoding="utf-8")
    print(receipt, flush=True)
    if record["result"] != "PASS":
        print(f"{record['case_id']} {record['result']} {record['assertion_id']} "
              f"{record['detail']}", file=sys.stderr, flush=True)
    return EXIT[record["result"]]


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
