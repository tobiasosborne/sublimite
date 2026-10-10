#!/usr/bin/env python3
"""Strict offline join for P2.6. Never subtract marginal percentiles.

Editor CSV zero T4/T5 placeholders are filled from the recorded Present frame
serial, only when exactly one traced key dequeue in its injection interval
belongs to that serial. Failures, missing pairs and phase slips are errors.
"""
import argparse
import bisect
import csv
import math
import struct
import sys
from pathlib import Path

FIELDS = "pair_id,target,inject_ns,msc,t4_ns,t5_ns,t6_ns,frame_id,phase_ns,period_ns,actual_phase_ns".split(",")


def read_rows(path):
    with open(path, newline="", encoding="ascii") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != FIELDS:
            raise ValueError(f"{path}: wrong CSV header")
        rows = []
        for line, raw in enumerate(reader, 2):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError(f"{path}:{line}: truncated/extra fields")
            row = {key: value if key == "target" else int(value) for key, value in raw.items()}
            if any(value < 0 or value > (1 << 64) - 1 for key, value in row.items() if key != "target"):
                raise ValueError(f"{path}:{line}: integer out of range")
            if row["target"] not in ("reference", "editor"):
                raise ValueError(f"{path}:{line}: unknown target")
            rows.append(row)
    return rows


def read_trace(path):
    # The repository's native little-endian x86 EDTRACE1 contract, timing rings
    # only. Ignore the optional INPT tail; it does not supply frame identity.
    with open(path, "rb") as source:
        def take(size):
            data = source.read(size)
            if len(data) != size:
                raise ValueError(f"{path}: truncated trace")
            return data

        magic, version, rings, record_size, capacity = struct.unpack("<8sIIII", take(24))
        if magic != b"EDTRACE1" or version != 1 or rings > 16 or record_size != 16 or capacity != 65536:
            raise ValueError(f"{path}: unsupported trace header")
        frames, seen = {}, set()
        for _ in range(rings):
            ring, count = struct.unpack("<II", take(8))
            if ring >= 16 or ring in seen or count > capacity:
                raise ValueError(f"{path}: invalid trace ring")
            seen.add(ring)
            for _ in range(count):
                ns, frame, event, thread = struct.unpack("<QIHH", take(16))
                if thread != ring or event > 6 or not ns:
                    raise ValueError(f"{path}: invalid trace record")
                if frame:
                    frames.setdefault(frame, {}).setdefault(event, []).append(ns)
    return frames


def complete_editor(rows, frames):
    ordered = sorted(rows, key=lambda row: row["inject_ns"])
    dequeues = sorted((ns, frame) for frame, events in frames.items() for ns in events.get(1, []))
    times = [ns for ns, _ in dequeues]
    for index, row in enumerate(ordered):
        if row["target"] != "editor":
            continue
        lower = row["inject_ns"]
        upper = ordered[index + 1]["inject_ns"] if index + 1 < len(ordered) else (1 << 64)
        lo, hi = bisect.bisect_left(times, lower), bisect.bisect_left(times, upper)
        if hi - lo != 1 or dequeues[lo][1] != row["frame_id"]:
            raise ValueError(f"pair {row['pair_id']}: editor Present serial has no unique matching key frame")
        events = frames[row["frame_id"]]
        for event, column in ((4, "t4_ns"), (5, "t5_ns"), (6, "t6_ns")):
            if event not in events:
                raise ValueError(f"pair {row['pair_id']}: editor frame missing {column}")
            row[column] = min(events[event])


