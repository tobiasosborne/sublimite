#!/usr/bin/env python3
"""Audit the retained raw experiment; never launch or remeasure anything."""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1] if len(sys.argv) > 1 else "variants/P4.14/results.log")
lines = source.read_text().splitlines()
records = [dict(re.findall(r"(\w+)=([^ ]+)", line)) for line in lines]
rows = [record for line, record in zip(lines, records) if line.startswith("SAMPLE ")]
modern = any("device_ns" in row for row in rows)
variants = "abcd" if modern else "abc"
assert rows, "no complete samples"
n = sum(row["variant"] == "a" for row in rows)
assert len(rows) == len(variants) * n
expected = list("abc") * n + (["d"] * n if modern else [])
assert [row["variant"] for row in rows] == expected, "not ABC interleaved then separate D"
for variant in variants:
    assert [int(row["round"]) for row in rows if row["variant"] == variant] == list(range(n))
assert all(int(row["map_ns"]) <= int(row["ready_map_ns"]) <= int(row["present_ns"]) <=
           int(row["complete_ns"]) for row in rows)
if modern:
    real = any("real_display=1" in line for line in lines if line.startswith("ESTIMATE "))
    for row in rows:
        assert int(row["present_ns"]) <= int(row["device_ns"]) <= int(row["complete_ns"])
        frames = [int(row[name]) for name in ("ready_frame", "present_frame", "device_frame", "complete_frame")]
        assert frames[0] > 0 and len(set(frames)) == 1
        if real and row["variant"] in "bd":
            assert row["backend"] == "native_gl", "real GL row fell back"
        if row["backend"] == "native_gl":
            assert int(row["gl_result"]) == 0 and int(row["displayed_msc"]) > 0
        assert row["evidence"] in ("(M)[AC]", "(M)[bat]") and float(row["load1"]) >= 0
        assert int(row["width"]) > 0 and int(row["height"]) > 0
    devices = [line for line in lines if line.startswith("DEVICE ")]
    assert len(devices) == 4 and all('device="' in line and 'driver="' in line for line in devices)
    for variant in "bc":
        assert any(line.startswith(f"MEMORY variant={variant} Rss:") for line in lines)
by_variant = {variant: [row for row in rows if row["variant"] == variant] for variant in variants}
metrics = {"exec_map_requested": "map_ns", "exec_ready_and_map": "ready_map_ns",
           "exec_cpu_submit_ready_and_map": "ready_map_ns", "exec_device_done": "device_ns",
           "exec_first_present_request": "present_ns", "exec_first_present_complete": "complete_ns"}
summary_count = 0
for line, record in zip(lines, records):
    if not line.startswith("BENCH "):
        continue
    values = sorted(int(row[metrics[record["name"]]]) for row in by_variant[record["variant"]])
    assert int(record["n"]) == n
    assert int(record["p50_ns"]) == values[(n + 1) // 2 - 1]
    assert int(record["p99_ns"]) == values[(99 * n + 99) // 100 - 1]
    summary_count += 1
assert summary_count == (20 if modern else 12)
print(f"First-frame audit GREEN: PASS {len(rows)}/{len(rows)} ordered rows; {n} per variant; "
      f"strict A/B/C order{' then D' if modern else ''}")
print(f"Summary audit: PASS all {summary_count} percentile rows match raw nearest-rank samples")
loads = [float(row["load1"]) for row in rows]
power = sorted({row["power"] for row in rows})
# Regex values end at a space; full Not charging state remains in original log.
tags = sorted({row["evidence"] for row in rows})
stamp = f"{'/'.join(tags)}, power=see raw rows, load1={min(loads):.2f}..{max(loads):.2f}"
print(f"Launch interval={(int(rows[-1]['start_ns']) - int(rows[0]['start_ns'])) / 1e9:.9f}s {stamp}")
for metric in dict.fromkeys(metrics.values()):
    if metric not in rows[0]:
        continue
    deltas = sorted(int(a[metric]) - int(c[metric]) for a, c in zip(by_variant['a'], by_variant['c']))
    print(f"Paired A-C {metric}: p50_ns={deltas[(n + 1) // 2 - 1]} p99_ns={deltas[(99 * n + 99) // 100 - 1]} "
          f"C_faster_pairs={sum(delta > 0 for delta in deltas)}/{n} {stamp}")
for variant, observations in by_variant.items():
    for metric in ("minor_delta", "major_delta", "rss_kib"):
        values = sorted(int(row[metric]) for row in observations)
        print(f"variant={variant} {metric}: p50={values[(n + 1) // 2 - 1]} p99={values[(99 * n + 99) // 100 - 1]} max={values[-1]} {stamp}")
