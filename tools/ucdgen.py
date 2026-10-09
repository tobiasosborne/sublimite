#!/usr/bin/env python3
"""Generate src/utf8/tables.h from Unicode 15.1.0 UCD files. Run: python3 -I tools/ucdgen.py
(--check: verify without writing). Inputs are verified against pinned hashes first."""
import hashlib, os, sys, urllib.request

VER = "15.1.0"
BASE = "https://www.unicode.org/Public/%s/ucd/" % VER
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VDIR = os.path.join(ROOT, "vendor", "ucd")
OUT = os.path.join(ROOT, "src", "utf8", "tables.h")
FILES = ["UnicodeData.txt", "EastAsianWidth.txt", "emoji/emoji-data.txt",
         "auxiliary/GraphemeBreakProperty.txt", "DerivedCoreProperties.txt",
         "auxiliary/GraphemeBreakTest.txt"]


# Pinned inputs. The hashes are the immutable record of what "Unicode 15.1.0"
# means here (also in vendor/ucd/SHA256SUMS, which this script never rewrites).
EXPECTED = {
    "DerivedCoreProperties.txt": "f55d0db69123431a7317868725b1fcbf1eab6b265d756d1bd7f0f6d9f9ee108b",
    "EastAsianWidth.txt": "b08191401dc125f4e84ef262a95754faae6b737c79538e17ea9664a63434e94e",
    "GraphemeBreakProperty.txt": "a7e52eee647e52dc210b8719b4d7037276f4b353810293d69377fc46374cec3f",
    "GraphemeBreakTest.txt": "ed9c5e92fd0911ccbeeb63c97cb19c519ea272ff1112ce843abd991582dd848f",
    "UnicodeData.txt": "2fc713e6a31a87c4850a37fe2caffa4218180fadb5de86b43a143ddb4581fb86",
    "emoji-data.txt": "d7aef489c8fe4c14f09ea5695200277c6b93ac82ac60845cdd2161b0d6835cc1",
}


def fetch():
    """Download missing inputs, then verify every input against EXPECTED (fail, never relabel)."""
    os.makedirs(VDIR, exist_ok=True)
    for f in FILES:
        dst = os.path.join(VDIR, os.path.basename(f))
        if not os.path.exists(dst):
            with urllib.request.urlopen(BASE + f, timeout=60) as r:
                data = r.read()
            with open(dst, "wb") as o:
                o.write(data)
    for f in FILES:
        name = os.path.basename(f)
        h = hashlib.sha256(open(os.path.join(VDIR, name), "rb").read()).hexdigest()
        if h != EXPECTED[name]:
            raise SystemExit("%s: sha256 %s does not match the pinned Unicode %s hash %s" % (name, h, VER, EXPECTED[name]))
    sums = {}
    with open(os.path.join(VDIR, "SHA256SUMS")) as fh:
        for ln in fh:
            if ln.strip():
                h, n = ln.split()
                sums[n] = h
    if sums != EXPECTED:
        raise SystemExit("vendor/ucd/SHA256SUMS disagrees with the pinned hashes in tools/ucdgen.py")


def data_lines(name):
    with open(os.path.join(VDIR, os.path.basename(name)), encoding="utf-8") as fh:
        for raw in fh:
            s = raw.split("#", 1)[0].strip()
            if s:
                yield [x.strip() for x in s.split(";")]


def span(field):
    if ".." in field:
        a, b = field.split("..")
        return int(a, 16), int(b, 16)
    v = int(field, 16)
    return v, v


def merge(rs):
    rs = sorted(rs)
    out = []
    for lo, hi in rs:
        if out and lo <= out[-1][1] + 1:
            if hi > out[-1][1]:
                out[-1][1] = hi
        else:
            out.append([lo, hi])
    return [(a, b) for a, b in out]


def prop_ranges(fname, prop):
    return [span(f[0]) for f in data_lines(fname) if len(f) > 1 and f[1] == prop]