def pair_rows(rows, count, first, tolerance, synthetic_clock=False):
    grouped, previous = {}, {}
    for row in sorted(rows, key=lambda item: item["inject_ns"]):
        pair, target = row["pair_id"], row["target"]
        key = (pair, target)
        if key in grouped:
            raise ValueError(f"pair {pair}: duplicate {target} row")
        grouped[key] = row
        if not row["frame_id"] or not row["msc"] or not row["inject_ns"] or not row["period_ns"]:
            raise ValueError(f"pair {pair}: missing identity/clock")
        if row["phase_ns"] >= row["period_ns"] or row["actual_phase_ns"] >= row["period_ns"]:
            raise ValueError(f"pair {pair}: phase slipped outside its refresh")
        if row["t4_ns"] < row["inject_ns"] or row["t5_ns"] < row["t4_ns"] or row["t6_ns"] < row["t4_ns"]:
            raise ValueError(f"pair {pair}: missing/nonmonotonic endpoints")
        if target in previous:
            prev = previous[target]
            if row["inject_ns"] <= prev["inject_ns"] or row["msc"] < prev["msc"] or row["frame_id"] <= prev["frame_id"]:
                raise ValueError(f"pair {pair}: target sequence went backwards")
            if any(row[column] <= prev[column] for column in ("t4_ns", "t5_ns", "t6_ns")):
                raise ValueError(f"pair {pair}: target endpoints went backwards")
        previous[target] = row
    expected = {(pair, target) for pair in range(first, first + count) for target in ("reference", "editor")}
    if grouped.keys() != expected:
        raise ValueError(f"incomplete run: expected {count} pairs; missing={sorted(expected-grouped.keys())[:8]}, extra={sorted(grouped.keys()-expected)[:8]}")
    differences = []
    for pair in range(first, first + count):
        ref, editor = grouped[pair, "reference"], grouped[pair, "editor"]
        if ref["phase_ns"] != editor["phase_ns"]:
            raise ValueError(f"pair {pair}: requested phases differ")
        # Independent measurements may round/jitter slightly. 1% is a setup
        # rate tolerance, not the G2c latency gate. Never accept 90/60 Hz pairs.
        low = min(ref["period_ns"], editor["period_ns"])
        if not synthetic_clock and abs(ref["period_ns"] - editor["period_ns"]) > low // 100:
            raise ValueError(f"pair {pair}: measured target refresh periods differ (place both on the same output)")
        if abs(ref["actual_phase_ns"] - editor["actual_phase_ns"]) > tolerance:
            raise ValueError(f"pair {pair}: actual phases differ by more than {tolerance} ns")
        diff = {"pair_id": pair}
        for stage in ("t4", "t5", "t6"):
            diff[f"{stage}_editor_minus_reference_ns"] = ((editor[f"{stage}_ns"] - editor["inject_ns"]) -
                                                        (ref[f"{stage}_ns"] - ref["inject_ns"]))
        differences.append(diff)
    return differences


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("injections", type=Path)
    parser.add_argument("--reference-frames", type=Path, help="optional refwin CSV; injectors using --wait-reference already have endpoints")
    parser.add_argument("--editor-trace", type=Path)
    parser.add_argument("--synthetic-clock", action="store_true", help="Xvfb TRACK only: skip measured rate comparison")
    parser.add_argument("--pairs", type=int, required=True, help="expected count; truncated runs must fail")
    parser.add_argument("--first-pair", type=int, default=1)
    parser.add_argument("--phase-tolerance-ns", type=int, required=True, help="coordinator-selected matching tolerance; never an optical gate")
    parser.add_argument("--csv", type=Path, required=True, help="complete joined timestamp CSV")
    parser.add_argument("--differences", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.pairs < 1 or args.pairs > 1000000 or args.first_pair < 1 or args.phase_tolerance_ns < 0:
            raise ValueError("invalid count/id/tolerance")
        rows = read_rows(args.injections)
        if args.reference_frames:
            ref_rows = read_rows(args.reference_frames)
            refs = {(row["pair_id"], row["target"]): row for row in ref_rows}
            if len(refs) != len(ref_rows) or any(row["target"] != "reference" for row in ref_rows):
                raise ValueError("duplicate/non-reference row in reference frames")
            for row in rows:
                if row["target"] == "reference":
                    ref = refs.pop((row["pair_id"], "reference"))
                    if any(row[key] != ref[key] for key in ("inject_ns", "msc", "phase_ns", "period_ns", "actual_phase_ns")):
                        raise ValueError("reference injection metadata mismatch")
                    if any(row[key] and row[key] != ref[key] for key in ("frame_id", "t4_ns", "t5_ns", "t6_ns")):
                        raise ValueError("reference endpoint mismatch")
                    row.update(ref)
            if refs:
                raise ValueError("unmatched reference frames")
        if args.editor_trace:
            complete_editor(rows, read_trace(args.editor_trace))
        differences = pair_rows(rows, args.pairs, args.first_pair, args.phase_tolerance_ns, args.synthetic_clock)
        # Validate the whole run before creating either output file.
        with open(args.csv, "w", newline="", encoding="ascii") as output:
            writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(sorted(rows, key=lambda row: (row["pair_id"], row["target"])))
        with open(args.differences, "w", newline="", encoding="ascii") as output:
            writer = csv.DictWriter(output, fieldnames=list(differences[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(differences)
        if args.synthetic_clock:
            print("TRACK SYNTHETIC-CLOCK: rate comparison disabled; cannot establish real vblank/G2c")
        for stage in ("t4", "t5", "t6"):
            values = sorted(row[f"{stage}_editor_minus_reference_ns"] for row in differences)
            p50 = values[math.ceil(len(values) * 0.50) - 1]
            p99 = values[math.ceil(len(values) * 0.99) - 1]
            print(f"TRACK paired {stage}: n={len(values)} p50={p50} ns p99={p99} ns (M); T6 is Present completion, not optical")
        return 0
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f"refwin_pairs: FAIL {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
