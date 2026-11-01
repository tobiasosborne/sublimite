#!/usr/bin/env python3
"""Bounded Windows suites; each suite owns its release/sanitizer passes."""
import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, required=True)
    parser.add_argument("binaries", nargs="+")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    logs = Path("build/win/check-logs")
    logs.mkdir(parents=True, exist_ok=True)
    groups = defaultdict(list)
    for binary in args.binaries:
        groups[Path(binary).name].append(binary)
    running = set()
    lock = threading.Lock()
    stopping = threading.Event()

    def interrupt(signum, _frame):
        stopping.set()
        with lock:
            for process in running:
                process.terminate()  # Our WSL relay; its Windows job kills descendants.
        raise InterruptedError(f"signal {signum}")

    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGINT, interrupt)

    def run(binary, mode, extra=()):
        label = f"{Path(binary).parts[-3]}.{Path(binary).stem}.{mode}"
        if extra:
            label += ".negative"
        path = logs / (label + ".log")
        env = dict(os.environ, WINRUN_ISOLATION=mode)
        start = time.monotonic()
        with path.open("w") as output:
            with lock:
                if stopping.is_set():
                    raise InterruptedError("check stopped")
                process = subprocess.Popen(["tools/winrun.sh", binary, *extra],
                                           stdout=output, stderr=subprocess.STDOUT, env=env)
                running.add(process)
            try:
                rc = process.wait()
            finally:
                with lock:
                    running.discard(process)
        content = path.read_text(errors="replace")
        seconds = time.monotonic() - start
        negative = bool(extra)
        passed = rc != 0 and "AddressSanitizer" in content if negative else rc == 0
        return label, path, content, passed, rc, seconds

    def suite(binaries):
        results = []
        for binary in binaries:
            results.append(run(binary, "station"))
            # Clipboard guards are in before this input-station pass. D3D's
            # desktop pass exercises real native occlusion, station hiddenness.
            if Path(binary).name in ("win32_plat_test.exe", "quiet_run_test.exe", "d3d11_test.exe"):
                results.append(run(binary, "desktop"))
            if binary.endswith("/san/tests/smoke_test.exe"):
                results.append(run(binary, "station", ("--expect-asan",)))
        return results

    skips = Counter()
    failed = 0
    start = time.monotonic()
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(suite, binaries) for binaries in groups.values()]
            for future in as_completed(futures):
                for label, path, content, passed, rc, seconds in future.result():
                    print(f"== {label}", flush=True)
                    print(content, end="" if content.endswith("\n") else "\n", flush=True)
                    print(f"RESULT {label} exit={rc} {'PASS' if passed else 'FAIL'} "
                          f"seconds={seconds:.3f} log={path}", flush=True)
                    failed += not passed
                    if not label.endswith(".negative"):
                        for line in content.splitlines():
                            match = re.search(r"\bSKIP\s+(.+)", line)
                            if match:
                                reason = match[1]
                                if reason.startswith("("):
                                    reason = reason[1:reason.index(")")]
                                skips[reason] += 1
    except InterruptedError as error:
        print(f"Windows check interrupted: {error}", flush=True)
        return 1
    # Preserve the existing integration gaps in check output and its skip census.
    gaps = (
        "editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)",
        "src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test",
    )
    for reason in gaps:
        print(f"Windows journal: SKIP {reason}", flush=True)
        skips[reason] += 1
    summary = "; ".join(f"{reason}={count}" for reason, count in sorted(skips.items())) or "none"
    print(f"Windows check: binaries={len(args.binaries)} desktop-passes="
          f"{sum(Path(b).name in ('win32_plat_test.exe', 'quiet_run_test.exe', 'd3d11_test.exe') for b in args.binaries)} "
          f"jobs={args.jobs} seconds={time.monotonic()-start:.3f} failures={failed}; "
          f"SKIP cases={sum(skips.values())}: {summary}", flush=True)
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
