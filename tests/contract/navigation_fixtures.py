"""Independent ASCII fixtures and row oracles for R1 and R6."""

from dataclasses import dataclass
import hashlib
import os
import re


WRAP_LINES = 128  # Equal halves around the mid-file Go To Line target.
WRAP_WORDS = 32  # 32 six-cell words give multiple rows on the test display.
LOG_BYTES = 1024 ** 3  # The exact size asserted by v1 starved_follows.
WINDOW_BYTES = 64 * 1024  # v1 SCROLL_SCAN_BUDGET.
RECORD_BYTES = 64  # One record and the EOL caret fit the document at test geometry.
LOG_SEED = "w0-r6-log-v1"
SEARCH_TOKEN = "navprobe"
SHARD_BYTES = 256 * 1024 * 1024  # v1 FIF_DEFAULT_FILE_BYTES.


@dataclass(frozen=True)
class Row:
    start: int
    text: str


def wrapped_fixture(path):
    text = "".join(" ".join(f"{line:03d}w{word:02d}" for word in range(WRAP_WORDS)) + "\n"
                   for line in range(1, WRAP_LINES + 1))
    path.write_text(text, encoding="ascii")
    return text


def visual_rows(text, width):
    """Lay out this fixture's unindented ASCII words with single spaces."""
    rows = []
    start = 0
    for line in text.split("\n"):
        offset = 0
        while len(line) - offset > width:
            # A separator immediately after a full row belongs to that row.
            end = line.rfind(" ", offset, offset + width + 1) + 1
            if end <= offset:
                raise ValueError("wrapped fixture contains an overwide word")
            rows.append(Row(start + offset, line[offset:end].rstrip(" ")))
            offset = end
        rows.append(Row(start + offset, line[offset:]))
        start += len(line) + 1
    return rows


def log_record(number, token="info"):
    message = f"L{number:08d} level={token} service=ingest status=ok"
    return message.ljust(RECORD_BYTES - 1).encode("ascii") + b"\n"


def log_fixture(path):
    # Use a per-window log batch ID in the unobserved body. Give the head and
    # the last two windows individual row IDs, including the EOF search hit.
    records_per_window = WINDOW_BYTES // RECORD_BYTES
    windows = LOG_BYTES // WINDOW_BYTES
    with path.open("xb") as stream:
        for window in range(windows):
            first = window * records_per_window
            if window == 0 or window >= windows - 2:
                records = range(first, first + records_per_window)
                block = b"".join(log_record(i, SEARCH_TOKEN if i in (
                    0, LOG_BYTES // RECORD_BYTES - 1) else "info") for i in records)
            else:
                token = hashlib.sha256(f"{LOG_SEED}:{window}".encode("ascii")).hexdigest()[:8]
                block = log_record(first, token) * records_per_window
            stream.write(block)
    if path.stat().st_size != LOG_BYTES:
        raise ValueError(f"expected {LOG_BYTES} bytes in {path}")


def read_log_rows(path, start, size):
    with path.open("rb") as stream:
        stream.seek(start)
        data = stream.read(size)
    if len(data) != size:
        raise ValueError(f"short fixture read at {start} in {path}")
    # At the tail this includes the empty logical row following the final LF.
    return [Row(start + match.start(), match[0].decode("ascii"))
            for match in re.finditer(rb"(?:\A|(?<=\n))[^\n]*", data)]


def search_shards(source, directory):
    """Expose all bytes to Find in Files despite its per-file limit."""
    directory.mkdir(mode=0o700)
    shard_count = LOG_BYTES // SHARD_BYTES
    with source.open("rb", buffering=0) as stream:
        for index in range(shard_count):
            path = directory / f"part{index}.log"
            with path.open("xb", buffering=0) as shard:
                remaining = SHARD_BYTES
                while remaining:
                    copied = os.copy_file_range(stream.fileno(), shard.fileno(), remaining)
                    if not copied:
                        raise ValueError(f"short corpus copy into {path}")
                    remaining -= copied
    return directory / f"part{shard_count - 1}.log"
