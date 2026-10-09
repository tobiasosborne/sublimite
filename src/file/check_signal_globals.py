#!/usr/bin/env python3
"""Check the proposed single process SIGBUS-service exception in a file object."""
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
ok = len(mutable) <= 1
print(f"finding10: {'ok' if ok else 'FAIL'} mutable globals={len(mutable)} "
      + ",".join(mutable))
sys.exit(0 if ok else 1)
