"""Stub-only process containment tests. No editor or display server is used."""

import hashlib
import json
import os
from dataclasses import replace
from pathlib import Path
import select
import signal
import socket
import stat
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import records
from sandbox import Refused, RunTarget, Sandbox


BUILD = Path(__file__).resolve().parents[2] / "build"
# Stub hang guard (E): many scheduler turns for a handful of local syscalls.
# This is neither a timing assertion nor the oracle run's watchdog.
WATCHDOG = 10
DISPLAY = ":42.1"  # Arbitrary positive local display spelling. Never contacted.

STUB = r'''#!/usr/bin/python3
import json
import os
from pathlib import Path
import socket
import sys

if sys.argv[1:] == ["hold"]:
    print("holding", flush=True)
    sys.stdin.read()
    raise SystemExit
mode, leaf, *args = sys.argv[1:]
record = dict(env=dict(os.environ), cwd=os.getcwd(), argv=args,
              fds=[int(fd) for fd in os.listdir("/proc/self/fd")
                   if os.path.exists("/proc/self/fd/" + fd)])
if mode == "record":
    print(json.dumps(record), flush=True)
    raise SystemExit
rt = os.open(os.environ["XDG_RUNTIME_DIR"], os.O_DIRECTORY)
path = f"/proc/self/fd/{rt}/{leaf}"
app_id = leaf.split(f"-{os.getuid()}", 1)[0]
data = Path(os.environ["XDG_DATA_HOME"]) / app_id
session = data / "session"
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
    if mode == "relay":
        peer.connect(path)
        peer.sendall(b"relay")
        record["reply"] = peer.recv(64).decode()
        print(json.dumps(record), flush=True)
    else:
        lock = Path(path).with_suffix(".lock")
        lock.touch(mode=0o600, exist_ok=True)
        if mode == "restart":
            assert session.read_text() == "primary"
            Path(path).unlink()
        else:
            data.mkdir(mode=0o700, exist_ok=True)
            session.write_text("primary")
        peer.bind(path)
        os.chmod(path, 0o600)
        peer.listen()
        print(json.dumps(record), flush=True)
        while True:
            conn, _ = peer.accept()
            with conn:
                message = conn.recv(64)
                if message in (b"quit", b"quit-stale", b"fail", b"fail-stale"):
                    if message in (b"quit", b"fail"):
                        Path(path).unlink()
                    raise SystemExit(1 if message.startswith(b"fail") else 0)
                if message:
                    conn.sendall(session.read_bytes())
'''


