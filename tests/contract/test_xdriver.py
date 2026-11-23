"""Run X11 driver conformance against an authenticated, privately owned Xvfb."""

import contextlib
import os
from pathlib import Path
import secrets
import selectors
import socket
import struct
import subprocess
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import records  # noqa: E402
from sandbox import CONTRACT_DISPLAY  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
DRIVER = BUILD / "xdriver"
CLIENT = BUILD / "xdriver-test-client"
# See docs/decisions/v2-he7.9.md for the fixture watchdog derivation.
# The shorter guard intentionally expires the broken INCR.
WATCHDOG = 15
TRANSFER_MS = WATCHDOG * 1000
PARTIAL_MS = 500


def readable(fd, timeout=WATCHDOG):
    with selectors.DefaultSelector() as selector:
        selector.register(fd, selectors.EVENT_READ)
        if not selector.select(timeout):
            raise AssertionError("timed out waiting for Xvfb or fixture output")


def available(number):
    path = f"/tmp/.X11-unix/X{number}"
    if os.path.lexists(path) or os.path.lexists(f"/tmp/.X{number}-lock"):
        return False
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        try:
            probe.bind("\0" + path)
        except OSError:
            return False
    return True


def stop(child):
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=WATCHDOG)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=WATCHDOG)


def setUpModule():
    global DISPLAY, ENV
    # Without a request, probe the decade the driver bead reserves; the runner passes one.
    requested = os.environ.get("XDRIVER_TEST_DISPLAY")
    if requested is not None and not CONTRACT_DISPLAY.fullmatch(requested):
        raise AssertionError("XDRIVER_TEST_DISPLAY must be a contract display, :120 through :199")
    candidates = [int(requested[1:])] if requested else range(120, 130)
    number = next((candidate for candidate in candidates if available(candidate)), None)
    if number is None:
        raise AssertionError("no available private Xvfb display in :120 through :129")
    DISPLAY = f":{number}"
    BUILD.mkdir(exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="xdriver-test-", dir=BUILD))
    auth = scratch / "authority"
    # Xauthority network-order FamilyLocal record with a fresh 128-bit MIT cookie.
    fields = (os.fsencode(socket.gethostname()), str(number).encode(),
              b"MIT-MAGIC-COOKIE-1", secrets.token_bytes(16))
    record = struct.pack("!H", 256)
    record += b"".join(struct.pack("!H", len(field)) + field for field in fields)
    fd = os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(record)
    ENV = {name: os.environ[name] for name in ("PATH",) if name in os.environ}
    ENV["XAUTHORITY"] = str(auth)
    with contextlib.ExitStack() as stack:
        read_fd, write_fd = os.pipe()
        stack.callback(os.close, read_fd)
        with os.fdopen(write_fd, "wb") as ready, (scratch / "Xvfb.log").open("wb") as log:
            server = subprocess.Popen(
                ["Xvfb", DISPLAY, "-displayfd", str(ready.fileno()), "-noreset",
                 "-nolisten", "tcp", "-auth", str(auth)],
                env=ENV, pass_fds=(ready.fileno(),), stderr=log, stdout=log,
            )
        unittest.addModuleCleanup(stop, server)
        with os.fdopen(os.dup(read_fd), "rb") as ready:
            readable(ready)
            response = ready.readline()
        if response != f"{number}\n".encode() or server.poll() is not None:
            raise AssertionError(f"Xvfb did not claim {DISPLAY}: {response!r}; {scratch}/Xvfb.log")
    print(f"Xvfb {DISPLAY}, owned PID {server.pid}", flush=True)


