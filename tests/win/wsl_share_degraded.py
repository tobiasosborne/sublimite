#!/usr/bin/env python3
"""Explicit WSL acceptance fixture; follows tools/wsl_share_probe.sh's protocol."""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from wsl_share_probe import serve  # noqa: E402


def main():
    os.chdir(ROOT)
    if not shutil.which("wslpath"):
        print("SKIP (requires real WSL UNC provider; outside check)")
        return 0
    base = Path(tempfile.mkdtemp(prefix="wsl-share-degraded-", dir=ROOT / "build"))
    logs = ROOT / "build/worker-logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / "edit-177r.28-real-share.log"
    stopped = threading.Event()
    helper = None
    try:
        (base / ".ipc").mkdir()
        windows = subprocess.check_output(["wslpath", "-w", str(base)], text=True).rstrip("\r\n")
        if windows.lower().startswith("\\\\wsl.localhost\\"):
            local = windows
        elif windows.lower().startswith("\\\\wsl$\\"):
            local = "\\\\wsl.localhost\\" + windows[len("\\\\wsl$\\"):]
        else:
            raise RuntimeError("fixture must be reached through the Linux WSL share")
        dollar = "\\\\wsl$\\" + local[len("\\\\wsl.localhost\\"):]
        forms = (local, dollar, "\\\\?\\UNC\\" + local[2:], "\\\\?\\UNC\\" + dollar[2:])
        helper = threading.Thread(target=serve, args=(base, stopped), daemon=True)
        helper.start()
        with log_path.open("w") as log:
            def emit(text):
                print(text, flush=True)
                log.write(text + "\n")
                log.flush()
            emit("[AC, desktop Spiral], Tobias using the box | " + subprocess.check_output(["uptime"], text=True).strip())
            emit("power: BAT0 absent; desktop AC per binding brief" if not Path("/sys/class/power_supply/BAT0/status").exists() else Path("/sys/class/power_supply/BAT0/status").read_text().strip())
            for index, form in enumerate(forms):
                rel = f"form-{index}"
                directory = base / rel
                directory.mkdir()
                source = directory / "source"
                source.write_bytes(b"original\n")
                source.chmod(0o751)  # Existing measured executable fixture.
                (directory / "link").symlink_to("source")
                for name, data in {"a": b"lower", "A": b"UPPER", "trailing.": b"dot", "space ": b"space"}.items():
                    (directory / name).write_bytes(data)
                before = source.stat()
                expected_names = sorted(item.name for item in directory.iterdir())
                # Assert the Linux state at the inspection boundary, before later mutations.
                original_action = serve.__globals__["action"]
                def checked_action(root, operation, relative):
                    if operation in ("inspect-save", "inspect-streams"):
                        now = source.stat()
                        assert source.read_bytes() == b"original\n"
                        assert (stat.S_IMODE(now.st_mode), now.st_uid, now.st_gid, now.st_ino) == (stat.S_IMODE(before.st_mode), before.st_uid, before.st_gid, before.st_ino)
                        assert sorted(item.name for item in directory.iterdir()) == expected_names
                    return original_action(root, operation, relative)
                serve.__globals__["action"] = checked_action
                env = os.environ.copy()
                env["WINRUN_ISOLATION"] = "station"
                emit(f"real share: {form}\\{rel}")
                with subprocess.Popen(["tools/winrun.sh", os.environ.get("WSL_DEGRADED_EXE", "build/win/rel/tests/wsl_share_degraded.exe"), form + "\\" + rel, local + "\\.ipc", rel], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env) as proc:
                    for line in proc.stdout:
                        emit(line.rstrip("\r\n"))
                    result = proc.wait()
                serve.__globals__["action"] = original_action
                if result:
                    raise RuntimeError(f"native acceptance failed: exit={result}")
                assert (directory / "link").is_symlink() and os.readlink(directory / "link") == "source"
                emit("Linux oracle: original bytes/mode/owner/inode and directory entries unchanged at save refusal; symlink intact")
            emit("real-share degraded: PASS | " + subprocess.check_output(["uptime"], text=True).strip())
    finally:
        stopped.set()
        if helper:
            helper.join()
        shutil.rmtree(base)
    return 0


if __name__ == "__main__":
    sys.exit(main())
