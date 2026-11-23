"""Conformance tests for the inspector wire, with independent hex vectors."""

import sys
from pathlib import Path

if not __package__:
    sys.path[0] = str(Path(__file__).resolve().parents[1])

import unittest
from dataclasses import replace
from mmap import mmap
from struct import calcsize, pack, pack_into

from contract import records
from contract import snapwire as wire


VECTORS = Path(__file__).with_name("vectors")
WORD = calcsize("<I")
MAGIC_SIZE = len(b"SCEL")
FRAME_HEADER_WORDS = 2 + 6 + 4 + 1 + 4 + 2  # witness/serial, sizes, rects, Q8, counts/flags
CELLS = 1 + FRAME_HEADER_WORDS  # skip the payload frame count and frame header
FIRST_CELL = CELLS + 1  # skip the cell count
TAB = 4 * WORD + WORD  # rectangle and active flag
# single-payload.hex ends with two tabs after their count, then the bottom-row rectangle.
BOTTOM_ROW_TAIL = 4 * WORD
TABS_TAIL = WORD + 2 * TAB + BOTTOM_ROW_TAIL


def vector(name):
    lines = (VECTORS / (name + ".hex")).read_text(encoding="ascii").splitlines()
    return bytes.fromhex(" ".join(line.split("#", 1)[0] for line in lines))


def word_at(data, offset, value):
    changed = bytearray(data)
    pack_into("<I", changed, offset, value)
    return bytes(changed)