class DriverTests(unittest.TestCase):
    def setUp(self):
        from xdriver import XDriver, XDriverError
        self.driver = XDriver(DRIVER, DISPLAY, env=ENV, timeout=WATCHDOG * 2)
        self.addCleanup(self.driver.close)
        self.error = XDriverError

    def fixture(self, *args):
        child = subprocess.Popen(
            [CLIENT, DISPLAY, *args], env=ENV,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, bufsize=0,
        )
        self.addCleanup(child.stdout.close)
        self.addCleanup(stop, child)
        return child

    def line(self, child):
        readable(child.stdout)
        line = child.stdout.readline()
        self.assertTrue(line.endswith(b"\n"), f"incomplete fixture line: {line!r}")
        return line[:-1].split()

    def ready(self, child):
        fields = self.line(child)
        self.assertEqual(fields[0], b"READY")
        return tuple(map(int, fields[1:]))

    def bind(self, child, window):
        self.assertEqual(self.driver.find(child.pid, TRANSFER_MS), window)

    def test_input_requires_verified_focus(self):
        client = self.fixture("window")
        window, *_ = self.ready(client)
        self.bind(client, window)
        # Actual server focus alone cannot authorize input from this driver.
        for command in (lambda: self.driver.key("a"), lambda: self.driver.chord("Control_L", "a"),
                        lambda: self.driver.motion(24, 30), lambda: self.driver.button(1)):
            with self.subTest(command=command), self.assertRaisesRegex(self.error, "^FOCUS_REQUIRED$"):
                command()
        self.driver.focus()
        other = self.fixture("window")
        self.ready(other)
        with self.assertRaisesRegex(self.error, "^FOCUS_LOST$"):
            self.driver.key("a")

    def test_keys_mouse_and_marker_arrive_in_order(self):
        client = self.fixture("window")
        window, a, enter, shift, control, digit = self.ready(client)
        self.bind(client, window)
        self.driver.focus()
        self.driver.key("a")
        self.driver.key(0xFF0D)  # X11 Return keysym, test numeric form.
        self.driver.key("A")
        self.driver.chord("Control_L", "a")
        self.driver.key("1")
        self.driver.motion(24, 30)
        self.driver.button(1)
        words = (1, 1, 0x89ABCDEF, 0xFFFFFFFF, 0)
        self.driver.marker(*words)
        events = []
        while True:
            line = self.line(client)
            events.append((line[0].decode(), *(int(field) for field in line[1:])))
            if line[0] == b"marker":
                break
        self.assertEqual(events, [
            ("down", a, 0), ("up", a, 0),
            ("down", enter, 0), ("up", enter, 0),
            ("down", shift, 0), ("down", a, 1), ("up", a, 1), ("up", shift, 1),
            ("down", control, 0), ("down", a, 4), ("up", a, 4), ("up", control, 4),
            ("down", digit, 0), ("up", digit, 0),
            ("motion", 24, 30), ("button-down", 1), ("button-up", 1),
            ("marker", *words),
        ])

    def test_stale_owner_is_rejected(self):
        original = self.fixture("owner", "CLIPBOARD", "normal")
        expected, = self.ready(original)
        replacement = self.fixture("owner", "CLIPBOARD", "normal")
        actual, = self.ready(replacement)
        self.assertNotEqual(expected, actual)
        self.assertEqual(self.driver.owner("CLIPBOARD"), actual)
        with self.assertRaisesRegex(self.error, "^STALE_OWNER$"):
            self.driver.read("CLIPBOARD", expected, TRANSFER_MS)

    def test_incomplete_incr_is_rejected(self):
        owner = self.fixture("owner", "CLIPBOARD", "partial")
        window, = self.ready(owner)
        with self.assertRaisesRegex(self.error, "^INCOMPLETE_INCR$"):
            self.driver.read("CLIPBOARD", window, PARTIAL_MS)
        self.assertEqual(self.line(owner), [b"sent-partial"])
        self.assertEqual(self.driver.owner("CLIPBOARD"), window)

    def test_receives_bytes_from_independent_owner(self):
        owner = self.fixture("owner", "PRIMARY", "normal")
        window, = self.ready(owner)
        self.assertEqual(self.driver.read("PRIMARY", window, TRANSFER_MS), b"\x00\xff\n \xc3\xa9\x00")

    def test_serves_exact_bytes_to_independent_reader(self):
        for selection, payload in (("CLIPBOARD", bytes(range(256)) * 2),
                                   ("PRIMARY", b"")):
            with self.subTest(selection=selection):
                window = self.driver.own(selection, payload)
                self.assertEqual(self.driver.owner(selection), window)
                reader = self.fixture("read", selection, str(len(payload)))
                received, chunks = self.ready(reader)
                self.assertEqual(received, len(payload))
                self.assertEqual(chunks, 0)
                self.assertEqual(reader.wait(timeout=WATCHDOG), 0)

    def test_incr_larger_than_server_maximum_both_directions(self):
        # One byte beyond the X server's advertised maximum request size.
        size = self.driver.max_request_bytes + 1
        payload = (bytes(range(256)) * ((size + 255) // 256))[:size]
        window = self.driver.own("CLIPBOARD", payload)
        reader = self.fixture("read", "CLIPBOARD", str(size))
        self.assertEqual(self.ready(reader), (size, 2))  # Two chunks cross the request limit.
        self.assertEqual(reader.wait(timeout=WATCHDOG), 0)
        with type(self.driver)(DRIVER, DISPLAY, env=ENV, timeout=WATCHDOG * 2) as other:
            self.assertEqual(other.max_request_bytes, self.driver.max_request_bytes)
            self.assertEqual(other.read("CLIPBOARD", window, TRANSFER_MS), payload)


class ProtocolTests(unittest.TestCase):
    def test_rejects_malformed_missing_and_extra_replies(self):
        from xdriver import XDriver, XDriverProtocolError
        for reply in ("OK 0 extra\n", "", "OK 17", "OK 17\nOK 17\n"):
            with self.subTest(reply=reply):
                env = dict(ENV, XDRIVER_TEST_REPLY=reply)
                with XDriver(CLIENT, DISPLAY, env=env, timeout=WATCHDOG) as driver:
                    with self.assertRaises(XDriverProtocolError):
                        driver.owner("CLIPBOARD")


if __name__ == "__main__":
    raise SystemExit(records.unittest_main("XDRIVER"))
