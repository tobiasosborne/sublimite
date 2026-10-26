#!/usr/bin/env python3
"""Check sanctioned fault and notification services in a module object."""
import subprocess
import sys

symbols = subprocess.check_output(
    ["nm", "--defined-only", sys.argv[1]], text=True
).splitlines()
mutable = [
    fields[2]
    for line in symbols
    if len(fields := line.split()) == 3 and fields[1] in ("b", "B", "d", "D")
    # COFF nm also lists these data sections; they are not C globals.
    and fields[2] not in (".bss", ".data")
]
# os_fault_service is the sole fault installer (edit-177r.2 / edit-pf3l).
# file_watches remains the UI-owned shared notification service.
# os_win_state: the Windows OS layer's write-once caches (edit-177r.3); never in a Linux object, listed for lockstep.
ok = set(mutable) <= {"file_watches", "os_fault_service", "os_win_state"}
print(f"finding10: {'ok' if ok else 'FAIL'} mutable globals={len(mutable)} "
      + ",".join(mutable))
sys.exit(0 if ok else 1)
