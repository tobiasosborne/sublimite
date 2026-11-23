"""The one owner of private X servers and of owned-process stops (docs/decisions/v2-he7.33.md).

private_server starts Xvfb on exactly one explicit contract display with a fresh MIT cookie
and yields it once its -displayfd names that display; every call is a new server, so a case
or an editor launch never observes an earlier one's history. fresh_display adds the X driver
bound to that server. stop is the one TERM -> kill -> reap routine: an expired wait kills and
reaps, then raises, so a stuck process can never leave a run green.

Deadlines (hang detectors, never verdicts; slowest measurement (M) x 10, rounded up to a
whole second, as in R4 and X3):
  READY_S  Xvfb start -> its display number on -displayfd: slowest 0.26 s (M, X3 runs,
           load 1-17, docs/decisions/v2-he7.15.md); 0.068 s at all three geometries
           [Not charging, load 1.7] (v2-he7.33).
  STOP_S   SIGTERM -> Xvfb reaped: slowest 0.0154 s (M) [Not charging, load 1.7]; 0.0075 s
           under load 7.0 [Discharging] (v2-he7.33, 20 starts per geometry each).
"""

from contextlib import contextmanager
from dataclasses import dataclass
import math
import os
from pathlib import Path
import secrets
import selectors
import signal
import socket
import struct
import subprocess
import time

from sandbox import CONTRACT_DISPLAY
from xdriver import XDriver

WATCHDOG_FACTOR = 10
READY_S = math.ceil(0.26 * WATCHDOG_FACTOR)
STOP_S = math.ceil(0.0154 * WATCHDOG_FACTOR)
DRIVER = Path(__file__).resolve().parents[2] / "build" / "xdriver"


class XServerError(RuntimeError):
    """A private server could not be established, or exited before its owner stopped it."""


class TeardownError(RuntimeError):
    """An owned process outlived its stop deadline (it is killed and reaped by then).

    `pending` is the exception the owned block was already raising, so a case can record
    its own FAIL or ERROR and this ERROR beside it."""

    def __init__(self, message, pending=None):
        super().__init__(message)
        self.pending = pending


def available(number):
    """No lock file, socket or abstract name claims display `number` (Xvfb arbitrates last)."""
    path = f"/tmp/.X11-unix/X{number}"
    if os.path.lexists(path) or os.path.lexists(f"/tmp/.X{number}-lock"):
        return False
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
        try:
            probe.bind("\0" + path)
        except OSError:
            return False
    return True


def stop(process, timeout_s, sig=signal.SIGTERM):
    """Signal `process` once and reap it; its exit status. Past `timeout_s` it is killed and
    reaped, then TeardownError is raised. An already exited process is only reaped."""
    if process.poll() is not None:
        return process.returncode
    process.send_signal(sig)
    try:
        return process.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        process.kill()
        status = process.wait()
        raise TeardownError(f"PID {process.pid} ignored signal {sig} for {timeout_s} s; "
                            f"killed and reaped ({status})") from None


def read_display(fd, deadline_s):
    """The -displayfd answer: bytes up to and including the first newline, read across any
    number of partial writes; EOF returns what arrived. Silence until the deadline raises."""
    answer = b""
    deadline = time.monotonic() + deadline_s
    with selectors.DefaultSelector() as ready:
        ready.register(fd, selectors.EVENT_READ)
        while not answer.endswith(b"\n"):
            if not ready.select(max(0, deadline - time.monotonic())):
                raise TimeoutError(f"no display number on -displayfd within {deadline_s} s")
            chunk = os.read(fd, 16)
            if not chunk:
                break
            answer += chunk
    return answer


def cookie(number):
    """One Xauthority FamilyLocal record (network order) with a fresh 128-bit MIT cookie."""
    fields = (os.fsencode(socket.gethostname()), str(number).encode("ascii"),
              b"MIT-MAGIC-COOKIE-1", secrets.token_bytes(16))
    return struct.pack("!H", 256) + b"".join(struct.pack("!H", len(f)) + f for f in fields)


@dataclass(frozen=True)
class Server:
    display: str
    pid: int
    cookie: bytes
    auth: Path
    env: dict  # PATH and XAUTHORITY: enough for an X client outside a sandbox


@contextmanager
def private_server(display, artifacts, *, geometry=None, label="Xvfb", command=("Xvfb",)):
    """A new Xvfb on `display` (":N", a contract display), screen `geometry` ("WxHxD") or
    Xvfb's default. Files go to `artifacts`/`label`.{Xauthority,log,pid}. `command` is a test
    seam for stand-in servers. Occupancy or a wrong, short or early-ended answer is
    XServerError, silence TimeoutError. A ready server that exits before the owner stops it,
    or outlives SIGTERM by STOP_S (then killed and reaped), is TeardownError."""
    match = CONTRACT_DISPLAY.fullmatch(display)
    if match is None:
        raise XServerError(f"{display!r} is not a contract display, :120 through :199")
    number = int(match[1])
    if not available(number):
        raise XServerError(f"display {display} is occupied")
    auth = Path(artifacts) / f"{label}.Xauthority"
    record = cookie(number)
    with os.fdopen(os.open(auth, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600),
                   "wb") as stream:
        stream.write(record)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "XAUTHORITY": str(auth)}
    read_fd, write_fd = os.pipe()
    try:
        with (Path(artifacts) / f"{label}.log").open("wb") as log:
            server = subprocess.Popen(
                [*command, display, "-displayfd", str(write_fd), "-noreset", "-nolisten", "tcp",
                 "-auth", str(auth), *(("-screen", "0", geometry) if geometry else ())],
                env={**env, "TMPDIR": str(artifacts)}, cwd=artifacts, pass_fds=(write_fd,),
                stdin=subprocess.DEVNULL, stdout=log, stderr=log)
        os.close(write_fd)
        write_fd = None
        (Path(artifacts) / f"{label}.pid").write_text(f"{server.pid}\n", encoding="ascii")
        pending, ready = None, False
        try:
            answer = read_display(read_fd, READY_S)
            if answer != f"{number}\n".encode() or server.poll() is not None:
                raise XServerError(f"PID {server.pid} did not claim {display}: {answer!r}, "
                                   f"exit {server.poll()}")
            ready = True
            yield Server(display, server.pid, record, auth, env)
        except BaseException as exc:
            pending = exc
            raise
        finally:
            early = server.poll()
            try:
                stop(server, STOP_S)
            except TeardownError as exc:
                raise TeardownError(f"Xvfb on {display}: {exc}", pending) from pending
            if ready and early is not None:
                raise TeardownError(f"Xvfb PID {server.pid} on {display} exited before its "
                                    f"owner stopped it: status {early}", pending) from pending
    finally:
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)


@contextmanager
def fresh_display(display, artifacts, label, env, *, timeout, geometry=None, driver=DRIVER):
    """A new private server whose cookie is written to env["XAUTHORITY"] (the case's), and an
    X driver bound to it with `env`. Yields (server, driver)."""
    with private_server(display, artifacts, geometry=geometry, label=label) as server:
        Path(env["XAUTHORITY"]).write_bytes(server.cookie)
        with XDriver(driver, display, env=env, timeout=timeout) as bound:
            yield server, bound