class SandboxTests(unittest.TestCase):
    def setUp(self):
        BUILD.mkdir(exist_ok=True)
        scratch = tempfile.TemporaryDirectory(prefix="contract-test-", dir=BUILD)
        self.addCleanup(scratch.cleanup)
        self.base = Path(scratch.name)
        self.cases = self.base / "cases"
        self.cases.mkdir(mode=0o700)
        self.build = self.base / "approved"
        self.build.mkdir()
        self.exe = self.build / "arbitrary-name"
        self.exe.write_text(STUB)
        self.exe.chmod(0o700)
        self.target = RunTarget("v1", self.exe, self.build, hashlib.sha256(self.exe.read_bytes()).hexdigest())
        self.original = {
            "HOME": str(self.base / "original-home"),
            "XDG_DATA_HOME": str(self.base / "original-data"),
            "XDG_CONFIG_HOME": str(self.base / "original-config"),
            "XDG_RUNTIME_DIR": str(self.base / "original-runtime"),
            "EDIT_IPC_FALLBACK_DIR": str(self.base / "original-fallback"),
        }
        inherited = patch.dict(os.environ, self.original, clear=True)
        inherited.start()
        self.addCleanup(inherited.stop)

    def case(self, *, target=None, display=DISPLAY):
        box = Sandbox(self.cases, target or self.target, display=display)
        self.addCleanup(box.close)
        return box

    def assert_no_launch(self, action):
        with patch("sandbox.subprocess.Popen") as spawn, patch("sandbox.socket.socket") as dial:
            with self.assertRaises(Refused):
                action()
            spawn.assert_not_called()
            dial.assert_not_called()

    def record(self, child):
        self.assertTrue(select.select([child.stdout], [], [], WATCHDOG)[0], "stub never announced readiness")
        line = child.stdout.readline()
        self.assertTrue(line, f"stub exited before readiness: {child.poll()}")
        return json.loads(line)

    def stub(self, box, mode, *args):
        return (mode, box.socket_path.name, *args)

    def start_server(self, box):
        primary = box.start(self.stub(box, "primary"), stdout=subprocess.PIPE)
        self.record(primary)
        box.record_server()
        return primary

    def test_default_or_inherited_locations(self):
        home = Path(self.original["HOME"])
        for key, value in [
            ("HOME", home),
            ("XDG_DATA_HOME", home / ".local/share"),
            ("XDG_DATA_HOME", self.original["XDG_DATA_HOME"]),
            ("XDG_CONFIG_HOME", home / ".config"),
            ("XDG_CONFIG_HOME", self.original["XDG_CONFIG_HOME"]),
            ("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"),
            ("XDG_RUNTIME_DIR", self.original["XDG_RUNTIME_DIR"]),
            ("XDG_RUNTIME_DIR", self.original["EDIT_IPC_FALLBACK_DIR"]),
            ("XDG_STATE_HOME", home / ".local/state"),
            ("XDG_CACHE_HOME", home / ".cache"),
        ]:
            with self.subTest(key=key, value=value), self.case() as box:
                box.env[key] = str(value)
                self.assert_no_launch(box.start)
        self.assertFalse(home.exists())

    def test_relative_unset_and_nonprivate_xdg(self):
        for key in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
            for value in (None, "relative", str(self.base / "unexpected")):
                with self.subTest(key=key, value=value), self.case() as box:
                    if value is None:
                        del box.env[key]
                    else:
                        box.env[key] = value
                    self.assert_no_launch(box.start)

    def test_symlink_escape_and_hardlinked_fixture(self):
        with self.case() as box:
            sentinel = self.base / "sentinel"
            sentinel.write_text("outside")
            link = box.fixtures / "escape"
            link.symlink_to(sentinel)
            self.assert_no_launch(box.start)
            link.unlink()
            os.link(sentinel, link)
            self.assert_no_launch(box.start)
        self.assertEqual(sentinel.read_text(), "outside")

    def test_app_directory_nofollow_and_private_mode(self):
        with self.case() as box:
            app = Path(box.env["XDG_CONFIG_HOME"]) / "sublimite"
            app.symlink_to(self.original["XDG_CONFIG_HOME"], target_is_directory=True)
            self.assert_no_launch(box.start)
            app.unlink()
            app.mkdir()
            app.chmod(0o755)
            self.assert_no_launch(box.start)
            app.chmod(0o700)

    def test_directory_owner_mode_and_unexpected_special_file(self):
        with self.case() as box:
            with patch.object(box, "_uid", os.getuid() + 1):
                self.assert_no_launch(box.start)
            box.root.chmod(0o755)
            self.assert_no_launch(box.start)
            box.root.chmod(0o700)
            os.mkfifo(box.fixtures / "unexpected")
            self.assert_no_launch(box.start)

    def test_foreign_socket_or_lock(self):
        with self.case() as box, socket.socket(socket.AF_UNIX) as foreign:
            runtime = os.open(box.socket_path.parent, os.O_DIRECTORY)
            self.addCleanup(os.close, runtime)
            foreign.bind(f"/proc/self/fd/{runtime}/{box.socket_path.name}")
            foreign.listen()
            self.assert_no_launch(box.start)
            box.socket_path.unlink()
            box.socket_path.with_suffix(".lock").touch(mode=0o600)
            self.assert_no_launch(box.start)

    def test_display_refusals_and_headless(self):
        for display in (":0", ":00.1", "unix/:0", "localhost:42", "", ":" + "9" * 32):
            with self.subTest(display=display):
                self.assert_no_launch(lambda: self.case(display=display).start())
        with patch.dict(os.environ, {"DISPLAY": ":0"}):
            self.assert_no_launch(lambda: self.case(display=None).start())
        with patch.dict(os.environ, {"DISPLAY": "unix/:042.0"}):
            self.assert_no_launch(lambda: self.case(display=":42.1").start())
        with self.case() as box:
            box.env["DISPLAY"] = ":0"
            self.assert_no_launch(box.start)
        with self.case(display=None) as box:
            child = box.start(self.stub(box, "record"), stdout=subprocess.PIPE)
            self.assertNotIn("DISPLAY", self.record(child)["env"])
            self.assertEqual(child.wait(timeout=WATCHDOG), 0)
            self.assertEqual(box.socket_path.name, f"sublimite-{os.getuid()}.sock")

    def test_manifest_required_absolute_approved_executable(self):
        daily = Path(self.original["HOME"]) / ".local/bin/sublimite"
        outside = self.base / "outside"
        outside.write_text(STUB)
        outside.chmod(0o700)
        link = self.build / "alias"
        link.symlink_to(outside)
        hardlink = self.build / "hardlink"
        os.link(outside, hardlink)
        nonexecutable = self.build / "nonexecutable"
        nonexecutable.write_text(STUB)
        alias_dir = self.build / "linked-directory"
        alias_dir.symlink_to(self.build, target_is_directory=True)
        for target in (
            replace(self.target, executable=Path(self.exe.name)),
            replace(self.target, executable=daily, build_dir=daily.parent),
            replace(self.target, executable=outside),
            replace(self.target, executable=self.build / "missing"),
            replace(self.target, executable=link),
            replace(self.target, executable=hardlink),
            replace(self.target, executable=nonexecutable),
            replace(self.target, executable=alias_dir / self.exe.name),
            replace(self.target, sha256="0" * 64),
            replace(self.target, role="unknown"),
        ):
            with self.subTest(target=target):
                self.assert_no_launch(lambda: self.case(target=target).start())
        self.assertFalse(daily.parent.exists())

    def test_wrong_peer_pid(self):
        with self.case() as box:
            runtime = os.open(box.socket_path.parent, os.O_DIRECTORY)
            self.addCleanup(os.close, runtime)
            address = f"/proc/self/fd/{runtime}/{box.socket_path.name}"
            # The owned process stays live while the parent plants the listener.
            child = box.start(("hold",), stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            self.assertTrue(select.select([child.stdout], [], [], WATCHDOG)[0])
            self.assertEqual(child.stdout.readline(), b"holding\n")
            with socket.socket(socket.AF_UNIX) as impostor:
                impostor.bind(address)
                box.socket_path.chmod(0o600)
                impostor.listen()
                lock = box.socket_path.with_suffix(".lock")
                lock.touch(mode=0o600)
                with self.assertRaisesRegex(Refused, "peer"):
                    box.record_server()
                self.assert_no_launch(box.relay)

    def test_wrong_recorded_pid_on_restart(self):
        with self.case() as box:
            primary = self.start_server(box)
            primary.kill()
            self.assertEqual(primary.wait(timeout=WATCHDOG), -signal.SIGKILL)
            box._endpoint = replace(box._endpoint, pid=os.getpid())
            self.assert_no_launch(box.restart)

    def test_wrong_recorded_inode_or_lock_on_restart(self):
        for leaf in ("socket", "lock"):
            with self.subTest(leaf=leaf), self.case() as box:
                primary = self.start_server(box)
                primary.kill()
                self.assertEqual(primary.wait(timeout=WATCHDOG), -signal.SIGKILL)
                path = box.socket_path
                if leaf == "lock":
                    path = path.with_suffix(".lock")
                path.rename(path.with_name("old"))
                if leaf == "lock":
                    path.touch(mode=0o600)
                    self.assert_no_launch(box.restart)
                else:
                    runtime = os.open(path.parent, os.O_DIRECTORY)
                    self.addCleanup(os.close, runtime)
                    with socket.socket(socket.AF_UNIX) as impostor:
                        impostor.bind(f"/proc/self/fd/{runtime}/{path.name}")
                        path.chmod(0o600)
                        impostor.listen()
                        self.assert_no_launch(box.restart)

    def test_restart_refuses_still_listening_recorded_socket(self):
        with self.case() as box:
            primary = self.start_server(box)
            primary.kill()
            self.assertEqual(primary.wait(timeout=WATCHDOG), -signal.SIGKILL)
            # Inject a successful dial after the genuine dead socket's inode check.
            with patch.object(box, "_dial", return_value=socket.socket(socket.AF_UNIX)) as probe, \
                 patch("sandbox.subprocess.Popen") as spawn:
                with self.assertRaisesRegex(Refused, "listening"):
                    box.restart()
                probe.assert_called_once()
                spawn.assert_not_called()

    def test_restart_requires_expected_process_and_socket_state(self):
        with self.case() as box:
            primary = self.start_server(box)
            self.assert_no_launch(box.restart)
            with box.connect() as peer:
                peer.sendall(b"quit-stale")
            self.assertEqual(primary.wait(timeout=WATCHDOG), 0)
            self.assert_no_launch(box.restart)
        with self.case() as box:
            primary = self.start_server(box)
            primary.kill()
            self.assertEqual(primary.wait(timeout=WATCHDOG), -signal.SIGKILL)
            box.socket_path.unlink()
            self.assert_no_launch(box.restart)

    def test_private_files_and_allowlist_are_revalidated(self):
        with self.case() as box:
            box.env["WAYLAND_DISPLAY"] = "wayland-0"
            self.assert_no_launch(box.start)
            del box.env["WAYLAND_DISPLAY"]
            Path(box.env["FONTCONFIG_FILE"]).write_text("foreign font configuration")
            self.assert_no_launch(box.start)
        with self.case() as box:
            Path(box.env["XAUTHORITY"]).unlink()
            self.assert_no_launch(box.start)

    def test_rechecks_before_connect_and_restart(self):
        with self.case() as box:
            primary = self.start_server(box)
            escape = box.fixtures / "escape"
            escape.symlink_to(self.base)
            self.assert_no_launch(box.connect)
            self.assert_no_launch(box.relay)
            primary.kill()
            primary.wait(timeout=WATCHDOG)
            self.assert_no_launch(box.restart)

    def test_root_and_runtime_replacement(self):
        for which in ("root", "runtime"):
            with self.subTest(which=which), self.case() as box:
                path = box.root if which == "root" else box.socket_path.parent
                displaced = path.with_name(path.name + "-displaced")
                path.rename(displaced)
                path.mkdir(mode=0o700)
                sentinel = path / "foreign"
                sentinel.touch()
                try:
                    self.assert_no_launch(box.start)
                    with self.assertRaises(Refused):
                        box.close()
                    self.assertTrue(sentinel.exists())
                finally:
                    sentinel.unlink()
                    path.rmdir()
                    displaced.rename(path)

    def test_private_primary_relay_restart(self):
        noise = {name: "inherited" for name in (
            "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS",
            "DESKTOP_STARTUP_ID", "XDG_ACTIVATION_TOKEN", "EDIT_DISPLAY",
            "EDIT_WAYLAND_DISPLAY", "EDIT_TRACE_DUMP", "SUBLIMITE_TEST_INPUT",
            "EDIT_IPC_NAMESPACE", "EDIT_IPC_TEST_PEER_UID", "PATH", "XAUTHORITY",
            "FONTCONFIG_FILE", "PYTHONPATH", "LD_PRELOAD",
        )}
        noise["DISPLAY"] = ":0"
        with patch.dict(os.environ, noise), self.case() as box, self.case() as other:
            self.assertNotEqual(box.root, other.root)
            self.assertEqual(stat.S_IMODE(box.root.stat().st_mode), 0o700)
            self.assertEqual(box.socket_path.name, f"sublimite-{os.getuid()}-d3a34322e31.sock")
            (box.fixtures / "input").write_text("fixture")
            stray = os.open(box.fixtures / "input", os.O_RDONLY)
            try:
                os.set_inheritable(stray, True)
                primary = box.start(self.stub(box, "primary", "argument with spaces"), stdout=subprocess.PIPE)
                record = self.record(primary)
                self.assertNotIn(stray, record["fds"])
            finally:
                os.close(stray)
            self.assertEqual(record["cwd"], str(box.work))
            self.assertEqual(record["argv"], ["argument with spaces"])
            self.assertEqual(record["env"]["DISPLAY"], DISPLAY)
            for name in noise:
                if name in ("DISPLAY", "XAUTHORITY", "FONTCONFIG_FILE"):
                    self.assertNotEqual(record["env"][name], noise[name])
                else:
                    self.assertNotIn(name, record["env"])
            for name in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_RUNTIME_DIR",
                         "XDG_CACHE_HOME", "XDG_STATE_HOME", "TMPDIR", "XAUTHORITY", "FONTCONFIG_FILE"):
                self.assertTrue(Path(record["env"][name]).is_relative_to(box.root), name)
            box.record_server()
            # A peer launch (v1 --new-instance) may carry the caller's named fds, and only those.
            named, stray = os.pipe()
            try:
                os.set_inheritable(stray, True)
                relay = box.relay(self.stub(box, "relay"), stdout=subprocess.PIPE, pass_fds=(named,))
                forwarded = self.record(relay)
            finally:
                os.close(named)
                os.close(stray)
            self.assertIn(named, forwarded["fds"])
            self.assertNotIn(stray, forwarded["fds"])
            self.assertEqual(relay.wait(timeout=WATCHDOG), 0)
            self.assertEqual(forwarded["env"], record["env"])
            self.assertEqual(forwarded["reply"], "primary")
            self.assertEqual((box.data_dir / "session").read_text(), "primary")
            self.assertFalse(other.data_dir.exists())
            primary.kill()
            self.assertEqual(primary.wait(timeout=WATCHDOG), -signal.SIGKILL)
            restart = box.restart(self.stub(box, "restart"), stdout=subprocess.PIPE)
            self.assertEqual(self.record(restart)["env"], record["env"])
            box.record_server()
            with box.connect() as link:
                link.settimeout(WATCHDOG)
                link.sendall(b"relay")
                self.assertEqual(link.recv(64), b"primary")
            root = box.root
        self.assertFalse(root.exists())

    def test_target_role_sets_namespace(self):
        with self.case(target=replace(self.target, role="v2")) as box:
            self.assertEqual(box.socket_path.name, f"sublimite-v2-{os.getuid()}-d3a34322e31.sock")
            self.assertEqual(box.data_dir.name, "sublimite-v2")
            self.assertEqual(box.config_dir.name, "sublimite-v2")
            self.start_server(box)

    def test_orderly_restart(self):
        with self.case() as box:
            primary = self.start_server(box)
            with box.connect() as peer:
                peer.sendall(b"quit")
            self.assertEqual(primary.wait(timeout=WATCHDOG), 0)
            self.assertFalse(box.socket_path.exists())
            child = box.restart(self.stub(box, "primary"), stdout=subprocess.PIPE)
            self.record(child)
            box.record_server()

    def test_restart_after_error_exit_that_removed_its_socket(self):
        # v1 exits 1 on a fatal editor error after ipc_server_fini (main.c:342-344).
        with self.case() as box:
            primary = self.start_server(box)
            with box.connect() as peer:
                peer.sendall(b"fail")
            self.assertEqual(primary.wait(timeout=WATCHDOG), 1)
            self.assertFalse(box.socket_path.exists())
            child = box.restart(self.stub(box, "primary"), stdout=subprocess.PIPE)
            self.record(child)
            box.record_server()
        with self.case() as box:
            primary = self.start_server(box)
            with box.connect() as peer:
                peer.sendall(b"fail-stale")
            self.assertEqual(primary.wait(timeout=WATCHDOG), 1)
            self.assert_no_launch(box.restart)

    def test_restart_refuses_replaced_executable_with_same_digest(self):
        with self.case() as box:
            primary = self.start_server(box)
            with box.connect() as peer:
                peer.sendall(b"quit")
            self.assertEqual(primary.wait(timeout=WATCHDOG), 0)
            self.exe.rename(self.exe.with_suffix(".old"))
            self.exe.write_text(STUB)
            self.exe.chmod(0o700)
            self.assert_no_launch(box.restart)


if __name__ == "__main__":
    raise SystemExit(records.unittest_main("SANDBOX"))
