#!/usr/bin/env python3
"""Audit the retained raw experiment; never launch or remeasure anything."""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1] if len(sys.argv) > 1 else "variants/P4.14/results.log")
lines = source.read_text().splitlines()
records = [dict(re.findall(r"(\w+)=([^ ]+)", line)) for line in lines]
rows = [record for line, record in zip(lines, records) if line.startswith("SAMPLE ")]
assert len(rows) == 600, f"expected 600 complete rows, got {len(rows)}"
assert [row["variant"] for row in rows] == list("abc") * 200
assert all(int(row["round"]) == i // 3 for i, row in enumerate(rows))
assert all(int(row["map_ns"]) <= int(row["ready_map_ns"]) <= int(row["present_ns"]) <=
           int(row["complete_ns"]) for row in rows)
by_variant = {variant: [row for row in rows if row["variant"] == variant] for variant in "abc"}
metrics = {"exec_map_requested": "map_ns", "exec_ready_and_map": "ready_map_ns",
           "exec_first_present_request": "present_ns", "exec_first_present_complete": "complete_ns"}
for line, record in zip(lines, records):
    if not line.startswith("BENCH "):
        continue
    values = sorted(int(row[metrics[record["name"]]]) for row in by_variant[record["variant"]])
    assert int(record["n"]) == 200
    assert int(record["p50_ns"]) == values[99]
    assert int(record["p99_ns"]) == values[197]
print("First-frame audit GREEN: PASS 600/600 ordered rows; 200 per variant; strict A/B/C order")
print("Summary audit: PASS all 12 percentile rows match raw nearest-rank samples")
loads = [float(row["load1"]) for row in rows]
power = sorted({row["power"] for row in rows})
# Regex values end at a space; full Not charging state remains in original log.
assert all(row["evidence"] == "(M)[AC]" for row in rows)
stamp = f"(M)[AC], Not charging, load1={min(loads):.2f}..{max(loads):.2f}"
print(f"Launch interval={(int(rows[-1]['start_ns']) - int(rows[0]['start_ns'])) / 1e9:.9f}s {stamp}")
for metric in metrics.values():
    deltas = sorted(int(a[metric]) - int(c[metric]) for a, c in zip(by_variant['a'], by_variant['c']))
    print(f"Paired A-C {metric}: p50_ns={deltas[99]} p99_ns={deltas[197]} "
          f"C_faster_pairs={sum(delta > 0 for delta in deltas)}/200 {stamp}")
for variant, observations in by_variant.items():
    for metric in ("minor_delta", "major_delta", "rss_kib"):
        values = sorted(int(row[metric]) for row in observations)
        print(f"variant={variant} {metric}: p50={values[99]} p99={values[197]} max={values[-1]} {stamp}")
