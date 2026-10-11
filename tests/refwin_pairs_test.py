"""Display-independent coverage of the offline EDTRACE1 join."""
import csv
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import refwin_pairs


class TraceJoinTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.trace = self.directory / "editor.trace"
        # Synthetic endpoints: reference latency = 10/20/30 ns; editor =
        # 15/25/35 ns. Every paired difference must therefore equal 5 ns.
        self.pipeline = [(205, 9, 1, 0), (215, 9, 4, 0),
                         (225, 9, 5, 0), (235, 9, 6, 0)]
        self.expected = {9: {1: [205], 4: [215], 5: [225], 6: [235]}}

    def dump(self, records):
        # Sizes/capacity come from src/trace/trace_fmt.h's EDTRACE1 layout.
        self.trace.write_bytes(struct.pack("<8sIIIIII", b"EDTRACE1", 1, 1,
                                          16, 65536, 0, len(records)) +
                               b"".join(struct.pack("<QIHH", *r) for r in records))

    def test_cli_startup_records(self):
        # Reproduce the reported startup IDs, both unframed (real dumps) and
        # attached to the key frame (must still be ignored by the join).
        self.dump([(event, frame, event, 0) for event in range(10, 19)
                   for frame in (0, 9)] + self.pipeline)
        injections = self.directory / "injections.csv"
        rows = [dict(pair_id=1, target="reference", inject_ns=100, msc=1,
                     t4_ns=110, t5_ns=120, t6_ns=130, frame_id=1,
                     phase_ns=0, period_ns=100, actual_phase_ns=0),
                dict(pair_id=1, target="editor", inject_ns=200, msc=1,
                     t4_ns=0, t5_ns=0, t6_ns=0, frame_id=9,
                     phase_ns=0, period_ns=100, actual_phase_ns=0)]
        with injections.open("w", newline="", encoding="ascii") as out:
            writer = csv.DictWriter(out, fieldnames=refwin_pairs.FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        joined, differences = self.directory / "joined.csv", self.directory / "differences.csv"
        result = subprocess.run(
            [sys.executable, "-B", str(ROOT / "tools/refwin_pairs.py"), str(injections),
             "--editor-trace", str(self.trace), "--pairs", "1",
             "--phase-tolerance-ns", "0", "--csv", str(joined),
             "--differences", str(differences)],
            cwd=self.directory, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        completed = {row["target"]: row for row in refwin_pairs.read_rows(joined)}
        self.assertEqual(completed["reference"], rows[0])
        self.assertEqual([completed["editor"][key] for key in ("t4_ns", "t5_ns", "t6_ns")],
                         [215, 225, 235])
        with differences.open(newline="", encoding="ascii") as source:
            self.assertEqual(list(csv.DictReader(source)),
                             [dict(pair_id="1", t4_editor_minus_reference_ns="5",
                                   t5_editor_minus_reference_ns="5",
                                   t6_editor_minus_reference_ns="5")])

    def test_legacy_pipeline(self):
        self.dump(self.pipeline)
        self.assertEqual(refwin_pairs.read_trace(self.trace), self.expected)

    def test_all_unpaired_events_are_skipped(self):
        events = refwin_pairs.read_event_ids(refwin_pairs.TRACE_HEADER)
        unpaired = set(events.values()) - {1, 4, 5, 6}
        self.dump([(100, frame, event, 0) for event in unpaired for frame in (0, 9)] +
                  self.pipeline)
        self.assertEqual(refwin_pairs.read_trace(self.trace), self.expected)

    def test_future_enum_events_and_gaps(self):
        header = self.directory / "trace.h"
        source = refwin_pairs.TRACE_HEADER.read_text(encoding="ascii")
        # A hypothetical sparse append tests explicit values, C suffixes,
        # implicit increments and comments without changing src/trace.
        header.write_text(source.replace("    TRACE_EV_COUNT",
                          "    TRACE_FUTURE = 0x2aU, /* explicit gap */\n"
                          "    TRACE_FUTURE_NEXT, // implicit successor\n"
                          "    TRACE_EV_COUNT"), encoding="ascii")
        with mock.patch.object(refwin_pairs, "TRACE_HEADER", header):
            events = refwin_pairs.read_event_ids(header)
            self.assertEqual(events["TRACE_FUTURE"], 42)
            self.assertEqual(events["TRACE_FUTURE_NEXT"], 43)
            self.assertNotIn("TRACE_EV_COUNT", events)
            self.dump(self.pipeline + [(100, 9, events["TRACE_FUTURE"], 0),
                                       (101, 0, events["TRACE_FUTURE_NEXT"], 0)])
            self.assertEqual(refwin_pairs.read_trace(self.trace), self.expected)
            # The predecessor of the explicit append lies in the enum gap.
            self.dump(self.pipeline + [(100, 9, events["TRACE_FUTURE"] - 1, 0)])
            with self.assertRaisesRegex(ValueError, "invalid trace record"):
                refwin_pairs.read_trace(self.trace)

    def test_malformed_records_still_fail(self):
        events = refwin_pairs.read_event_ids(refwin_pairs.TRACE_HEADER)
        for record in [(100, 9, max(events.values()) + 1, 0),
                       (100, 9, 65535, 0), (100, 9, 1, 1), (0, 9, 1, 0)]:
            with self.subTest(record=record):
                self.dump(self.pipeline + [record])
                with self.assertRaisesRegex(ValueError, "invalid trace record"):
                    refwin_pairs.read_trace(self.trace)
        self.dump(self.pipeline)
        self.trace.write_bytes(self.trace.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, "truncated trace"):
            refwin_pairs.read_trace(self.trace)


if __name__ == "__main__":
    unittest.main(verbosity=2)
