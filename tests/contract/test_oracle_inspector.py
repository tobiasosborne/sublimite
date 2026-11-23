"""Exercise the pinned v1 adapter on an exclusively owned X11 display."""

import argparse
from contextlib import ExitStack, contextmanager
import ctypes
from dataclasses import replace
import fcntl
import json
import mmap
import os
from pathlib import Path
import re
import secrets
import select
import selectors
import socket
import struct
import subprocess
import tempfile
import time
import unittest

from build_oracle import EDITOR, FAULT, PATCH, STAMP, STOCK, manifest, sha256
import records
from sandbox import CONTRACT_DISPLAY, Refused, RunTarget, Sandbox
import snapwire as wire
from test_xdriver import WATCHDOG, available, readable, stop
from xdriver import XDriver, XDriverError

ROOT = Path(__file__).resolve().parents[2]
# The v1 tab strip uses 16 cells per tab. This small screen leaves a partial
# slot at the right edge, with room for document text and several tab titles.
WIDTH, HEIGHT, DEPTH = 640, 480, 24
# One settled frame, at most one overscan row, one clipped tab per column,
# and at most the UTF-8 replacement scalar per cell. One pixel per cell is the lower bound.
CAPACITY = (2 * wire.WORD.size + wire.FRAME_SIZES.size + 3 * wire.RECT.size
            + wire.SCROLL.size + wire.FRAME_TAIL.size + wire.WORD.size
            + WIDTH * (HEIGHT + 1) * (wire.CELL_HEADER.size + len("�".encode()))
            + WIDTH * (wire.RECT.size + wire.WORD.size))
GUARD = re.compile(rb"oracle law2: active=(\d+) allocations=(\d+) scopes=(\d+)")
IN_MOVED_TO = 0x00000080
IN_Q_OVERFLOW = 0x00004000
IN_IGNORED = 0x00008000
INOTIFY_EVENT = struct.Struct("=iIII")


@contextmanager
def display(number, scratch):
    if not CONTRACT_DISPLAY.fullmatch(number) or not available(int(number[1:])):
        raise ValueError("oracle requires a free contract display, :120 through :199")
    auth = scratch / "authority"
    fields = (os.fsencode(socket.gethostname()), number[1:].encode(),
              b"MIT-MAGIC-COOKIE-1", secrets.token_bytes(16))
    record = struct.pack("!H", 256)  # Xauthority FamilyLocal
    record += b"".join(struct.pack("!H", len(field)) + field for field in fields)
    fd = os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(record)
    with ExitStack() as stack:
        rfd, wfd = os.pipe()
        with os.fdopen(wfd, "wb") as ready, (scratch / "Xvfb.log").open("wb") as log:
            server = subprocess.Popen(
                ["Xvfb", number, "-screen", "0", f"{WIDTH}x{HEIGHT}x{DEPTH}",
                 "-displayfd", str(ready.fileno()), "-noreset", "-nolisten", "tcp",
                 "-auth", str(auth)], pass_fds=(ready.fileno(),), stdout=log, stderr=log,
            )
        stack.callback(stop, server)
        with os.fdopen(rfd, "rb") as ready:
            readable(ready)
            if ready.readline() != number[1:].encode() + b"\n" or server.poll() is not None:
                raise RuntimeError(f"Xvfb did not start. See {scratch / 'Xvfb.log'}")
        print(f"Xvfb {number}, owned PID {server.pid}", flush=True)
        yield record


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


def next_event(control, deadline, ignore=b"P"):
    """The first fault-control byte other than `ignore`; one deadline for the whole wait."""
    while True:
        readable(control, deadline - time.monotonic())
        event = control.recv(1)
        if event != ignore:
            return event


def target(path):
    digest = DIGESTS[path.name] if DIGESTS else sha256(path)
    return RunTarget("v1", path, path.parent, digest)


