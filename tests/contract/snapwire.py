"""Screen-snapshot codec for the contract driver (docs/decisions/v2-he7.10-wire.md)."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import IntEnum, IntFlag
from struct import Struct
from typing import Optional


REVISION = 1
MAGIC = b"SCEL"
WORD = Struct("<I")
U32_MAX = (1 << (8 * WORD.size)) - 1
I32_LIMIT = (U32_MAX + 1) // 2  # one word bit carries the sign
SUBPIXELS = 1 << 8  # render's Q8 units per pixel

PACKET = Struct("<4s5I")
FRAME_SIZES = Struct("<8I")  # witness, serial, and window/grid/cell axis pairs
RECT = Struct("<4I")
SCROLL = Struct("<i")
FRAME_TAIL = Struct("<2I")  # extra rows and sections
CELL_HEADER = Struct("<5I")  # kind, attributes, colors, text byte count
INTERVAL = Struct("<2I")


class Operation(IntEnum):
    ARM = 0
    SNAP = 1
    NEXT = 2


class Result(IntEnum):
    OK = 0
    ARMED = 1
    VERSION = 2
    ORDER = 3
    SIZE = 4
    INVALID = 5


class Witness(IntEnum):
    SETTLED = 0
    FIRST_ACTION = 1


class CellKind(IntEnum):
    BLANK = 0
    TEXT = 1
    CONTINUATION = 2
    UNRESOLVED = 3


class Attribute(IntFlag):
    CARET = 1 << 0
    UNDERLINE = 1 << 1
    WIDE_LEAD = 1 << 2


class Section(IntFlag):
    CELLS = 1 << 0
    TABS = 1 << 1
    BOTTOM_ROW = 1 << 2
    MINIMAP = 1 << 3


class ProtocolError(ValueError):
    """A malformed observation or invalid transaction."""


class ReplyError(ProtocolError):
    """An explicit non-success result from the editor."""

    def __init__(self, result, ticket, submit_serial):
        self.result = result
        self.ticket = ticket
        self.submit_serial = submit_serial
        super().__init__(f"inspector returned {result.name} (ticket {ticket}, serial {submit_serial})")


class SizeError(ReplyError):
    """The submitted snapshot exceeded the agreed mapping capacity."""


class UnprovidedSectionError(ProtocolError):
    """The adapter did not provide this section of the frame."""

    def __init__(self, section):
        self.section = section
        super().__init__(f"frame did not provide {section.name.lower()}")


class UnresolvedCellError(ProtocolError):
    """A text read crossed artwork without a text label."""


class MissingWitnessError(ProtocolError):
    """A requested first-action witness was absent."""


@dataclass(frozen=True)
class Request:
    operation: Operation
    ticket: int
    previous_serial: int = 0


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int

    @property
    def empty(self):
        return self.width == 0 or self.height == 0

    def contains(self, rect):
        return (self.x <= rect.x <= rect.x + rect.width <= self.x + self.width
                and self.y <= rect.y <= rect.y + rect.height <= self.y + self.height)


@dataclass(frozen=True)
class Cell:
    kind: CellKind
    attributes: Attribute = Attribute(0)
    foreground: int = 0
    background: int = 0
    text: str = ""


@dataclass(frozen=True)
class Tab:
    rect: Rect
    active: bool


@dataclass(frozen=True)
class Minimap:
    region: Rect
    box: Rect
    ink_intervals: tuple[tuple[int, int], ...]


def _provided(value, section):
    if value is None:
        raise UnprovidedSectionError(section)
    return value


@dataclass(frozen=True)
class Frame:
    witness: Witness
    submit_serial: int
    window_width: int
    window_height: int
    columns: int
    rows: int
    cell_width: int
    cell_height: int
    document: Rect
    scroll_q8: int
    scroll_rect: Rect
    extra_rows: int
    _cells: Optional[tuple[Cell, ...]] = None
    _tabs: Optional[tuple[Tab, ...]] = None
    _bottom_row: Optional[Rect] = None
    _minimap: Optional[Minimap] = None

    @property
    def provided_sections(self):
        flags = Section(0)
        for section, value in ((Section.CELLS, self._cells), (Section.TABS, self._tabs),
                               (Section.BOTTOM_ROW, self._bottom_row), (Section.MINIMAP, self._minimap)):
            if value is not None:
                flags |= section
        return flags

    @property
    def cells(self):
        return _provided(self._cells, Section.CELLS)

    @property
    def tabs(self):
        return _provided(self._tabs, Section.TABS)

    @property
    def bottom_row(self):
        return _provided(self._bottom_row, Section.BOTTOM_ROW)

    @property
    def minimap(self):
        return _provided(self._minimap, Section.MINIMAP)

    def cell_text(self, row, start=0, stop=None):
        """Read a half-open column range on one logical grid row."""
        cells = self.cells
        if stop is None:
            stop = self.columns
        if not (0 <= row < self.rows + self.extra_rows and 0 <= start <= stop <= self.columns):
            raise IndexError(f"cell range ({row}, {start}:{stop}) is outside the grid")
        offset = row * self.columns
        text = []
        for column in range(start, stop):
            cell = cells[offset + column]
            if cell.kind == CellKind.UNRESOLVED:
                raise UnresolvedCellError(f"unresolved artwork at row {row}, column {column}")
            text.append(" " if cell.kind == CellKind.BLANK else cell.text)
        return "".join(text)

    def drawn_caret_cells(self):
        """Coordinates (row, column) in row-major order."""
        return tuple(divmod(i, self.columns) for i, cell in enumerate(self.cells)
                     if cell.attributes & Attribute.CARET)

    def _rect_text(self, rect):
        if rect.empty:
            return ""
        return "\n".join(self.cell_text(row, rect.x, rect.x + rect.width)
                         for row in range(rect.y, rect.y + rect.height))

    def visible_tab_titles(self):
        return tuple(self._rect_text(tab.rect) for tab in self.tabs)

    @property
    def active_tab(self):
        return next((tab for tab in self.tabs if tab.active), None)

    def bottom_row_text(self):
        return self._rect_text(self.bottom_row)

    @property
    def minimap_box(self):
        return self.minimap.box

    @property
    def minimap_ink_intervals(self):
        return self.minimap.ink_intervals


@dataclass(frozen=True)
class Snapshot:
    frames: tuple[Frame, ...]

    @property
    def settled(self):
        return self.frames[-1]

    @property
    def first_action(self):
        frame = self.frames[0]
        if frame.witness != Witness.FIRST_ACTION:
            raise MissingWitnessError("snapshot has no first-action witness")
        return frame


@dataclass(frozen=True)
class Reply:
    ticket: int
    result: Result
    submit_serial: int
    snapshot: Optional[Snapshot] = None


def _require(condition, message):
    if not condition:
        raise ProtocolError(message)


def _u32(value):
    return isinstance(value, int) and 0 <= value <= U32_MAX


def _enum(cls, value, name):
    try:
        return cls(value)
    except ValueError as exc:
        raise ProtocolError(f"unknown {name} {value}") from exc


def encode_request(request):
    """Return the five X11 data32 words as integers for the X driver."""
    _require(_u32(request.operation), "invalid request operation")
    operation = _enum(Operation, request.operation, "operation")
    _require(_u32(request.ticket) and request.ticket != 0, "invalid request ticket")
    _require(_u32(request.previous_serial), "invalid previous serial")
    _require(operation == Operation.NEXT or request.previous_serial == 0,
             "previous serial is only used by NEXT")
    return (REVISION, int(operation), request.ticket, request.previous_serial, 0)


def _bytes_view(data):
    try:
        with memoryview(data) as view:
            return view.cast("B")
    except (TypeError, ValueError) as exc:
        raise ProtocolError("expected contiguous wire bytes") from exc


class _Reader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def fit(self, count, width, name):
        _require(count <= (len(self.data) - self.offset) // width, f"truncated {name}")

    def fields(self, layout, name):
        self.fit(1, layout.size, name)
        fields = layout.unpack_from(self.data, self.offset)
        self.offset += layout.size
        return fields

    def word(self, name):
        return self.fields(WORD, name)[0]

    def rect(self, name):
        return Rect(*self.fields(RECT, name))

    def string(self, size):
        self.fit(size, 1, "cell string")
        end = self.offset + size
        try:
            with self.data[self.offset:end] as text:
                value = text.tobytes().decode("utf-8")
        except UnicodeError as exc:
            raise ProtocolError("invalid UTF-8 in cell string") from exc
        self.offset = end
        return value


def _validate_rect(rect, bounds, name):
    _require(all(_u32(value) for value in (rect.x, rect.y, rect.width, rect.height))
             and bounds.contains(rect), f"{name} rectangle outside its bounds")


def _validate_geometry(frame):
    _enum(Witness, frame.witness, "witness kind")
    _require(_u32(frame.submit_serial) and frame.submit_serial != 0, "invalid frame serial")
    sizes = (frame.window_width, frame.window_height, frame.columns, frame.rows,
             frame.cell_width, frame.cell_height)
    _require(all(_u32(value) and value > 0 for value in sizes), "invalid frame dimensions")
    _require((frame.columns - 1) * frame.cell_width < frame.window_width
             and (frame.rows - 1) * frame.cell_height < frame.window_height,
             "grid dimensions extend outside the window")
    _require(isinstance(frame.scroll_q8, int) and -I32_LIMIT <= frame.scroll_q8 < I32_LIMIT
             and abs(frame.scroll_q8) < frame.cell_height * SUBPIXELS,
             "scroll translation must fit a signed word and be shorter than a cell")
    _require(_u32(frame.extra_rows) and frame.extra_rows <= 1
             and (frame.extra_rows == 0 or frame.scroll_q8 != 0), "invalid extra row count")
    _require(frame.columns * (frame.rows + frame.extra_rows) <= U32_MAX,
             "grid dimensions overflow the cell count")
    grid = Rect(0, 0, frame.columns, frame.rows)
    _validate_rect(frame.document, grid, "document")
    _validate_rect(frame.scroll_rect, grid, "scroll")
    _require(frame.scroll_q8 == 0 or not frame.scroll_rect.empty,
             "scroll translation requires a nonempty scroll rectangle")


def _check_cell_count(frame, count):
    _require(count == frame.columns * (frame.rows + frame.extra_rows),
             "cell count does not match grid dimensions")


def _validate_cell(cell):
    _enum(CellKind, cell.kind, "cell kind")
    allowed = int(Attribute.CARET | Attribute.UNDERLINE | Attribute.WIDE_LEAD)
    _require(_u32(cell.attributes) and not (cell.attributes & ~allowed), "unknown cell attribute bits")
    _require(not (cell.attributes & Attribute.WIDE_LEAD)
             or cell.kind in (CellKind.TEXT, CellKind.UNRESOLVED), "wide lead on an invalid cell kind")
    _require(_u32(cell.foreground) and _u32(cell.background), "invalid color class")
    _require(isinstance(cell.text, str) and "\0" not in cell.text
             and bool(cell.text) == (cell.kind == CellKind.TEXT), "invalid text for cell kind")


def _validate_sections(frame):
    if frame._cells is not None:
        _check_cell_count(frame, len(frame.cells))
        for cell in frame.cells:
            _validate_cell(cell)
    grid = Rect(0, 0, frame.columns, frame.rows)
    if frame._tabs is not None:
        _require(len(frame.tabs) <= frame.columns, "tab count exceeds columns")
        for tab in frame.tabs:
            _validate_rect(tab.rect, grid, "tab")
            _require(not tab.rect.empty, "visible tab rectangle is empty")
            _require(isinstance(tab.active, bool), "invalid tab active flag")
        _require(sum(tab.active for tab in frame.tabs) <= 1, "several visible tabs are active")
    if frame._bottom_row is not None:
        _validate_rect(frame.bottom_row, grid, "bottom row")
        _require(frame.bottom_row.empty or frame.bottom_row.height == 1,
                 "bottom row rectangle must span one row")
    if frame._minimap is not None:
        minimap = frame.minimap
        window = Rect(0, 0, frame.window_width, frame.window_height)
        _validate_rect(minimap.region, window, "minimap region")
        _validate_rect(minimap.box, window, "minimap box")
        _require(minimap.box.empty or minimap.region.contains(minimap.box),
                 "minimap box rectangle outside its region")
        last_end = -1
        for start, end in minimap.ink_intervals:
            _require(not minimap.region.empty and _u32(start) and _u32(end)
                     and last_end < start < end <= minimap.region.height, "invalid minimap ink interval")
            last_end = end


def _validate_snapshot(snapshot):
    expected = (Witness.FIRST_ACTION, Witness.SETTLED) if len(snapshot.frames) == 2 else (Witness.SETTLED,)
    _require(tuple(frame.witness for frame in snapshot.frames) == expected, "invalid frame witness order/count")
    _require(all(_u32(frame.submit_serial) and frame.submit_serial != 0 for frame in snapshot.frames),
             "invalid frame serial")
    _require(len(snapshot.frames) == 1 or snapshot.first_action.submit_serial <= snapshot.settled.submit_serial,
             "invalid frame serial order")


def _read_cell(reader):
    kind, attrs, fg, bg, size = reader.fields(CELL_HEADER, "cell header")
    return Cell(_enum(CellKind, kind, "cell kind"), Attribute(attrs), fg, bg, reader.string(size))


def _read_tab(reader):
    rect = reader.rect("tab rectangle")
    active = reader.word("tab active flag")
    _require(active in (0, 1), "invalid tab active flag")
    return Tab(rect, bool(active))


def _read_minimap(reader):
    region = reader.rect("minimap region")
    box = reader.rect("minimap box")
    count = reader.word("ink interval count")
    _require(count <= region.height, "ink interval count exceeds region height")
    reader.fit(count, INTERVAL.size, "ink intervals")
    return Minimap(region, box, tuple(reader.fields(INTERVAL, "ink interval") for _ in range(count)))


def _read_frame(reader):
    witness, serial, *sizes = reader.fields(FRAME_SIZES, "frame header")
    document = reader.rect("document rectangle")
    (scroll_q8,) = reader.fields(SCROLL, "scroll translation")
    scroll_rect = reader.rect("scroll rectangle")
    extra_rows, flags = reader.fields(FRAME_TAIL, "frame tail")
    allowed = int(Section.CELLS | Section.TABS | Section.BOTTOM_ROW | Section.MINIMAP)
    _require(not (flags & ~allowed), "unknown provided sections bits")
    frame = Frame(_enum(Witness, witness, "witness kind"), serial, *sizes,
                  document, scroll_q8, scroll_rect, extra_rows)
    _validate_geometry(frame)

    cells = tabs = bottom_row = minimap = None
    if flags & Section.CELLS:
        count = reader.word("cell count")
        _check_cell_count(frame, count)
        reader.fit(count, CELL_HEADER.size, "cells")
        cells = tuple(_read_cell(reader) for _ in range(count))
    if flags & Section.TABS:
        count = reader.word("tab count")
        _require(count <= frame.columns, "tab count exceeds columns")
        reader.fit(count, RECT.size + WORD.size, "tabs")
        tabs = tuple(_read_tab(reader) for _ in range(count))
    if flags & Section.BOTTOM_ROW:
        bottom_row = reader.rect("bottom row rectangle")
    if flags & Section.MINIMAP:
        minimap = _read_minimap(reader)
    frame = replace(frame, _cells=cells, _tabs=tabs, _bottom_row=bottom_row, _minimap=minimap)
    _validate_sections(frame)
    return frame


def decode_payload(data):
    """Validate an exact payload and return owned, immutable observations."""
    with _bytes_view(data) as view:
        reader = _Reader(view)
        _require(len(view) <= U32_MAX, "payload length exceeds a wire word")
        count = reader.word("frame count")
        _require(count in (1, 2), "invalid frame count")
        snapshot = Snapshot(tuple(_read_frame(reader) for _ in range(count)))
        _validate_snapshot(snapshot)
        _require(reader.offset == len(view), "trailing bytes after the snapshot")
    return snapshot


def decode_reply(packet, mapping, request, *, after_serial=0, require_first_action=False):
    """Match one completion to a request and consume its declared mapping prefix.

    Pass the last accepted observation serial as after_serial, or zero when new.
    The transport owns packet truncation/ancillary-data checks and the mapping lifetime.
    """
    _, operation, expected_ticket, previous, _ = encode_request(request)
    _require(_u32(after_serial), "invalid previous observation serial")
    with _bytes_view(packet) as response:
        _require(len(response) == PACKET.size, "invalid packet length")
        magic, revision, ticket, code, serial, size = PACKET.unpack(response)
    _require(magic == MAGIC, "wrong reply magic")
    _require(revision == REVISION, f"unsupported reply revision {revision}")
    _require(ticket == expected_ticket, f"reply ticket {ticket} does not match request {expected_ticket}")
    result = _enum(Result, code, "result code")

    if result != Result.OK:
        _require(size == 0, "non-OK reply has nonzero payload length")
        if result == Result.ARMED:
            _require(operation == Operation.ARM, "ARMED reply requires an ARM request")
            _require(serial >= after_serial, "stale ARMED reply serial")
            return Reply(ticket, result, serial)
        error = SizeError if result == Result.SIZE else ReplyError
        raise error(result, ticket, serial)

    _require(operation != Operation.ARM, "ARM cannot return an OK snapshot")
    _require(serial > max(after_serial, previous), "OK reply serial is stale")
    with _bytes_view(mapping) as mapped:
        _require(0 < size <= len(mapped), "invalid reply payload length for mapping")
        with mapped[:size] as payload:
            snapshot = decode_payload(payload)
    _require(snapshot.settled.submit_serial == serial, "settled frame serial does not match reply serial")
    _require(snapshot.frames[0].submit_serial > after_serial, "stale first frame serial")
    _require(operation == Operation.SNAP or len(snapshot.frames) == 1, "NEXT reply has a first-action witness")
    if require_first_action and snapshot.frames[0].witness != Witness.FIRST_ACTION:
        raise MissingWitnessError("snapshot has no first-action witness")
    return Reply(ticket, result, serial, snapshot)
