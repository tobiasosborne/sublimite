"""Linux process containment for the behavioral contract runner."""

from contextlib import ExitStack, contextmanager
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import signal
import socket
import stat
import struct
import subprocess
import tempfile


class Refused(RuntimeError):
    """The case could not establish containment."""


@dataclass(frozen=True)
class RunTarget:
    """Target fields from the trusted run manifest."""

    role: str
    executable: Path
    build_dir: Path
    sha256: str


@dataclass(frozen=True)
class _Endpoint:
    pid: int
    socket_fd: int
    lock_fd: int

    def close(self):
        os.close(self.socket_fd)
        os.close(self.lock_fd)


_APP_IDS = {"v1": "sublimite", "v2": "sublimite-v2"}
_DIRS = {
    "HOME": "home",
    "XDG_DATA_HOME": "data",
    "XDG_CONFIG_HOME": "config",
    "XDG_RUNTIME_DIR": "runtime",
    "XDG_CACHE_HOME": "cache",
    "XDG_STATE_HOME": "state",
    "TMPDIR": "tmp",
}
_HOME_DEFAULTS = {
    "XDG_DATA_HOME": ".local/share",
    "XDG_CONFIG_HOME": ".config",
    "XDG_CACHE_HOME": ".cache",
    "XDG_STATE_HOME": ".local/state",
}
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
_PATH_FLAGS = os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC
_DISPLAY = re.compile(r":([0-9]+)(?:\.[0-9]+)?")
_LOCAL_DISPLAY = re.compile(r"(?:unix/|unix|localhost)?(:[0-9]+(?:\.[0-9]+)?)")
# A standalone config keeps system font discovery out of the user's profile.
_FONTS = b"""<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
"""


def _absolute(value, label):
    if value is None:
        raise Refused(f"{label} must be absolute")
    path = Path(value)
    if path.anchor != "/" or ".." in path.parts:
        raise Refused(f"{label} must be an absolute path without '..': {value!r}")
    return path


def _identity(info):
    return info.st_dev, info.st_ino


def _same_file(fd, info):
    return info is not None and _identity(os.fstat(fd)) == _identity(info)


def _stat_at(fd, name):
    try:
        return os.stat(name, dir_fd=fd, follow_symlinks=False)
    except FileNotFoundError:
        return None


def _proc_path(fd, pid="self"):
    return f"/proc/{pid}/fd/{fd}"