class Inspector:
    def __init__(self, stack, capacity=CAPACITY):
        self.fd = os.memfd_create("oracle-cells", os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        stack.callback(os.close, self.fd)
        os.ftruncate(self.fd, capacity)
        fcntl.fcntl(self.fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK)
        self.mapping = stack.enter_context(mmap.mmap(self.fd, capacity, access=mmap.ACCESS_READ))
        self.reader, self.writer = socket.socketpair(socket.AF_UNIX,
                                                     socket.SOCK_SEQPACKET | socket.SOCK_NONBLOCK)
        stack.enter_context(self.reader)
        stack.enter_context(self.writer)
        self.ticket = self.serial = 0

    @property
    def args(self):
        return [f"--inspect-fds={self.fd},{self.writer.fileno()}"]

    @property
    def fds(self):
        return self.fd, self.writer.fileno()

    def send(self, driver, operation=wire.Operation.SNAP, previous=0):
        self.ticket += 1
        request = wire.Request(operation, self.ticket, previous)
        driver.marker(*wire.encode_request(request))
        return request

    def packet(self):
        readable(self.reader)
        packet, ancillary, flags, _ = self.reader.recvmsg(wire.PACKET.size)
        if flags or ancillary:
            raise wire.ProtocolError("truncated reply or unexpected ancillary data")
        return packet

    def receive(self, request):
        reply = wire.decode_reply(self.packet(), self.mapping, request, after_serial=self.serial)
        self.serial = reply.submit_serial
        return reply.snapshot

    def snap(self, driver):
        return self.receive(self.send(driver)).settled


class OracleTests(unittest.TestCase):
    @contextmanager
    def launch(self, *, inspect=True, files=("fixture.txt",), capacity=CAPACITY,
               executable=None, control=None):
        with ExitStack() as stack:
            box = stack.enter_context(Sandbox(SCRATCH, target(executable or ARGS.editor),
                                              display=ARGS.display))
            (box.root / "Xauthority").write_bytes(AUTH)
            paths = [box.fixtures / name for name in files]
            for path in paths:
                path.write_bytes(b"")
            inspector = Inspector(stack, capacity) if inspect else None
            args = inspector.args if inspector else []
            fds = inspector.fds if inspector else ()
            if control:
                fd = control.fileno()
                args.insert(0, str(fd))
                fds += (fd,)
            child = box.start([*args, *map(str, paths)], pass_fds=fds, stderr=subprocess.PIPE)
            if control:
                control.close()
            if inspector:
                inspector.writer.close()
            driver = stack.enter_context(XDriver(ARGS.driver, ARGS.display, env=box.env,
                                                timeout=2 * WATCHDOG))
            try:
                window = driver.find(child.pid, WATCHDOG * 1000)
            except XDriverError:
                if child.poll() is not None:
                    self.fail(f"editor exited {child.returncode}: {child.stderr.read().decode()}")
                raise
            driver.focus()
            yield child, driver, window, inspector, paths

    def finish(self, child, *, guarded, typed=False, allocations=0):
        child.terminate()  # v1's signalfd requests an ordinary clean close.
        _, stderr = child.communicate(timeout=WATCHDOG)
        self.assertEqual(child.returncode, 0, stderr.decode())
        if guarded:
            self.assertRegex(stderr, GUARD)
            report = GUARD.search(stderr)
            print(report.group().decode(), flush=True)
            active, count, scopes = map(int, report.groups())
            self.assertEqual((active, count), (1, allocations), stderr.decode(errors="replace"))
            if typed:
                self.assertGreater(scopes, 0, "no typed frame was measured")

    def assert_editor_alive(self, child, action):
        if child.poll() is not None:
            stderr = child.stderr.read().decode(errors="replace")
            self.fail(f"{child.args[0]} exited {child.returncode} before {action}\n{stderr}")

    def test_idle(self):
        with self.launch() as (child, driver, _, inspector, _paths):
            snapshot = inspector.receive(inspector.send(driver))
            self.assertEqual(len(snapshot.frames), 1)
            with self.assertRaises(wire.MissingWitnessError):
                _ = snapshot.first_action
            frame = snapshot.settled
            self.assertGreater(frame.document.height, 0)
            self.assertTrue(frame.tabs)
            self.assertEqual(frame.provided_sections,
                             wire.Section.CELLS | wire.Section.TABS | wire.Section.BOTTOM_ROW)
            with self.assertRaises(wire.UnprovidedSectionError):
                _ = frame.minimap_box
            self.finish(child, guarded=True)

    def assert_line(self, frame, text):
        doc = frame.document
        self.assertEqual(frame.cell_text(doc.y, doc.x, doc.x + len(text)), text)

    def test_typed_snap_next_and_stale_serial(self):
        with self.launch() as (child, driver, _, inspector, _paths):
            idle = inspector.snap(driver)
            text = "oracle"
            for char in text:
                driver.key(char)
            request = inspector.send(driver)
            packet = inspector.packet()
            typed = wire.decode_reply(packet, inspector.mapping, request,
                                      after_serial=idle.submit_serial)
            self.assert_line(typed.snapshot.settled, text)
            # Feed the same previously accepted transaction to the live decoder.
            with self.assertRaisesRegex(wire.ProtocolError, "serial is stale"):
                wire.decode_reply(packet, inspector.mapping, request, after_serial=typed.submit_serial)
            inspector.serial = typed.submit_serial
            driver.key("x")
            later = inspector.receive(inspector.send(driver, wire.Operation.NEXT, inspector.serial))
            self.assert_line(later.settled, text + "x")
            with self.assertRaises(wire.ReplyError) as error:
                inspector.receive(inspector.send(driver, wire.Operation.NEXT, wire.U32_MAX))
            self.assertEqual(error.exception.result, wire.Result.ORDER)
            self.finish(child, guarded=True, typed=True)

    def test_arm_invalid_and_small_mapping(self):
        with self.launch(capacity=wire.WORD.size) as (child, driver, _, inspector, _paths):
            with self.assertRaises(wire.ReplyError) as error:
                inspector.receive(inspector.send(driver, wire.Operation.ARM))
            self.assertEqual(error.exception.result, wire.Result.INVALID)
            with self.assertRaises(wire.SizeError):
                inspector.snap(driver)
            self.finish(child, guarded=True)

    def test_clipped_tab(self):
        files = tuple("abcdef")  # enough one-cell titles to overflow the fixture strip
        with self.launch(files=files) as (child, driver, _, inspector, _paths):
            inspector.snap(driver)  # initial command-line opens are foreground work
            driver.chord("Alt_L", "1")  # pin the strip to the first tab
            frame = inspector.snap(driver)
            first, last = frame.tabs[0], frame.tabs[-1]
            self.assertTrue(first.active)
            self.assertGreater(last.rect.x, first.rect.x)
            self.assertLess(last.rect.width, first.rect.width)
            self.assertEqual(last.rect.x + last.rect.width, frame.columns - 1)
            titles = tuple(title.strip() for title in frame.visible_tab_titles())
            self.assertEqual(titles, files[:len(titles)])
            # Restoring this edge tab to full width exposes out-of-grid cells.
            full = replace(last, rect=replace(last.rect, width=first.rect.width))
            with self.assertRaisesRegex(wire.ProtocolError, "tab rectangle"):
                wire.encode_payload(wire.Snapshot((replace(frame, _tabs=(*frame.tabs[:-1], full)),)))
            self.finish(child, guarded=True)

    def test_noop_and_dropped_submit(self):
        control, endpoint = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_NONBLOCK)
        with control, endpoint, self.launch(executable=ARGS.fault, control=endpoint) as (
                child, driver, _, inspector, _paths):
            baseline = inspector.snap(driver)
            # The first request can use the still-in-flight startup damage.
            if ready_now(control):
                self.assertEqual(control.recv(1), b"P")
            driver.key("Shift_L")
            noop = inspector.snap(driver)
            readable(control)
            self.assertEqual(control.recv(1), b"P")
            self.assertFalse(ready_now(control), "no-op requested multiple repaints")
            self.assertEqual(noop.cell_text(noop.document.y, noop.document.x),
                             baseline.cell_text(baseline.document.y, baseline.document.x))

            control.sendall(b"D")
            request = inspector.send(driver)
            # Repaints may precede the drop; a stream of them until expiry is ERROR.
            self.assertEqual(next_event(control, time.monotonic() + WATCHDOG), b"D")
            self.assertFalse(ready_now(inspector.reader), "dropped submit completed SNAP")
            control.sendall(b"R")
            driver.key("x")
            snapshot = inspector.receive(request)
            self.assertGreater(snapshot.settled.submit_serial, noop.submit_serial)
            self.finish(child, guarded=True)

    def test_paste_fenced(self):
        with self.launch() as (child, driver, _, inspector, _paths):
            inspector.snap(driver)
            text = b"pasted ascii"
            driver.own("CLIPBOARD", text)
            driver.chord("Control_L", "v")
            self.assert_line(inspector.snap(driver), text.decode())
            self.finish(child, guarded=True)

    def test_allocation_count_does_not_stop_the_editor(self):
        control, endpoint = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_NONBLOCK)
        with control, endpoint, self.launch(executable=ARGS.fault, control=endpoint) as (
                child, driver, _, inspector, _paths):
            inspector.snap(driver)
            control.sendall(b"A")
            driver.key("x")
            self.assert_line(inspector.snap(driver), "x")
            driver.key("y")
            self.assert_line(inspector.snap(driver), "xy")
            self.finish(child, guarded=True, typed=True, allocations=1)

    def disk_clipboard(self, child, driver, window, path):
        """Save then copy; each wait ends on its own event, and expiry is ERROR."""
        text = b"disk and clipboard"
        sentinel = driver.own("CLIPBOARD", b"prior owner")
        for char in text.decode():
            driver.key("space" if char == " " else char)
        # The destination rename is armed before Ctrl+S; its first committed bytes are scored.
        with directory_watch(path.parent, IN_MOVED_TO) as watch:
            driver.chord("Control_L", "s")
            wait_for_file_event(watch, path, IN_MOVED_TO, child)
        saved = path.read_bytes()
        driver.chord("Control_L", "a")
        driver.chord("Control_L", "c")
        deadline = time.monotonic() + WATCHDOG
        while (owner := driver.owner("CLIPBOARD")) != window:
            if owner not in (0, sentinel):
                raise RuntimeError(f"unexpected clipboard owner {owner}, expected window {window}")
            self.assert_editor_alive(child, "taking clipboard ownership")
            if time.monotonic() >= deadline:
                raise TimeoutError("editor did not claim CLIPBOARD after Select All and Copy")
            time.sleep(0.01)  # observe ownership without reissuing Copy (cases/recovery.py)
        result = saved, driver.read("CLIPBOARD", window, WATCHDOG * 1000)
        self.assertEqual(result, (text, text))
        return result

    def test_disconnect_and_stock_disk_clipboard(self):
        inspected = []
        for close_before_marker in (True, False):
            with self.launch() as (child, driver, window, inspector, paths):
                inspector.snap(driver)
                if close_before_marker:
                    inspector.reader.close()
                else:
                    # The editor can still peek, but its reply send gets EPIPE.
                    inspector.reader.shutdown(socket.SHUT_RD)
                inspector.send(driver)
                if not close_before_marker:
                    poll = select.poll()
                    poll.register(inspector.reader, select.POLLHUP)
                    events = poll.poll(WATCHDOG * 1000)
                    if not events:
                        raise TimeoutError("inspector did not close after reply send failed")
                    self.assertTrue(events[0][1] & select.POLLHUP, f"poll events {events}")
                    inspector.reader.close()
                self.assert_editor_alive(child, "editing after inspection disconnected")
                inspected.append(self.disk_clipboard(child, driver, window, paths[0]))
                self.finish(child, guarded=True, typed=True)
        for binary in (ARGS.stock, ARGS.editor):
            with self.launch(inspect=False, executable=binary) as (child, driver, window, _, paths):
                control = self.disk_clipboard(child, driver, window, paths[0])
                self.assertEqual(inspected, [control, control])
                self.finish(child, guarded=False)

    def test_unpatched_refusal(self):
        with ExitStack() as stack:
            box = stack.enter_context(Sandbox(SCRATCH, target(ARGS.stock or ARGS.editor),
                                              display=ARGS.display))
            (box.root / "Xauthority").write_bytes(AUTH)
            inspection = Inspector(stack)
            child = box.start(inspection.args, pass_fds=inspection.fds, stderr=subprocess.PIPE)
            inspection.writer.close()
            _, stderr = child.communicate(timeout=WATCHDOG)
            self.assertEqual(child.returncode, 2, stderr.decode())
            self.assertIn(b"usage: sublimite", stderr)
            self.assertNotIn(b"--inspect-fds", stderr)
            print(f"stock v1 refused --inspect-fds (exit {child.returncode})\n"
                  f"{stderr.decode().strip()}", flush=True)


