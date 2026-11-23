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
import tempfile

LINEIDX_CHUNK = 65_536
LINEIDX_EDIT_MAX_CHUNKS = 512
LINE_BYTES = 64
TWIN_BYTES = LINEIDX_EDIT_MAX_CHUNKS * LINEIDX_CHUNK
OVER_BYTES = (LINEIDX_EDIT_MAX_CHUNKS + 1) * LINEIDX_CHUNK
# The reviewed files (docs/decisions/v2-he7.15.md fixture table): an oracle independent of
# line(), so a changed generator is rejected instead of deriving its own expectation.
REVIEWED = {
    "twin": (TWIN_BYTES, "f3d8ccc6a575bb8c88884f583d1e46f5348f66ba2217d74f76eac83adfd39a46"),
    "over": (OVER_BYTES, "7b5afe68bc3d7257923dd352789ef38ee788013263db54331006f0926948855e"),
}
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


def file_digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _publish(path, fill, check=None):
    """Write `fill(stream)` into a temporary private to this writer, run `check` on it once,
    then rename it over `path`: concurrent writers each publish complete bytes."""
    fd, partial = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".partial", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            fill(stream)
            stream.flush()
            os.fsync(stream.fileno())
        if check is not None:
            check(partial)
        os.replace(partial, path)
    except BaseException:
        os.unlink(partial)
        raise


def write(directory, variant):
    """Publish the reviewed fixture (a valid cached file is hashed once) and its manifest."""
    size, sha256 = REVIEWED[variant]
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"large_undo_{variant}.txt"

    def reviewed(name):
        return os.stat(name).st_size == size and file_digest(name) == sha256

    def verify(name):
        if not reviewed(name):
            raise RuntimeError(f"generated {variant} fixture is not the reviewed one: {sha256}")

    if not (path.is_file() and reviewed(path)):
        _publish(path, lambda stream: stream.writelines(blocks(size)), verify)
    manifest = {
        "variant": variant, "path": str(path.resolve()), "bytes": size,
        "lines": size // LINE_BYTES, "chunks": -(-size // LINEIDX_CHUNK), "sha256": sha256,
    }
    _publish(directory / f"large_undo_{variant}.json",
             lambda stream: stream.write((json.dumps(manifest, indent=2) + "\n").encode("ascii")))
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="fixture directory (under build/)")
    parser.add_argument("variants", nargs="*", choices=sorted(REVIEWED))
    args = parser.parse_args(argv)
    for variant in args.variants or sorted(REVIEWED):
        print(json.dumps(write(args.out, variant)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
