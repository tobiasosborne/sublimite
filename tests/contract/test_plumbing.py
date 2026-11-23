"""Self-test for the plumbing every inspected case shares (docs/decisions/v2-he7.33.md).

XServerTests: tests/contract/xserver.py, the one private X server owner (findings 9, 19).

Stand-in servers (tests/contract/xserver.py's `command` seam) answer -displayfd wrongly,
short, split or never, exit early or ignore SIGTERM; real Xvfb serves an authenticated client
at each geometry, and two successive servers on one display share no client history (the
advisor's R6 ruling). The TERM-ignoring wrapper also runs the X driver conformance suite
end to end: its tests pass, its invocation must still exit 4.

TransportTests and CountTests: the one snapshot receive path (Inspector.packet) and the one
Law-2 count parser (law2_count) in tests/contract/test_oracle_inspector.py (finding 21).

BindingTests: every inspected case launches only the bytes the runner selected (--digests):
an editor replaced after selection, with a matching new build stamp, is ERROR before any
launch, so before any input (findings 2 and 20).

Usage: python3 -B tests/contract/test_plumbing.py --display :1NN (a free contract display).
"""

import argparse
import array
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import os
import socket
import stat
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import records  # noqa: E402
from sandbox import Refused  # noqa: E402
import snapwire as wire  # noqa: E402
from test_oracle_inspector import Inspector, law2_count  # noqa: E402
import xserver  # noqa: E402

BUILD = HERE.parents[1] / "build"
DISPLAY = None
STANDIN = r'''import os, signal, subprocess, sys, time
mode, display = sys.argv[1], sys.argv[2]
fd = int(sys.argv[sys.argv.index("-displayfd") + 1])
number = display[1:].encode()
if mode == "wrap":  # real Xvfb under a parent that ignores SIGTERM. When the owner kills
    import ctypes    # the parent, PDEATHSIG terms the server, which removes its socket and lock.
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    libc = ctypes.CDLL(None)
    def orphaned():
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        libc.prctl(1, signal.SIGTERM)  # PR_SET_PDEATHSIG
    server = subprocess.Popen(["/usr/bin/Xvfb", *sys.argv[2:]], pass_fds=(fd,), preexec_fn=orphaned)
    os.close(fd)
    sys.exit(server.wait())
answers = {"wrong": [b"199\n"], "short": [number], "split": [number[:1], number[1:], b"\n"],
           "ignore-term": [number + b"\n"], "early-exit": [number + b"\n"], "silent": [],
           "exit-before-answer": []}[mode]
if mode == "ignore-term":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
for part in answers:
    os.write(fd, part)
    time.sleep(0.05)
if mode != "silent":
    os.close(fd)
if mode == "exit-before-answer":
    sys.exit(1)
if mode == "early-exit":
    sys.exit(0)
while True:
    time.sleep(1)
'''


def gone(pid):
    """The stand-in was reaped: its PID names no process, or a different program."""
    try:
        return b"standin" not in Path(f"/proc/{pid}/cmdline").read_bytes()
    except FileNotFoundError:
        return True


class XServerTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="xserver-test-", dir=BUILD))
        self.standin = self.dir / "standin.py"
        self.standin.write_text(STANDIN, encoding="ascii")

    def server(self, mode=None, **kwargs):
        command = ("Xvfb",) if mode is None else (sys.executable, "-I", str(self.standin), mode)
        return xserver.private_server(DISPLAY, self.dir, command=command,
                                      label=f"{mode or 'Xvfb'}-{time.monotonic_ns()}", **kwargs)

    def test_occupied_display_is_refused_before_any_server(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as squatter:
            squatter.bind(f"\0/tmp/.X11-unix/X{DISPLAY[1:]}")
            with self.assertRaisesRegex(xserver.XServerError, "is occupied"):
                with self.server():
                    pass
        self.assertEqual(sorted(p.suffix for p in self.dir.iterdir()), [".py"])
        for display in (":0", ":119", ":200", "120"):
            with self.subTest(display=display), self.assertRaises(xserver.XServerError):
                with xserver.private_server(display, self.dir):
                    pass

    def test_displayfd_answers(self):
        for mode, error in (("wrong", xserver.XServerError), ("short", xserver.XServerError),
                            ("exit-before-answer", xserver.XServerError),
                            ("silent", TimeoutError)):
            with self.subTest(mode), self.assertRaises(error):
                with self.server(mode):
                    pass
        with self.subTest("split"), self.server("split") as server:
            self.assertFalse(gone(server.pid))
        self.assertTrue(gone(server.pid))

    def test_term_ignored_is_reaped_then_error_keeping_the_first_failure(self):
        first = ValueError("first assertion mismatch")
        with self.assertRaises(xserver.TeardownError) as caught:
            with self.server("ignore-term") as server:
                raise first
        self.assertIs(caught.exception.pending, first)
        self.assertIn("killed and reaped", str(caught.exception))
        self.assertTrue(gone(server.pid))
        with self.assertRaises(xserver.TeardownError) as caught, self.server("ignore-term"):
            pass
        self.assertIsNone(caught.exception.pending)

    def test_early_server_exit_surfaces(self):
        with self.assertRaisesRegex(xserver.TeardownError, "exited before its owner stopped it"):
            with self.server("early-exit") as server:
                pidfd = os.pidfd_open(server.pid)
                try:
                    os.waitid(os.P_PIDFD, pidfd, os.WEXITED | os.WNOWAIT)
                finally:
                    os.close(pidfd)

    def test_authenticated_client_at_each_geometry(self):
        empty = self.dir / "no-cookie"
        empty.write_bytes(b"")
        for geometry, size in ((None, "1280x1024"), ("640x480x24", "640x480"),
                               ("1280x1024x24", "1280x1024")):
            with self.subTest(geometry=geometry), self.server(geometry=geometry) as server:
                self.assertEqual(stat.S_IMODE(server.auth.stat().st_mode), 0o600)
                info = subprocess.run(["xdpyinfo", "-display", DISPLAY], env=server.env,
                                      capture_output=True, text=True, timeout=xserver.READY_S)
                self.assertEqual(info.returncode, 0, info.stderr)
                self.assertRegex(info.stdout, rf"dimensions:\s+{size} pixels")
                refused = subprocess.run(["xdpyinfo", "-display", DISPLAY],
                                         env=dict(server.env, XAUTHORITY=str(empty)),
                                         capture_output=True, timeout=xserver.READY_S)
                self.assertNotEqual(refused.returncode, 0)
        env = {"PATH": os.environ["PATH"], "XAUTHORITY": str(self.dir / "case.Xauthority")}
        with xserver.fresh_display(DISPLAY, self.dir, "fresh", env, timeout=30) as (server, driver):
            self.assertEqual(Path(env["XAUTHORITY"]).read_bytes(), server.cookie)
            self.assertGreater(driver.max_request_bytes, 0)

    def test_successive_servers_share_no_history(self):
        seen = []
        for _ in range(2):
            with self.server() as server:
                xprop = ["xprop", "-display", DISPLAY, "-root"]
                before = subprocess.run([*xprop, "_B6_HISTORY"], env=server.env,
                                        capture_output=True, text=True, timeout=xserver.READY_S)
                self.assertIn("no such atom", before.stdout + before.stderr)
                subprocess.run([*xprop, "-f", "_B6_HISTORY", "8s", "-set", "_B6_HISTORY", "x"],
                               env=server.env, check=True, timeout=xserver.READY_S)
                seen.append((server.pid, server.cookie))
        self.assertNotEqual(seen[0], seen[1])

    def test_term_ignoring_server_leaves_a_passing_suite_at_exit_4(self):
        wrapper = self.dir / "bin" / "Xvfb"
        wrapper.parent.mkdir()
        wrapper.write_text(f"#!{sys.executable} -I\nimport sys\nsys.argv.insert(1, 'wrap')\n"
                           + STANDIN, encoding="ascii")
        wrapper.chmod(0o700)
        env = dict(os.environ, PATH=f"{wrapper.parent}:{os.environ['PATH']}",
                   XDRIVER_TEST_DISPLAY=DISPLAY)
        result = subprocess.run([sys.executable, "-B", str(HERE / "test_xdriver.py")], env=env,
                                stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=600)
        printed = {r["assertion_id"]: r for r in map(records.parse, (
            line for line in result.stdout.splitlines() if line.startswith(records.PREFIX)))}
        self.assertEqual(result.returncode, records.EXIT["ERROR"], result.stdout + result.stderr)
        teardown = printed.pop("tearDownModule")
        self.assertEqual(teardown["status"], "ERROR")
        self.assertRegex(teardown["predicate"], "TeardownError: .*killed and reaped")
        self.assertTrue(printed)
        self.assertEqual({r["status"] for r in printed.values()}, {"PASS"}, printed)


class TransportTests(unittest.TestCase):
    """One policy: a queued reply is read even when the editor exited as well (a case that
    needs a live editor checks it); exit without a reply is Refused; silence TimeoutError."""

    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.inspector = Inspector(self.stack, wire.WORD.size)
        self.send = self.stack.enter_context(self.inspector.writer.dup())

    def child(self, code):
        child = subprocess.Popen([sys.executable, "-I", "-c", code])
        self.addCleanup(xserver.stop, child, xserver.STOP_S, 9)
        return self.inspector.attach(child)

    def test_simultaneous_packet_and_exit_returns_the_packet(self):
        child = self.child("pass")
        reply = b"SCEL" + bytes(wire.PACKET.size - 4)
        self.send.send(reply)
        os.waitid(os.P_PIDFD, self.inspector.pidfd, os.WEXITED | os.WNOWAIT)
        self.assertEqual(self.inspector.packet(), reply)
        self.assertIsNotNone(child.poll())

    def test_missing_packet(self):
        self.inspector.deadline = time.monotonic() + 0.2  # (G) fixture: a live, silent editor
        self.child("import time; time.sleep(60)")
        with self.assertRaisesRegex(TimeoutError, "missing snapshot reply"):
            self.inspector.packet()

    def test_exit_without_a_packet(self):
        self.child("pass")
        os.waitid(os.P_PIDFD, self.inspector.pidfd, os.WEXITED | os.WNOWAIT)
        with self.assertRaisesRegex(Refused, "exited while awaiting"):
            self.inspector.packet()

    def test_malformed_frames(self):
        self.child("import time; time.sleep(60)")
        self.send.send(bytes(wire.PACKET.size + 1))
        with self.assertRaisesRegex(wire.ProtocolError, "truncated"):
            self.inspector.packet()
        with open(os.devnull, "rb") as passed:
            self.send.sendmsg([bytes(wire.PACKET.size)], [(
                socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [passed.fileno()]))])
        with self.assertRaisesRegex(wire.ProtocolError, "ancillary"):
            self.inspector.packet()