def ready_now(fd):
    with selectors.DefaultSelector() as selector:
        selector.register(fd, selectors.EVENT_READ)
        return bool(selector.select(0))


def main():
    global ARGS, AUTH, DIGESTS, SCRATCH
    parser = argparse.ArgumentParser(description=__doc__)
    editor = parser.add_mutually_exclusive_group(required=True)
    editor.add_argument("--editor", type=lambda path: Path(path).absolute())
    editor.add_argument("--build-dir", type=lambda path: Path(path).absolute())
    parser.add_argument("--stock", type=lambda path: Path(path).absolute())
    parser.add_argument("--fault", type=lambda path: Path(path).absolute())
    parser.add_argument("--driver", default=ROOT / "build/xdriver", type=Path)
    parser.add_argument("--display", required=True)
    parser.add_argument("--expect-unpatched", action="store_true")
    parser.add_argument("tests", nargs="*")
    ARGS = parser.parse_args()
    DIGESTS = {}
    if ARGS.build_dir:
        recorded = json.loads((ARGS.build_dir / STAMP).read_text())
        if recorded != manifest(ARGS.build_dir, sha256(PATCH)):
            parser.error("oracle build manifest differs from current patch or executables")
        DIGESTS = recorded["executables"]
        ARGS.editor = ARGS.build_dir / EDITOR
        ARGS.stock = ARGS.build_dir / STOCK
        ARGS.fault = ARGS.build_dir / FAULT
    if not (ARGS.tests or ARGS.expect_unpatched or (ARGS.stock and ARGS.fault)):
        parser.error("full suite requires the stock and fault control executables")
    root = ROOT / "build"
    root.mkdir(exist_ok=True)
    SCRATCH = Path(tempfile.mkdtemp(prefix="oracle-test-", dir=root))
    with display(ARGS.display, SCRATCH) as AUTH:
        tests = ARGS.tests or (["test_unpatched_refusal"] if ARGS.expect_unpatched else
                              unittest.defaultTestLoader.getTestCaseNames(OracleTests))
        suite = unittest.TestSuite(OracleTests(name) for name in tests)
        return records.run_suite(suite, "ADAPTER")


if __name__ == "__main__":
    raise SystemExit(main())