class WireTest(unittest.TestCase):
    def setUp(self):
        self.packet = vector("single-reply")
        self.payload = vector("single-payload")
        self.snap = wire.Request(wire.Operation.SNAP, ticket=41)  # ticket from the vector

    def test_envelopes(self):
        self.assertEqual(wire.encode_request(self.snap), (1, 1, 41, 0, 0))
        self.assertEqual(wire.encode_request(wire.Request(wire.Operation.NEXT, 42, 6)),
                         (1, 2, 42, 6, 0))
        self.assertEqual(wire.encode_request(wire.Request(wire.Operation.ARM, 41)),
                         (1, 0, 41, 0, 0))
        for reason, request in (("invalid request ticket", replace(self.snap, ticket=0)),
                                ("invalid request ticket", replace(self.snap, ticket=1 << (WORD * 8))),
                                ("only used by NEXT", replace(self.snap, previous_serial=6)),
                                ("unknown operation 3", replace(self.snap, operation=3))):
            with self.subTest(request=request), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.encode_request(request)

    def test_single_frame(self):
        reply = wire.decode_reply(self.packet, self.payload, self.snap)
        self.assertEqual((reply.ticket, reply.submit_serial, reply.result), (41, 7, wire.Result.OK))
        frame = reply.snapshot.settled
        self.assertEqual(frame.witness, wire.Witness.SETTLED)
        self.assertEqual(frame.cell_text(1, 0, 4), "A界 ")
        self.assertEqual(frame.cell_text(1, 2, 3), "")  # the continuation has no extra text
        self.assertEqual(frame.drawn_caret_cells(), ((1, 1),))
        self.assertEqual(frame.cells[frame.columns + 3].attributes, wire.Attribute.UNDERLINE)
        self.assertEqual(frame.visible_tab_titles(), ("L", "R…"))
        self.assertEqual(frame.active_tab, wire.Tab(wire.Rect(3, 0, 2, 1), True))
        self.assertEqual(frame.bottom_row_text(), "ready")

    def test_unresolved_cells_fail_only_reads_that_cross_them(self):
        frame = wire.decode_payload(self.payload).settled
        self.assertEqual(frame.cells[2 * frame.columns - 1].kind, wire.CellKind.UNRESOLVED)
        with self.assertRaises(wire.UnresolvedCellError):
            frame.cell_text(1)
        with self.assertRaises(IndexError):
            frame.cell_text(1, 0, frame.columns + 1)

    def test_two_frames_with_signed_translation_and_minimap(self):
        packet, payload = vector("two-reply"), vector("two-payload")
        snap = wire.decode_reply(packet, payload, self.snap, after_serial=5,
                                 require_first_action=True).snapshot
        self.assertEqual(tuple(frame.submit_serial for frame in snap.frames), (6, 7))
        self.assertEqual(snap.first_action.scroll_q8, -(1 << 7))  # half a pixel in Q8
        self.assertEqual(snap.first_action.cell_text(1), "y")  # the extra row
        self.assertEqual(snap.first_action.visible_tab_titles(), ())
        self.assertIsNone(snap.first_action.active_tab)
        self.assertEqual(snap.first_action.bottom_row_text(), "")
        self.assertEqual(snap.first_action.minimap_box, wire.Rect(6, 1, 4, 4))
        self.assertEqual(snap.first_action.minimap_ink_intervals, ((1, 3), (5, 6)))

    def test_missing_first_action(self):
        with self.assertRaises(wire.MissingWitnessError):
            wire.decode_payload(self.payload).first_action
        with self.assertRaises(wire.MissingWitnessError):
            wire.decode_reply(self.packet, self.payload, self.snap, require_first_action=True)

    def test_one_submit_can_supply_both_witnesses(self):
        # The first natural action submit can also be the first submit after the fence.
        settled = self.payload[WORD:]
        first = word_at(settled, 0, wire.Witness.FIRST_ACTION)
        payload = pack("<I", 2) + first + settled
        packet = word_at(self.packet, MAGIC_SIZE + 4 * WORD, len(payload))
        snap = wire.decode_reply(packet, payload, self.snap, require_first_action=True).snapshot
        self.assertEqual(snap.first_action.submit_serial, snap.settled.submit_serial)

    def test_unprovided_sections(self):
        frame = wire.decode_payload(self.payload).settled
        for name in ("minimap", "minimap_box", "minimap_ink_intervals"):
            with self.subTest(name=name), self.assertRaises(wire.UnprovidedSectionError):
                getattr(frame, name)
        # Clearing all bits omits all section bodies. A read fails even for an empty range.
        header = word_at(self.payload[:CELLS * WORD], (CELLS - 1) * WORD, 0)
        frame = wire.decode_payload(header).settled
        for accessor in (lambda: frame.cells, lambda: frame.cell_text(0, 0, 0),
                         frame.drawn_caret_cells, lambda: frame.tabs, frame.visible_tab_titles,
                         lambda: frame.active_tab, lambda: frame.bottom_row, frame.bottom_row_text):
            with self.subTest(accessor=accessor), self.assertRaises(wire.UnprovidedSectionError):
                accessor()

    def test_titles_and_bottom_text_require_cells(self):
        frame = wire.decode_payload(self.payload).settled
        # Clear the cells bit and drop the cells section; keep tabs and the bottom row.
        sections = wire.Section.TABS | wire.Section.BOTTOM_ROW
        header = word_at(self.payload[:CELLS * WORD], (CELLS - 1) * WORD, sections)
        no_cells = wire.decode_payload(header + self.payload[-TABS_TAIL:]).settled
        self.assertEqual(no_cells.active_tab, frame.active_tab)
        for accessor in (no_cells.visible_tab_titles, no_cells.bottom_row_text):
            with self.assertRaises(wire.UnprovidedSectionError):
                accessor()

    def test_next_reply(self):
        request = wire.Request(wire.Operation.NEXT, ticket=42, previous_serial=6)
        reply = wire.decode_reply(vector("next-reply"), self.payload, request)
        self.assertEqual(reply.snapshot.settled.submit_serial, 7)
        with self.assertRaisesRegex(wire.ProtocolError, "serial"):
            wire.decode_reply(vector("next-reply"), self.payload,
                              replace(request, previous_serial=7))

    def test_arm_reply_and_v1_rejection(self):
        request = replace(self.snap, operation=wire.Operation.ARM)
        packet = pack("<4s5I", b"SCEL", 1, request.ticket, 1, 0, 0)
        self.assertIsNone(wire.decode_reply(packet, b"", request).snapshot)
        with self.assertRaisesRegex(wire.ProtocolError, "ARMED"):
            wire.decode_reply(packet, b"", self.snap)
        with self.assertRaises(wire.ReplyError) as failure:
            wire.decode_reply(word_at(packet, MAGIC_SIZE + 2 * WORD, 5), b"", request)
        self.assertEqual(failure.exception.result, wire.Result.INVALID)

    def test_bad_packet_header(self):
        for reason, packet in (
            ("magic", b"FAIL" + self.packet[MAGIC_SIZE:]),
            ("revision", word_at(self.packet, MAGIC_SIZE, 0)),
            ("ticket", word_at(self.packet, MAGIC_SIZE + WORD, 42)),
            ("result", word_at(self.packet, MAGIC_SIZE + 2 * WORD, 6)),  # one after INVALID
            ("packet length", self.packet[:-1]),
            ("packet length", self.packet + b"\0"),
        ):
            with self.subTest(reason=reason), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.decode_reply(packet, self.payload, self.snap)

    def test_old_or_mismatched_serial(self):
        for after in (7, 8):  # same as, then later than, the acknowledged serial
            with self.assertRaisesRegex(wire.ProtocolError, "serial"):
                wire.decode_reply(self.packet, self.payload, self.snap, after_serial=after)
        packet = word_at(self.packet, MAGIC_SIZE + 3 * WORD, 8)
        with self.assertRaisesRegex(wire.ProtocolError, "serial"):
            wire.decode_reply(packet, self.payload, self.snap)
        with self.assertRaisesRegex(wire.ProtocolError, "serial"):
            wire.decode_reply(vector("two-reply"), vector("two-payload"), self.snap, after_serial=6)

    def test_bad_payload_lengths_and_mapping_lifetime(self):
        length_offset = MAGIC_SIZE + 4 * WORD
        for reason, length, mapped in (
            ("length for mapping", len(self.payload), self.payload[:-1]),
            ("truncated bottom row rectangle", len(self.payload) - 1, self.payload),
            ("trailing bytes", len(self.payload) + 1, self.payload + b"\0"),
            ("length for mapping", 0, self.payload),
        ):
            with self.subTest(length=length), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.decode_reply(word_at(self.packet, length_offset, length), mapped, self.snap)
        mapping = bytearray(self.payload + b"unused mapping tail")
        reply = wire.decode_reply(self.packet, mapping, self.snap)
        mapping[:] = b"overwritten by a later transaction"
        self.assertEqual(reply.snapshot.settled.bottom_row_text(), "ready")

    def test_failed_decode_does_not_pin_the_mapping(self):
        for decode in (lambda mapping: wire.decode_reply(self.packet, mapping, self.snap),
                       wire.decode_payload):
            # Let the ProtocolError cross the mapping's context exit. Its traceback
            # must not retain an exported memoryview that prevents mmap.close().
            with self.assertRaises(wire.ProtocolError):
                with mmap(-1, len(self.payload)) as mapping:
                    decode(mapping)  # anonymous mappings start with a zeroed frame count

    def test_size_error(self):
        size = vector("size-reply")
        with self.assertRaises(wire.SizeError) as failure:
            wire.decode_reply(size, b"", self.snap)
        self.assertEqual(failure.exception.result, wire.Result.SIZE)
        with self.assertRaisesRegex(wire.ProtocolError, "payload length"):
            wire.decode_reply(word_at(size, MAGIC_SIZE + 4 * WORD, 1), b"\0", self.snap)

    def test_malformed_payload_fields(self):
        for reason, offset, value in (
            ("dimensions", 3 * WORD, 0),  # window width in first frame
            ("dimensions", 5 * WORD, 6),  # columns exceed 50 px / 10 px
            ("rectangle", 11 * WORD, 6),  # document width exceeds five columns
            ("extra row", (CELLS - 2) * WORD, 1),  # no fractional translation
            ("sections", (CELLS - 1) * WORD, 16),  # first bit after the four sections
            ("cell count", CELLS * WORD, 14),  # five columns times three rows is 15
            ("cell kind", FIRST_CELL * WORD, 4),  # one after unresolved
            ("attribute", (FIRST_CELL + 1) * WORD, 8),  # first bit after the three flags
        ):
            with self.subTest(reason=reason), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.decode_payload(word_at(self.payload, offset, value))
        size = len(self.payload)
        for reason, offset, value in (
            ("several visible tabs are active", size - TABS_TAIL + WORD + 4 * WORD, 1),  # first tab
            ("bottom row rectangle outside", size - BOTTOM_ROW_TAIL + WORD, 3),  # y = rows
        ):
            with self.subTest(reason=reason), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.decode_payload(word_at(self.payload, offset, value))
        offset = (FIRST_CELL + 5) * WORD  # kind, flags, colors and string count precede UTF-8
        for reason, byte in (("UTF-8", b"\xff"), ("invalid text for cell kind", b"\0")):
            corrupt = self.payload[:offset] + byte + self.payload[offset + 1:]
            with self.subTest(reason=reason), self.assertRaisesRegex(wire.ProtocolError, reason):
                wire.decode_payload(corrupt)

    def test_rejects_every_truncated_payload_prefix(self):
        for name in ("single-payload", "two-payload"):
            data = vector(name)
            for size in range(len(data)):
                with self.subTest(name=name, size=size), self.assertRaises(wire.ProtocolError):
                    wire.decode_payload(data[:size])

    def test_bad_frame_order_and_minimap_geometry(self):
        data = vector("two-payload")
        # The two-frame vector embeds the singleton's frame as its settled suffix.
        settled = len(data) - (len(self.payload) - WORD)
        with self.assertRaisesRegex(wire.ProtocolError, "witness order"):
            wire.decode_payload(word_at(data, settled, 1))
        with self.assertRaisesRegex(wire.ProtocolError, "serial order"):
            wire.decode_payload(word_at(data, settled + WORD, 5))  # older than first-action serial 6
        # Last interval in first frame becomes adjacent to [1, 3).
        with self.assertRaisesRegex(wire.ProtocolError, "ink interval"):
            wire.decode_payload(word_at(data, settled - 2 * WORD, 3))


if __name__ == "__main__":
    raise SystemExit(records.unittest_main("WIRE"))