class CountTests(unittest.TestCase):
    def test_one_validated_count(self):
        report = b"oracle law2: active=%d allocations=%d scopes=%d\n"
        self.assertEqual(law2_count(b"x\n" + report % (1, 4, 7)), (4, 7))
        self.assertEqual(law2_count(report % (1, 0, 3)).allocations, 0)
        for name, log in (("missing", b"sublimite: bye\n"), ("inactive", report % (0, 0, 3)),
                          ("empty window", report % (1, 0, 0)),
                          ("two reports", report % (1, 0, 3) + report % (1, 4, 3))):
            with self.subTest(name), self.assertRaises(Refused):
                law2_count(log)


class BindingTests(unittest.TestCase):
    def test_replaced_editor_is_refused_before_launch(self):
        directory = Path(tempfile.mkdtemp(prefix="binding-test-", dir=BUILD))
        build = directory / "contract-oracle"
        build.mkdir()
        marker = directory / "launched"
        editor = build / "sublimite"
        editor.write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
        editor.chmod(0o700)
        selected = {str(editor): hashlib.sha256(editor.read_bytes()).hexdigest()}
        # B, with a stamp that names B: a case reading the stamp would launch it.
        editor.write_text(f"#!/bin/sh\necho launched >> {marker}\n", encoding="ascii")
        stamp = {"pin": "6fec2e8c41220e284066ae8479d8f1b573febdb2",
                 "patch_sha256": hashlib.sha256((HERE / "v1-inspector.patch").read_bytes()).hexdigest(),
                 "executables": {name: hashlib.sha256(editor.read_bytes()).hexdigest()
                                 for name in ("sublimite",)}}
        (build / "manifest.json").write_text(json.dumps(stamp), encoding="ascii")
        digests = ["--digests", json.dumps(selected)]
        common = ["--display", DISPLAY, *digests]
        for name, argv in (
                ("navigation", ["cases/navigation.py", "--build-dir", str(build), "--case", "R1"]),
                ("files", ["cases/files.py", "--build-dir", str(build),
                           "--negative", "R3.results-between-a-and-b"]),
                ("recovery", ["cases/recovery.py", "--oracle", str(editor), "--case", "pre-ack-file"]),
                ("capacity", ["cases/capacity.py", "--build-dir", str(build)]),
                ("adapter", ["test_oracle_inspector.py", "--build-dir", str(build), "test_idle"])):
            with self.subTest(name):
                result = subprocess.run([sys.executable, "-B", str(HERE / argv[0]), *argv[1:],
                                         *common], stdin=subprocess.DEVNULL, capture_output=True,
                                        text=True, timeout=300)
                printed = [records.parse(line) for line in result.stdout.splitlines()
                           if line.startswith(records.PREFIX)]
                self.assertEqual(result.returncode, records.EXIT["ERROR"],
                                 result.stdout[-2000:] + result.stderr[-2000:])
                self.assertEqual([r["status"] for r in printed], ["ERROR"], printed)
                self.assertIn("digest mismatch", json.dumps(printed[0]))
                self.assertFalse(marker.exists(), "the replaced editor was launched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--display", required=True)
    args, rest = parser.parse_known_args()
    DISPLAY = args.display
    BUILD.mkdir(exist_ok=True)
    sys.exit(records.unittest_main("PLUMBING", argv=rest))
