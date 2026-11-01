#!/usr/bin/env python3
"""Private Linux fixtures/marker service for wsl_share_probe.c; no WSL lifecycle calls."""
import errno
import fcntl
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time


def describe(path):
    info = path.lstat()
    text = (f"mode={stat.S_IMODE(info.st_mode):04o} uid={info.st_uid} gid={info.st_gid} "
            f"dev={info.st_dev} ino={info.st_ino} links={info.st_nlink} size={info.st_size}")
    if path.is_symlink():
        text += f" symlink={os.readlink(path)}"
    elif path.is_file():
        with path.open("rb") as handle:
            text += f" bytes_hex={handle.read(64).hex()}"
    return text


def checked_path(base, relative):
    # Reject parent traversal and symlinked parents; final symlink is inspected,
    # never followed by cleanup. All fixture symlinks point inside this tree.
    rel = Path(relative)
    if rel.is_absolute() or ".." in rel.parts or not rel.parts:
        raise ValueError("unsafe relative name")
    target = base / rel
    target.parent.resolve().relative_to(base.resolve())
    return target


def action(base, operation, relative):
    path = checked_path(base, relative)
    if operation == "hardlink":
        os.link(path, path.with_name("hard"))
    elif operation == "write":
        with path.open("r+b", buffering=0) as handle:
            handle.write(b"B")
            os.fsync(handle.fileno())
    elif operation == "truncate":
        with path.open("r+b", buffering=0) as handle:
            handle.truncate(0)
            os.fsync(handle.fileno())
    elif operation == "replace":
        temp = path.with_name("linux-replacement")
        temp.write_bytes(b"BBBBBBBB")
        with temp.open("rb") as handle:
            os.fsync(handle.fileno())
        os.replace(temp, path)
    elif operation == "delete":
        path.unlink()
        return "OK deleted"
    elif operation in ("rename", "identity-rename"):
        target = path.with_name("mapped-moved" if operation == "rename" else "renamed")
        path.rename(target)
        return "OK " + describe(target)
    elif operation in ("prepare-save", "prepare-symlink"):
        path.chmod(0o751)  # (E) executable + distinct owner/group/other bits.
        if operation == "prepare-symlink":
            path.with_name("save-link").symlink_to(path.name)
    elif operation == "inspect-symlink":
        return "OK target=" + describe(path) + " link=" + describe(path.with_name("save-link"))
    elif operation == "inspect-streams":
        return "OK entries=" + repr(sorted(item.name for item in path.parent.iterdir())) + " file=" + describe(path)
    elif operation == "linux-lock":
        # Test native Windows byte-range lock interoperability, not flock.
        with path.open("r+b") as handle:
            try:
                fcntl.lockf(handle, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0)
            except OSError as error:
                if error.errno not in (errno.EACCES, errno.EAGAIN):
                    raise
                return f"OK linux_lock_blocked=1 errno={error.errno}"
            fcntl.lockf(handle, fcntl.LOCK_UN, 1, 0)
        return "OK linux_lock_blocked=0"
    elif operation == "prepare-names":
        names = {"a": b"lower", "A": b"UPPER", "a:b": b"colon", 'quote"': b"quote",
                 "trailing.": b"dot", "space ": b"space"}
        translations, windows_paths = [], []
        for name, contents in names.items():
            item = path.parent / name
            item.write_bytes(contents)
            mapped = subprocess.check_output(["wslpath", "-w", str(item)], text=True).rstrip("\r\n")
            windows_paths.append(mapped)
            translations.append(f"{name!r}={mapped!r}")
        path.with_name("name-map").write_text("\n".join(windows_paths) + "\n")
        return "OK wslpath=" + ";".join(translations)
    elif operation != "inspect-save":
        raise ValueError(f"unknown operation {operation}")
    return "OK " + describe(path)


def serve(base, stopped):
    ipc = base / ".ipc"
    seen = set()
    while not stopped.is_set():
        for ready in ipc.glob("*.ready"):
            if ready.name in seen:
                continue
            seen.add(ready.name)
            try:
                request = ready.with_suffix(".request")
                operation, relative = request.read_text().splitlines()
                reply = action(base, operation, relative)
            except Exception as error:  # Report action failures to the probe.
                reply = f"ERR {type(error).__name__} errno={getattr(error, 'errno', None)} {error}"
            response = ready.with_suffix(".response")
            temp = ready.with_suffix(".response.tmp")
            temp.write_text(reply)
            os.replace(temp, response)
        stopped.wait(0.005)  # (E) 5 ms helper polling; measured delays include it.


def main():
    if len(sys.argv) != 1:
        raise SystemExit("usage: tools/wsl_share_probe.sh")
    worktree = Path(__file__).resolve().parent.parent
    os.chdir(worktree)
    exe = worktree / "build/win/rel/tests/wsl_share_probe.exe"
    if not exe.exists():
        raise SystemExit("build first: taskset -c 48-63 nice -n 10 make TARGET=win -j16 win-wsl-share-probe")
    logs = worktree / "build/worker-logs"
    logs.mkdir(parents=True, exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix="wsl-share-probe-", dir=worktree / "build"))
    (base / ".ipc").mkdir()
    log_path = logs / (base.name + ".log")
    windows = subprocess.check_output(["wslpath", "-w", str(base)], text=True).strip()
    if windows.lower().startswith("\\\\wsl.localhost\\"):
        local = windows
        dollar = "\\\\wsl$\\" + windows[len("\\\\wsl.localhost\\"):]
    elif windows.lower().startswith("\\\\wsl$\\"):
        dollar = windows
        local = "\\\\wsl.localhost\\" + windows[len("\\\\wsl$\\"):]
    else:
        shutil.rmtree(base)
        raise SystemExit("requires a Linux worktree reached through a WSL UNC share")
    stopped = threading.Event()
    helper = threading.Thread(target=serve, args=(base, stopped), daemon=True)
    helper.start()
    try:
        with log_path.open("w") as log:
            def emit(line):
                print(line, flush=True)
                log.write(line + "\n")
                log.flush()
            emit("measurement | [AC, desktop Spiral] | Tobias using the box | " + subprocess.check_output(["uptime"], text=True).strip())
            battery = Path("/sys/class/power_supply/BAT0/status")
            emit("power | " + (battery.read_text().strip() if battery.exists() else "BAT0 absent; desktop AC per brief"))
            emit("kernel | " + os.uname().release)
            emit("helper | polling_ms=5 (E); notification delays include request publication/helper scheduling/observation")
            env = os.environ.copy()
            env["WINRUN_ISOLATION"] = "station"
            with subprocess.Popen(["tools/winrun.sh", str(exe), "--run", local, dollar, local + "\\.ipc"],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env) as process:
                for line in process.stdout:
                    emit(line.rstrip("\r\n"))
                result = process.wait()
            emit(f"wrapper | exit={result} (M)")
            emit("load-after | " + subprocess.check_output(["uptime"], text=True).strip())
    finally:
        stopped.set()
        helper.join()
        shutil.rmtree(base)
    shutil.copyfile(log_path, logs / "wsl-share-probe.log")
    print(f"evidence: {log_path.relative_to(worktree)}", flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())
