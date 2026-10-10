#!/bin/sh
# Shared across worktrees and tools. Keep the flock descriptor across exec/fork
# so nested callers reuse it and crashed parents cannot release active children.
set -eu
exec python3 -c '
import errno
import fcntl
import math
import os
import sys
import time

def main():
    if len(sys.argv) < 2:
        print("usage: with_display_lock.sh command [args ...]", file=sys.stderr)
        return 2
    display = os.environ.get("DISPLAY", "")
    if not os.environ.get("EDIT_ALLOW_REAL_DISPLAY") and display in (
        "", ":0", "unix:0", "localhost:0"
    ) or (not os.environ.get("EDIT_ALLOW_REAL_DISPLAY") and
          display.startswith((":0.", "unix:0.", "localhost:0."))):
        display = os.environ.get("EDIT_DISPLAY", ":99")
    number = display.rsplit(":", 1)[-1].split(".", 1)[0]
    if ":" not in display or not number.isascii() or not number.isdecimal():
        print("display lock: invalid DISPLAY", file=sys.stderr)
        return 2
    number = str(int(number))
    path = "/tmp/edit-xvfb-" + number + ".lock"
    try:
        timeout = float(os.environ.get("EDIT_DISPLAY_LOCK_TIMEOUT", "600"))
        if not math.isfinite(timeout) or timeout < 0:
            raise ValueError()
    except ValueError:
        print("display lock: invalid EDIT_DISPLAY_LOCK_TIMEOUT", file=sys.stderr)
        return 2
    os.environ["DISPLAY"] = display
    # An environment marker alone is insufficient: check the inherited inode
    # and acquire on that same open-file description (flock is reentrant there).
    inherited = os.environ.get("EDIT_DISPLAY_LOCK_FD", "")
    fd = None
    if inherited.isascii() and inherited.isdecimal():
        try:
            candidate = int(inherited)
            if os.path.samestat(os.fstat(candidate), os.stat(path)):
                fcntl.flock(candidate, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fd = candidate
        except (OSError, OverflowError):
            pass
    if fd is None:
        fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_CLOEXEC, 0o600)
        identity = os.fstat(fd)
        def owner():
            # Read the kernel owner, never a stale PID sidecar. --no-fork/exec
            # callers preserve the process that acquired the flock.
            try:
                with open("/proc/locks", encoding="ascii") as locks:
                    for line in locks:
                        fields = line.split()
                        if len(fields) < 6 or fields[1:4] != ["FLOCK", "ADVISORY", "WRITE"]:
                            continue
                        major, minor, inode = fields[5].split(":")
                        if (int(major, 16), int(minor, 16), int(inode)) == (
                            os.major(identity.st_dev), os.minor(identity.st_dev), identity.st_ino
                        ):
                            return fields[4]
            except (OSError, ValueError):
                pass
            return "unknown"
        deadline = time.monotonic() + timeout
        waiting = False
        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError as error:
                if error.errno not in (errno.EACCES, errno.EAGAIN):
                    raise
                if not waiting:
                    print("waiting for display :" + number + " (held by " + owner() + ")",
                          file=sys.stderr, flush=True)
                    waiting = True
                if time.monotonic() >= deadline:
                    print(os.path.basename(sys.argv[1]) + ": display lock timeout: display :" +
                          number + " (held by " + owner() + ")", file=sys.stderr)
                    return 1
                time.sleep(min(0.05, max(0, deadline - time.monotonic())))
    os.set_inheritable(fd, True)
    os.environ["EDIT_DISPLAY_LOCK_FD"] = str(fd)
    os.execvp(sys.argv[1], sys.argv[1:])

try:
    sys.exit(main())
except OSError as error:
    print("display lock: " + str(error), file=sys.stderr)
    sys.exit(1)
' "$@"
