"""Run X11 driver conformance against an authenticated, privately owned Xvfb."""

import contextlib
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import records  # noqa: E402
from sandbox import CONTRACT_DISPLAY  # noqa: E402
import xserver  # noqa: E402

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
    """Wait for input; expiry is a harness ERROR (TimeoutError), never an assertion FAIL."""
    with selectors.DefaultSelector() as selector:
        selector.register(fd, selectors.EVENT_READ)
        # A spent deadline is expired even when input is already queued.
        if timeout <= 0 or not selector.select(timeout):
            raise TimeoutError("timed out waiting for Xvfb or fixture output")


def setUpModule():
    global DISPLAY, ENV
    # Without a request, probe the decade the driver bead reserves; the runner passes one.
    requested = os.environ.get("XDRIVER_TEST_DISPLAY")
    candidates = [requested] if requested else [f":{n}" for n in range(120, 130)]
    DISPLAY = next((c for c in candidates if CONTRACT_DISPLAY.fullmatch(c)
                    and xserver.available(int(c[1:]))), None)
    if DISPLAY is None:
        raise AssertionError(f"no free contract display among {candidates}")
    BUILD.mkdir(exist_ok=True)
    owner = contextlib.ExitStack()
    unittest.addModuleCleanup(owner.close)
    ENV = owner.enter_context(xserver.private_server(
        DISPLAY, Path(tempfile.mkdtemp(prefix="xdriver-test-", dir=BUILD)))).env
    print(f"Xvfb {DISPLAY}", flush=True)


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
        self.addCleanup(xserver.stop, child, WATCHDOG)
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
