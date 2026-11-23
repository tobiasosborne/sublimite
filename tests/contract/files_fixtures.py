"""Disk fixtures from the stock v1 find-in-files and binary-open tests."""

import os


SEARCH_FILES = {
    "a.txt": b"Alpha cat\ncatapult CAT cat\nlast cat",
    "nested/b.c": b"cat\nscatter\ncat!\n",
    "nested/skip.txt": b"cat",
    "binary": b"cat\0cat",
}
BINARY_SEED = 2463534242  # v1 binary_open_fixture's xorshift32 seed.


def search_tree(root):
    """Keep the reference hit order and add an unrelated binary sentinel."""
    (root / "nested").mkdir(mode=0o700)
    for name, data in SEARCH_FILES.items():
        (root / name).write_bytes(data)


def search_results(root):
    path = os.fsencode(root)
    return (
        path + b"/a.txt:\n    1: Alpha cat\n    2: catapult CAT cat\n    3: last cat\n\n"
        + path + b"/nested/b.c:\n    1: cat\n    2: scatter\n    3: cat!\n\n"
        + path + b"/nested/skip.txt:\n    1: cat\n\n9 matches across 3 files\n"
    )


def binary_bytes():
    """Port binary_open_fixture verbatim, including unsigned 32-bit truncation."""
    size = 256 * 1024  # v1 uses 256 KiB to exceed the file prefix and viewport.
    data = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    blanks = tuple(s.encode("utf-8") for s in ("\u00a0", "\u2002", "\u3000", "\u200b", "\uf227"))
    state = BINARY_SEED
    mask = (1 << 32) - 1
    while len(data) < size:
        state ^= (state << 13) & mask
        state ^= state >> 17
        state ^= (state << 5) & mask
        pick = state % 16
        if pick < len(blanks):
            glyph = blanks[pick]
            if len(data) + len(glyph) > size:
                break
            data.extend(glyph)
        elif pick == 5:
            data.append(0)
        elif pick == 6:
            data.append(ord("\n"))
        else:
            data.append((state >> 8) & 0xff)
    return bytes(data).ljust(size, b"\0")
