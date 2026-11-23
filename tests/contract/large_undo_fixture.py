"""Deterministic ASCII/LF fixtures for the X3 large-delete undo case.

Sizes come from the pinned v1 line-index constants (docs/decisions/v2-he7.15.md):

  LINEIDX_CHUNK           = 65,536  (P, v1 src/lineidx/lineidx.h:35)
  LINEIDX_EDIT_MAX_CHUNKS = 512     (P, v1 src/lineidx/lineidx.h:38)

After Select All + Delete the index holds one empty chunk, so the inverse insert of
L bytes plans newlen = L and pieces = ceil(L / 65,536) (v1 lineidx.c plan_edit).
The veto fires iff pieces > 512, so the largest admitted size is
512 x 65,536 = 33,554,432 B (the twin) and the design's above-boundary fixture is
(512 + 1) x 65,536 = 33,619,968 B, one full chunk past it.

Every line is 64 bytes (63 printable ASCII + LF). 64 divides 65,536, so both sizes
are whole lines: 524,288 and 525,312 lines. The content is a pure function of the
line number; no seed, clock or environment input.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

LINEIDX_CHUNK = 65_536
LINEIDX_EDIT_MAX_CHUNKS = 512
LINE_BYTES = 64
TWIN_BYTES = LINEIDX_EDIT_MAX_CHUNKS * LINEIDX_CHUNK
OVER_BYTES = (LINEIDX_EDIT_MAX_CHUNKS + 1) * LINEIDX_CHUNK
VARIANTS = {"twin": TWIN_BYTES, "over": OVER_BYTES}
_ALPHABET = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
_LINES_PER_BLOCK = LINEIDX_CHUNK // LINE_BYTES


def line(number):
    """Return line `number`: 10-digit number, space, 52 rotated letters, LF."""
    start = number % len(_ALPHABET)
    body = (_ALPHABET[start:] + _ALPHABET[:start])[:52]
    return b"%010d " % number + body + b"\n"


def blocks(size):
    """Yield the fixture in 65,536-byte blocks (one index chunk each)."""
    if size % LINE_BYTES:
        raise ValueError(f"size must be a multiple of {LINE_BYTES}: {size}")
    lines = size // LINE_BYTES
    for first in range(0, lines, _LINES_PER_BLOCK):
        yield b"".join(line(n) for n in range(first, min(lines, first + _LINES_PER_BLOCK)))


def expected_digest(size):
    """SHA-256 computed from the generator, independent of any file on disk."""
    digest = hashlib.sha256()
    for block in blocks(size):
        digest.update(block)
    return digest.hexdigest()


def file_digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(directory, variant):
    """Create (or verify) the fixture and its manifest; return the manifest."""
    size = VARIANTS[variant]
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"large_undo_{variant}.txt"
    record = directory / f"large_undo_{variant}.json"
    expected = expected_digest(size)
    if not path.exists() or path.stat().st_size != size or file_digest(path) != expected:
        partial = path.with_suffix(".partial")
        with open(partial, "wb") as stream:
            for block in blocks(size):
                stream.write(block)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(partial, path)
    on_disk = file_digest(path)
    if path.stat().st_size != size or on_disk != expected:
        raise RuntimeError(f"fixture verification failed: {path}")
    manifest = {
        "variant": variant, "path": str(path.resolve()), "bytes": size,
        "lines": size // LINE_BYTES, "chunks": -(-size // LINEIDX_CHUNK),
        "sha256": on_disk, "generator_sha256": expected,
    }
    partial = record.with_suffix(".partial")
    partial.write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    os.replace(partial, record)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="fixture directory (under build/)")
    parser.add_argument("variants", nargs="*", choices=sorted(VARIANTS))
    args = parser.parse_args(argv)
    for variant in args.variants or sorted(VARIANTS):
        print(json.dumps(write(args.out, variant)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