def main():
    fetch()
    gc = {}
    first = None
    udata = []
    for f in data_lines("UnicodeData.txt"):
        cp, name, cat = int(f[0], 16), f[1], f[2]
        if name.endswith(", First>"):
            first = cp
            continue
        if name.endswith(", Last>") and first is not None:
            udata.append((first, cp, cat))
            first = None
            continue
        udata.append((cp, cp, cat))
    zw = []
    for lo, hi, cat in udata:
        if cat in ("Mn", "Me", "Mc", "Cf"):
            zw.append((lo, hi))
    zw += prop_ranges("DerivedCoreProperties.txt", "Default_Ignorable_Code_Point")
    zw += [(0x200B, 0x200F), (0xFE00, 0xFE0F), (0xE0100, 0xE01EF)]
    wide = [span(f[0]) for f in data_lines("EastAsianWidth.txt") if len(f) > 1 and f[1] in ("W", "F")]
    wide += prop_ranges("emoji/emoji-data.txt", "Emoji_Presentation")
    ext = prop_ranges("emoji/emoji-data.txt", "Extended_Pictographic")
    gbp = {}
    for f in data_lines("auxiliary/GraphemeBreakProperty.txt"):
        gbp.setdefault(f[1], []).append(span(f[0]))
    incb = {}
    for f in data_lines("DerivedCoreProperties.txt"):
        if len(f) > 2 and f[1] == "InCB":
            incb.setdefault(f[2], []).append(span(f[0]))
    tables = [
        ("ZERO_WIDTH", zw), ("WIDE", wide), ("EXT_PICT", ext),
        ("REGIONAL_INDICATOR", gbp.get("Regional_Indicator", [])),
        ("HANGUL_L", gbp.get("L", [])), ("HANGUL_V", gbp.get("V", [])),
        ("HANGUL_T", gbp.get("T", [])), ("HANGUL_LV", gbp.get("LV", [])),
        ("HANGUL_LVT", gbp.get("LVT", [])), ("PREPEND", gbp.get("Prepend", [])),
        ("SPACINGMARK", gbp.get("SpacingMark", [])), ("EXTEND", gbp.get("Extend", [])),
        ("CONTROL", gbp.get("Control", [])),
        ("INCB_CONSONANT", incb.get("Consonant", [])), ("INCB_LINKER", incb.get("Linker", [])),
        ("INCB_EXTEND", incb.get("Extend", [])),
    ]
    out = ["/* Generated by tools/ucdgen.py from Unicode %s. Do not edit. */" % VER,
           "#ifndef EDITOR_UTF8_TABLES_H", "#define EDITOR_UTF8_TABLES_H", "",
           "#include <stdint.h>", "",
           "typedef struct { uint32_t lo, hi; } utf8_range;", "",
           "static const char UNICODE_VERSION[] = \"%s\";" % VER, ""]
    total = 0
    counts = []
    for name, rs in tables:
        m = merge(rs)
        if not m:
            raise SystemExit("empty table " + name)
        counts.append((name, len(m)))
        total += len(m) * 8
        out.append("static const utf8_range UCD_%s[%d] = {" % (name, len(m)))
        for a, b in m:
            out.append("    {0x%04X, 0x%04X}," % (a, b))
        out.append("};")
        out.append("")
    out.append("#endif")
    text = "\n".join(out) + "\n"
    if "--check" in sys.argv:        # non-mutating CI check: tables.h must equal a fresh generation
        if open(OUT, encoding="utf-8").read() != text:
            raise SystemExit("src/utf8/tables.h is stale: run python3 -I tools/ucdgen.py")
        print("tables.h up to date")
        return
    with open(OUT, "w", encoding="utf-8") as o:
        o.write(text)
    for name, n in counts:
        print("%-20s %6d ranges" % (name, n))
    print("total ranges %d, table bytes %d (%d bytes sizeof(utf8_range)=8)" % (sum(n for _, n in counts), total, total))


if __name__ == "__main__":
    main()
