"""Synchronous standard-library client for the persistent contract X11 driver."""

import binascii
import math
import os
import re
import selectors
import subprocess
import time


class XDriverError(RuntimeError):
    """The driver refused a command and returned its error code."""


class XDriverProtocolError(XDriverError):
    """The driver stream is unusable."""


_UINT32_MAX = (1 << 32) - 1
_UNSIGNED = re.compile(rb"0|[1-9][0-9]*")
_ERROR = re.compile(rb"ERR [A-Z][A-Z0-9_]*")


def _uint(value, minimum=0, maximum=_UINT32_MAX):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"expected integer in {minimum}..{maximum}: {value!r}")
    return str(value).encode("ascii")


def _keysym(name):
    if type(name) is int:
        if not 1 <= name <= _UINT32_MAX:
            raise ValueError(f"numeric X keysym outside uint32 range: {name}")
        return hex(name).encode("ascii")
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_]+", name):
        raise ValueError(f"invalid X keysym: {name!r}")
    return name.encode("ascii")


def _selection(name):
    if name not in ("CLIPBOARD", "PRIMARY"):
        raise ValueError(f"invalid X selection: {name!r}")
    return name.encode("ascii")


class XDriver:
    """Keep one driver connection alive across actions, markers and windows.

    The caller owns the authenticated display and provides the child environment.
    Use sequentially. Pass a process watchdog in seconds and command deadlines
    in milliseconds. The watchdog covers pipe I/O as well as server stalls.
    """

    def __init__(self, executable, display, *, env, timeout):
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be positive and finite")
        self._timeout = timeout
        self._incoming = bytearray()
        self._process = subprocess.Popen(
            [executable, "--display", display], env=env, bufsize=0,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        )
        try:
            os.set_blocking(self._process.stdin.fileno(), False)
            os.set_blocking(self._process.stdout.fileno(), False)
            ready = self._line(time.monotonic() + timeout)
            if not ready.startswith(b"READY "):
                raise XDriverProtocolError(f"expected READY, got {ready[:80]!r}")
            self.max_request_bytes = self._parse_uint(ready[6:], 4, _UINT32_MAX * 4)
            if self.max_request_bytes % 4:
                raise XDriverProtocolError("unaligned X request limit")
        except BaseException:
            self._dispose()
            raise

    @staticmethod
    def _wait(selector, deadline):
        remaining = deadline - time.monotonic()
        if remaining > 0 and selector.select(remaining):
            return
        raise XDriverProtocolError("xdriver pipe deadline expired")

    def _line(self, deadline):
        fd = self._process.stdout.fileno()
        with selectors.DefaultSelector() as selector:
            selector.register(fd, selectors.EVENT_READ)
            while True:
                self._wait(selector, deadline)
                try:
                    chunk = os.read(fd, 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise XDriverProtocolError("xdriver closed stdout before a complete reply")
                end = chunk.find(b"\n")
                if end < 0:
                    self._incoming.extend(chunk)
                    continue
                if end != len(chunk) - 1:
                    raise XDriverProtocolError("xdriver returned data after its reply")
                self._incoming.extend(chunk[:end])
                line = bytes(self._incoming)
                self._incoming.clear()
                return line

    def _write(self, data, deadline):
        fd = self._process.stdin.fileno()
        view = memoryview(data)
        with selectors.DefaultSelector() as selector:
            selector.register(fd, selectors.EVENT_WRITE)
            while view:
                self._wait(selector, deadline)
                try:
                    written = os.write(fd, view)
                except BlockingIOError:
                    continue
                view = view[written:]

    @staticmethod
    def _parse_uint(raw, minimum=0, maximum=_UINT32_MAX):
        if len(raw) > len(str(maximum)) or not _UNSIGNED.fullmatch(raw):
            raise XDriverProtocolError(f"invalid integer reply: {raw[:80]!r}")
        value = int(raw)
        if not minimum <= value <= maximum:
            raise XDriverProtocolError(f"integer reply outside {minimum}..{maximum}: {value}")
        return value

    @staticmethod
    def _parse_data(raw):
        if raw == b"-":
            return b""
        if not raw or len(raw) % 2 or raw.translate(None, b"0123456789abcdef"):
            raise XDriverProtocolError(f"invalid byte reply: {raw[:80]!r}")
        return binascii.unhexlify(raw)

    def _ask(self, command, *args, parse=None):
        if self._process is None:
            raise XDriverProtocolError("xdriver is closed")
        wire = b" ".join((command, *args)) + b"\n"
        deadline = time.monotonic() + self._timeout
        try:
            self._write(wire, deadline)
            reply = self._line(deadline)
            if parse is None and reply == b"OK":
                return None
            if parse is not None and reply.startswith(b"OK "):
                return parse(reply[3:])
            if _ERROR.fullmatch(reply):
                raise XDriverError(reply[4:].decode("ascii"))
            raise XDriverProtocolError(f"unexpected {command.decode()} reply: {reply[:80]!r}")
        except OSError as exc:
            self._dispose()
            raise XDriverProtocolError(f"xdriver pipe failed: {exc}") from exc
        except XDriverProtocolError:
            self._dispose()
            raise

    def find(self, pid, timeout_ms):
        """Bind one mapped top-level window for pid and clear verified focus."""
        return self._ask(b"find", _uint(pid, 1), _uint(timeout_ms, 1),
                         parse=lambda raw: self._parse_uint(raw, 1))

    def focus(self):
        self._ask(b"focus")

    def key(self, keysym):
        self._ask(b"key", _keysym(keysym))

    def chord(self, *modifiers_and_key):
        if len(modifiers_and_key) < 2:
            raise ValueError("a chord needs one or more modifiers and a key")
        self._ask(b"chord", *map(_keysym, modifiers_and_key))

    def motion(self, x, y):
        """Move to target-relative coordinates."""
        self._ask(b"motion", _uint(x, 0, 32767), _uint(y, 0, 32767))

    def button(self, detail, action="click"):
        if action not in ("down", "up", "click"):
            raise ValueError(f"invalid button action: {action!r}")
        self._ask(b"button", _uint(detail, 1, 255), action.encode("ascii"))

    def owner(self, selection):
        """Return the current owner XID, or zero when unowned."""
        return self._ask(b"owner", _selection(selection), parse=self._parse_uint)

    def own(self, selection, payload, timeout_ms=None):
        """Publish exact bytes and return the helper's selection owner XID."""
        if not isinstance(payload, bytes):
            raise TypeError("selection payload must be bytes")
        if timeout_ms is None:
            timeout_ms = max(1, min(_UINT32_MAX, int(self._timeout * 500)))
        hex_bytes = payload.hex().encode("ascii") if payload else b"-"
        return self._ask(b"own", _selection(selection), hex_bytes, _uint(timeout_ms, 1),
                         parse=lambda raw: self._parse_uint(raw, 1))

    def read(self, selection, expected_owner, timeout_ms):
        return self._ask(b"read", _selection(selection), _uint(expected_owner, 1),
                         _uint(timeout_ms, 1), parse=self._parse_data)

    def marker(self, *words):
        if len(words) != 5:
            raise ValueError("format-32 X11 ClientMessage requires five words")
        self._ask(b"marker", *map(_uint, words))

    def _dispose(self):
        process, self._process = self._process, None
        if process is None:
            return
        process.stdin.close()
        process.stdout.close()
        if process.poll() is None:
            process.kill()
        process.wait(timeout=self._timeout)

    def close(self):
        if self._process is None:
            return
        try:
            self._ask(b"quit")
            try:
                code = self._process.wait(timeout=self._timeout)
            except subprocess.TimeoutExpired as exc:
                raise XDriverProtocolError("xdriver did not exit after quit") from exc
            if code or self._process.stdout.read():
                raise XDriverProtocolError(f"invalid xdriver shutdown, exit status {code}")
        finally:
            self._dispose()

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.close()
