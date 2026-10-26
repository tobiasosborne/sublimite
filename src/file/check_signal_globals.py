#!/usr/bin/env python3
"""Check the sanctioned SIGBUS and inotify process services in a file object."""
import subprocess
import sys

symbols = subprocess.check_output(
    ["nm", "--defined-only", sys.argv[1]], text=True
).splitlines()
mutable = [
    fields[2]
    for line in symbols
    if len(fields := line.split()) == 3 and fields[1] in ("b", "B", "d", "D")
]
# os_fault_service: the OS-layer fault service (edit-177r.2, Tobias 2026-10-25); file_bus goes when W2 migrates src/file.
# os_win_state: the Windows OS layer's write-once caches (edit-177r.3); never in a Linux object, listed for lockstep.
ok = set(mutable) <= {"file_bus", "file_watches", "os_fault_service", "os_win_state"}
print(f"finding10: {'ok' if ok else 'FAIL'} mutable globals={len(mutable)} "
      + ",".join(mutable))
sys.exit(0 if ok else 1)
