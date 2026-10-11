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
ok = set(mutable) <= {"file_bus", "file_watches"}
print(f"finding10: {'ok' if ok else 'FAIL'} mutable globals={len(mutable)} "
      + ",".join(mutable))
sys.exit(0 if ok else 1)
