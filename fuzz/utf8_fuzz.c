/* libFuzzer target for src/utf8 (P1.1b). Input bytes are the text; libFuzzer
 * hands them over in an exact-size heap buffer, so any read past data[size-1]
 * is an ASan error. Properties:
 *   - decode matches an independent bit-pattern reference decoder, at full
 *     length and truncated to every k < 4 (len <= k: never uses bytes past n);
 *   - valid units re-encode to the same bytes (encode(decode(x)) == x);
 *   - prev from every unit end returns that unit's start; a backward walk
 *     visits exactly the forward units;
 *   - grapheme_next advances (>= 1), never overruns, and lands on a unit
 *     boundary; clusters tile the input;
 *   - cell_width is 0..2 and the inline and table paths agree;
 *   - ascii_run equals a byte loop (every start below 64, then every 61st);
 *   - encode of arbitrary 32-bit values decodes back to the value or U+FFFD. */
#include "utf8/utf8.h"
#include <stdlib.h>
#include <string.h>

#define FAIL() __builtin_trap()
#define REQUIRE(c) do { if (!(c)) FAIL(); } while (0)

static utf8_step ref_decode(const uint8_t *p, size_t n)
{
    utf8_step bad = { p[0], 1, 0 };
    uint8_t b = p[0];
    size_t len;
    uint32_t cp, min;
    if (b < 0x80) { utf8_step s = { b, 1, 1 }; return s; }
    if ((b & 0xE0) == 0xC0) { len = 2; cp = b & 0x1Fu; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0Fu; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07u; min = 0x10000; }
    else return bad;
    if (n < len) return bad;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return bad;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return bad;
    utf8_step s = { cp, (uint8_t)len, 1 };
    return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        REQUIRE(utf8_grapheme_next(data, 0) == 0);
        REQUIRE(utf8_prev(data, 0) == 0);
        return 0;
    }
    uint8_t *unit = calloc(size + 1, 1);            /* unit[o] = 1 iff o is a unit boundary */
    if (!unit) return 0;
    size_t off = 0, units = 0;
    while (off < size) {
        size_t rem = size - off;
        utf8_step s = utf8_decode(data + off, rem), r = ref_decode(data + off, rem);
        REQUIRE(s.cp == r.cp && s.len == r.len && s.valid == r.valid);
        REQUIRE(s.len >= 1 && s.len <= 4 && s.len <= rem);
        if (s.valid) {
            uint8_t e[4];
            REQUIRE(utf8_encode(s.cp, e) == s.len && memcmp(e, data + off, s.len) == 0);
        } else {
            REQUIRE(s.len == 1 && s.cp == data[off] && data[off] >= 0x80);
        }
        for (size_t k = 1; k < 4 && k < rem; k++) {  /* truncated view */
            utf8_step t = utf8_decode(data + off, k), tr = ref_decode(data + off, k);
            REQUIRE(t.cp == tr.cp && t.len == tr.len && t.valid == tr.valid && t.len <= k);
        }
        int w = utf8_cell_width(s.cp);
        REQUIRE(w >= 0 && w <= 2 && w == utf8_cell_width_table(s.cp));
        unit[off] = 1;
        off += s.len;
        units++;
        REQUIRE(utf8_prev(data, off) == off - s.len);
    }
    unit[size] = 1;
    size_t back = 0;
    for (size_t o = size; o > 0; back++) {
        size_t p = utf8_prev(data, o);
        REQUIRE(p < o && unit[p]);
        o = p;
    }
    REQUIRE(back == units);
    for (off = 0; off < size;) {
        size_t g = utf8_grapheme_next(data + off, size - off);
        REQUIRE(g >= 1 && g <= size - off && unit[off + g]);
        off += g;
    }
    for (size_t st = 0; st < size; st += st < 64 ? 1 : 61) {   /* bounded: inputs reach 4 KiB */
        size_t r = st;
        while (r < size && data[r] < 0x80) r++;
        REQUIRE(utf8_ascii_run(data + st, size - st) == r - st);
    }
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t x;
        memcpy(&x, data + i, 4);
        uint8_t e[4];
        size_t l = utf8_encode(x, e);
        utf8_step s = utf8_decode(e, l);
        int scalar = x <= 0x10FFFF && !(x >= 0xD800 && x <= 0xDFFF);
        REQUIRE(s.valid && s.len == l && s.cp == (scalar ? x : 0xFFFDu));
    }
    free(unit);
    return 0;
}