@contextmanager
def _absolute_dir(path):
    """Open every component without following a link, including the last."""
    fd = os.open("/", _DIR_FLAGS)
    try:
        for component in path.parts[1:]:
            next_fd = os.open(component, _DIR_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        yield fd
    finally:
        os.close(fd)


class Sandbox:
    """Own one private tree, its editor processes and the named IPC endpoint.

    The driver supplies the display after establishing its private X server.
    With no display and no inherited DISPLAY, a case can run headless.
    """

    def __init__(self, base, target, *, display=None):
        self._creator = os.getpid()
        self._uid = os.getuid()
        self._target = target
        self._app_id = _APP_IDS.get(target.role)
        if self._app_id is None:
            raise Refused(f"unknown manifest target role: {target.role!r}")
        inherited = dict(os.environ)
        self._check_display(display, inherited.get("DISPLAY"))
        suffix = "-d" + display.encode("ascii").hex() if display is not None else ""
        self._socket_leaf = f"{self._app_id}-{self._uid}{suffix}.sock"
        # v1's endpoint.leaf is char[64], including its terminating NUL.
        if len(self._socket_leaf) >= 64:
            raise Refused("DISPLAY is too long for the filesystem socket name")
        real_home = _absolute(inherited.get("HOME"), "original HOME")
        self._daily = real_home / ".local/bin/sublimite"
        self._forbidden = self._live_roots(real_home, inherited)
        self._base = _absolute(base, "sandbox parent")
        self._reject_profile_path(self._base)

        self._primary = None
        self._endpoint = None
        self._children = []
        self._target_fd = None
        self._closed = False
        self._pins = {}
        # A single stack owns the directory pins for the life of the case.
        self._fds = ExitStack()
        try:
            self._base_fd = self._fds.enter_context(_absolute_dir(self._base))
            made = tempfile.mkdtemp(prefix="case-", dir=_proc_path(self._base_fd))
            self.root = self._base / Path(made).name
            root_fd = self._pin_dir(self._base_fd, self.root.name)
            self._root_fd = root_fd
            try:
                for name in (*_DIRS.values(), "work", "fixtures"):
                    os.mkdir(name, mode=0o700, dir_fd=root_fd)
                    self._pins[name] = self._pin_dir(root_fd, name)
                os.mkdir("fontconfig", mode=0o700, dir_fd=self._pins["cache"])
                self._create_file("Xauthority", b"")
                self._create_file("fonts.conf", _FONTS)
                self._env = {key: str(self.root / name) for key, name in _DIRS.items()}
                self._env.update(LANG="C.UTF-8", XAUTHORITY=str(self.root / "Xauthority"),
                                 FONTCONFIG_FILE=str(self.root / "fonts.conf"))
                if display is not None:
                    self._env["DISPLAY"] = display
                self.env = self._env.copy()
                self.work = self.root / "work"
                self.fixtures = self.root / "fixtures"
                self.data_dir = self.root / "data" / self._app_id
                self.config_dir = self.root / "config" / self._app_id
                self.socket_path = self.root / "runtime" / self._socket_leaf
                self._lock_leaf = self.socket_path.with_suffix(".lock").name
            except BaseException:
                self._remove_root(self._base_fd, root_fd)
                raise
        except BaseException:
            self._fds.close()
            raise

    @staticmethod
    def _check_display(display, inherited):
        if display is None:
            if inherited is not None:
                raise Refused("inherited DISPLAY cannot select a display for the case")
            return
        match = _DISPLAY.fullmatch(display)
        if match is None or not match[1].lstrip("0"):
            raise Refused(f"DISPLAY must select a positive local display: {display!r}")
        ambient = _LOCAL_DISPLAY.fullmatch(inherited or "")
        if ambient and _DISPLAY.fullmatch(ambient[1])[1].lstrip("0") == match[1].lstrip("0"):
            raise Refused(f"DISPLAY selects the inherited server: {display!r}")

    def _live_roots(self, home, inherited):
        roots = {key: {home / suffix} for key, suffix in _HOME_DEFAULTS.items()}
        roots.update(HOME={home}, XDG_RUNTIME_DIR={Path(f"/run/user/{self._uid}")})
        for key in (*_HOME_DEFAULTS, "XDG_RUNTIME_DIR", "EDIT_IPC_FALLBACK_DIR"):
            value = inherited.get(key, "")
            if value.startswith("/"):
                # POSIX/Linux treats multiple leading slashes as the same root.
                path = Path(os.path.normpath("/" + value.lstrip("/")))
                roots["XDG_RUNTIME_DIR" if key == "EDIT_IPC_FALLBACK_DIR" else key].add(path)
        return roots

    def _reject_profile_path(self, path):
        if path == self._daily:
            raise Refused(f"installed daily editor is forbidden: {path}")
        # Worktrees and build directories can live under the original HOME.
        # The nested live profile and IPC roots must never be opened.
        for key, roots in self._forbidden.items():
            if key != "HOME" and any(path.is_relative_to(root) for root in roots):
                raise Refused(f"path is in an original {key} location: {path}")

    def _pin_dir(self, parent, name):
        fd = os.open(name, _DIR_FLAGS, dir_fd=parent)
        self._fds.callback(os.close, fd)
        self._private_dir(fd, name)
        return fd

    def _private_dir(self, fd, label):
        info = os.fstat(fd)
        if info.st_uid != self._uid or stat.S_IMODE(info.st_mode) != 0o700:
            raise Refused(f"directory must be owned by uid {self._uid} with mode 0700: {label}")
        return info.st_dev

    def _create_file(self, name, content):
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC
        fd = os.open(name, flags, 0o600, dir_fd=self._root_fd)
        with os.fdopen(fd, "wb") as file:
            file.write(content)

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.close()

    def _require_creator(self):
        if self._closed or os.getpid() != self._creator:
            raise Refused("only the creating process may use an open sandbox")

    @contextmanager
    def _root(self):
        """Reopen the physical path and verify the pins from the top down."""
        try:
            with _absolute_dir(self._base) as base_fd, ExitStack() as opened:
                if not _same_file(self._base_fd, os.fstat(base_fd)):
                    raise Refused("sandbox parent identity changed")
                root_fd = os.open(self.root.name, _DIR_FLAGS, dir_fd=base_fd)
                opened.callback(os.close, root_fd)
                if not _same_file(self._root_fd, os.fstat(root_fd)):
                    raise Refused("sandbox root identity changed")
                self._private_dir(root_fd, self.root)
                for name, pin in self._pins.items():
                    fd = os.open(name, _DIR_FLAGS, dir_fd=root_fd)
                    opened.callback(os.close, fd)
                    if not _same_file(pin, os.fstat(fd)):
                        raise Refused(f"private directory identity changed: {name}")
                    self._private_dir(fd, name)
                yield base_fd, root_fd
        except OSError as exc:
            raise Refused(f"sandbox filesystem check failed: {exc}") from exc

    def _walk(self, fd, device, *, at_root=False, at_runtime=False, cleanup=False):
        """Verify descendants with directory fds. Cleanup can unlink symlinks."""
        for name in os.listdir(fd):
            info = _stat_at(fd, name)
            if info is None:
                continue  # A live child can rename away a temporary artifact.
            if info.st_dev != device or (not stat.S_ISLNK(info.st_mode) and info.st_uid != self._uid):
                raise Refused(f"foreign owner or filesystem inside the sandbox: {name}")
            if stat.S_ISDIR(info.st_mode):
                if at_root and name in self._pins and not _same_file(self._pins[name], info):
                    raise Refused(f"private directory identity changed: {name}")
                child = os.open(name, _DIR_FLAGS, dir_fd=fd)
                try:
                    if not _same_file(child, info):
                        raise Refused(f"sandbox directory identity changed: {name}")
                    if not cleanup:
                        self._private_dir(child, name)
                    self._walk(child, device, at_runtime=at_root and name == "runtime",
                               cleanup=cleanup)
                    if cleanup:
                        self._rmdir(fd, name, child)
                finally:
                    os.close(child)
            elif cleanup:
                os.unlink(name, dir_fd=fd)
            else:
                regular = stat.S_ISREG(info.st_mode) and info.st_nlink == 1
                endpoint = (at_runtime and name == self._socket_leaf
                            and stat.S_ISSOCK(info.st_mode))
                if not regular and not endpoint:
                    raise Refused(f"unexpected link or special file inside the sandbox: {name}")

    @staticmethod
    def _rmdir(parent, name, fd):
        if not _same_file(fd, _stat_at(parent, name)):
            raise Refused(f"sandbox directory identity changed before removal: {name}")
        os.rmdir(name, dir_fd=parent)

    def _remove_root(self, base_fd, root_fd):
        if not _same_file(root_fd, _stat_at(base_fd, self.root.name)):
            raise Refused("sandbox root identity changed before cleanup")
        self._walk(root_fd, os.fstat(root_fd).st_dev, at_root=True, cleanup=True)
        self._rmdir(base_fd, self.root.name, root_fd)

    @contextmanager
    def _checked(self):
        self._require_creator()
        env = self.env.copy()
        for key in _DIRS:
            value = _absolute(env.get(key), key)
            if value in self._forbidden.get(key, ()):
                raise Refused(f"{key} selects an original profile or runtime location")
        if env != self._env:
            raise Refused("child environment differs from the sandbox allowlist")
        with self._root() as (_, root_fd):
            self._walk(root_fd, os.fstat(root_fd).st_dev, at_root=True)
            for name in ("Xauthority", "fonts.conf"):
                info = _stat_at(root_fd, name)
                if info is None or not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600:
                    raise Refused(f"missing or non-private case file: {name}")
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
            with os.fdopen(os.open("fonts.conf", flags, dir_fd=root_fd), "rb") as fonts:
                if fonts.read(len(_FONTS) + 1) != _FONTS:
                    raise Refused("case font configuration changed")
            yield env

    @contextmanager
    def _approved_executable(self):
        name = _absolute(self._target.executable, "manifest executable")
        build = _absolute(self._target.build_dir, "manifest build directory")
        self._reject_profile_path(name)
        self._reject_profile_path(build)
        if not name.is_relative_to(build) or name == build:
            raise Refused(f"executable is outside its designated build directory: {name}")
        with _absolute_dir(name.parent) as parent:
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
            fd = os.open(name.name, flags, dir_fd=parent)
            try:
                info = os.fstat(fd)
                if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not info.st_mode & 0o111:
                    raise Refused(f"manifest executable must be a singly linked executable file: {name}")
                with os.fdopen(os.dup(fd), "rb") as file:
                    digest = hashlib.file_digest(file, "sha256").hexdigest()
                if digest != self._target.sha256:
                    raise Refused(f"manifest executable digest mismatch: {name}")
                if self._target_fd is not None and not _same_file(self._target_fd, info):
                    raise Refused(f"manifest executable identity changed: {name}")
                yield fd
            finally:
                os.close(fd)

    def _spawn(self, argv, env, *, stdin, stdout, stderr, pass_fds):
        with self._approved_executable() as fd:
            if self._target_fd is None:
                self._target_fd = os.dup(fd)
                self._fds.callback(os.close, self._target_fd)
            # Keep an executable pin open in the parent so shebang scripts can
            # be reopened by their interpreters with no extra child fd.
            child = subprocess.Popen(
                [str(self._target.executable), *argv], executable=_proc_path(self._target_fd, self._creator),
                cwd=_proc_path(self._pins["work"]), env=env, umask=0o077,
                stdin=stdin, stdout=stdout, stderr=stderr, close_fds=True, pass_fds=pass_fds,
            )
        self._children.append(child)
        return child

    def _socket_and_lock(self):
        runtime = self._pins["runtime"]
        return _stat_at(runtime, self._socket_leaf), _stat_at(runtime, self._lock_leaf)

    @staticmethod
    def _dial(endpoint):
        peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            peer.setblocking(False)
            # Resolve the verified socket inode itself, even if its name moves.
            peer.connect(_proc_path(endpoint.socket_fd))
            peer.setblocking(True)
            return peer
        except BaseException:
            peer.close()
            raise

    def _verify_endpoint(self, endpoint, *, allow_missing_socket=False):
        sock, lock = self._socket_and_lock()
        if not _same_file(endpoint.lock_fd, lock):
            raise Refused("recorded IPC lock identity changed")
        if lock.st_uid != self._uid or lock.st_nlink != 1 or stat.S_IMODE(lock.st_mode) != 0o600:
            raise Refused("recorded IPC lock must remain private")
        if sock is None and allow_missing_socket:
            return False
        if not _same_file(endpoint.socket_fd, sock):
            raise Refused("recorded IPC socket identity changed")
        if sock.st_uid != self._uid or stat.S_IMODE(sock.st_mode) != 0o600:
            raise Refused("recorded IPC socket must remain private")
        return True

    def _verify_peer(self, endpoint):
        self._verify_endpoint(endpoint)
        peer = self._dial(endpoint)
        try:
            credentials = peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("iII"))
            pid, uid, _ = struct.unpack("iII", credentials)
            if (pid, uid) != (endpoint.pid, self._uid):
                raise Refused(f"IPC peer credentials do not match the owned server: pid={pid}, uid={uid}")
            self._verify_endpoint(endpoint)
            if self._primary.poll() is not None:
                raise Refused("owned IPC server exited during verification")
            return peer
        except BaseException:
            peer.close()
            raise

    def start(self, argv=(), *, stdin=subprocess.DEVNULL, stdout=None, stderr=None, pass_fds=()):
        """Start the first primary. Both endpoint names must be absent."""
        with self._checked() as env:
            if self._primary is not None or any(self._socket_and_lock()):
                raise Refused("initial launch requires an unused socket and lock")
            self._primary = self._spawn(argv, env, stdin=stdin, stdout=stdout,
                                        stderr=stderr, pass_fds=pass_fds)
            return self._primary

    def record_server(self):
        """Record the ready primary's socket and lifecycle lock after a peer check."""
        with self._checked():
            if self._primary is None or self._primary.poll() is not None or self._endpoint is not None:
                raise Refused("recording requires a live, unrecorded primary")
            sock, lock = self._socket_and_lock()
            for info, test in ((sock, stat.S_ISSOCK), (lock, stat.S_ISREG)):
                if info is None or not test(info.st_mode) or info.st_uid != self._uid:
                    raise Refused("missing or invalid case IPC socket or lock")
            runtime = self._pins["runtime"]
            with ExitStack() as opened:
                sock_fd = os.open(self._socket_leaf, _PATH_FLAGS, dir_fd=runtime)
                opened.callback(os.close, sock_fd)
                lock_fd = os.open(self._lock_leaf, _PATH_FLAGS, dir_fd=runtime)
                opened.callback(os.close, lock_fd)
                if not _same_file(sock_fd, sock) or not _same_file(lock_fd, lock):
                    raise Refused("IPC endpoint changed during recording")
                endpoint = _Endpoint(self._primary.pid, sock_fd, lock_fd)
                with self._verify_peer(endpoint):
                    pass
                self._endpoint = endpoint
                opened.pop_all()

    def _recorded(self):
        endpoint = self._endpoint
        if endpoint is None or self._primary is None or endpoint.pid != self._primary.pid:
            raise Refused("IPC endpoint does not match a recorded primary PID")
        return endpoint

    def connect(self):
        """Return a connection to the verified primary, owned by the caller."""
        with self._checked():
            endpoint = self._recorded()
            if self._primary.poll() is not None:
                raise Refused("recorded IPC primary already exited")
            return self._verify_peer(endpoint)

    def relay(self, argv=(), *, stdin=subprocess.DEVNULL, stdout=None, stderr=None):
        """Launch a forwarding process after verifying the existing primary."""
        with self.connect():
            pass
        with self._checked() as env:
            self._verify_endpoint(self._recorded())
            return self._spawn(argv, env, stdin=stdin, stdout=stdout, stderr=stderr, pass_fds=())

    def restart(self, argv=(), *, stdin=subprocess.DEVNULL, stdout=None, stderr=None, pass_fds=()):
        """Allow the editor to reclaim only its own recorded, dead socket.

        A reaped SIGKILL must leave the recorded socket refusing connections. Any exit with a
        status (0, or an error status such as v1's 1 after a fatal editor error) must have
        removed its socket. Other signals are refused."""
        with self._checked():
            endpoint = self._recorded()
            status = self._primary.poll()
            if status is None:
                raise Refused("restart requires the recorded primary to have been reaped")
            if status == -signal.SIGKILL:
                self._verify_endpoint(endpoint)
                try:
                    with self._dial(endpoint):
                        raise Refused("recorded IPC socket is still listening")
                except ConnectionRefusedError:
                    pass
            elif status < 0 or self._verify_endpoint(endpoint, allow_missing_socket=True):
                raise Refused("restart requires SIGKILL or an exit that removed its socket")

        with self._checked() as env:
            exited = status >= 0
            has_socket = self._verify_endpoint(endpoint, allow_missing_socket=exited)
            if has_socket and exited:
                raise Refused("IPC socket reappeared after exit")
            self._primary = self._spawn(argv, env, stdin=stdin, stdout=stdout,
                                        stderr=stderr, pass_fds=pass_fds)
            self._endpoint = None
            endpoint.close()
            return self._primary

    def close(self):
        """Reap owned processes and remove the verified case tree."""
        if self._closed:
            return
        self._require_creator()
        for child in reversed(self._children):
            if child.poll() is None:
                child.kill()
            child.wait()
            for stream in (child.stdin, child.stdout, child.stderr):
                if stream is not None:
                    stream.close()
        self._children.clear()
        if self._endpoint is not None:
            self._endpoint.close()
            self._endpoint = None
        with self._root() as (base_fd, root_fd):
            self._remove_root(base_fd, root_fd)
        self._fds.close()
        self._closed = True
