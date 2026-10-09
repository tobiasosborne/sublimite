#include <stdint.h>
#include <stdio.h>
#include "utf8/tables.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static int in_table(const utf8_range *t, size_t n, uint32_t cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < t[mid].lo) hi = mid;
        else if (cp > t[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static int check_sorted(const char *name, const utf8_range *t, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (t[i].lo > t[i].hi) { fprintf(stderr, "%s[%zu] inverted\n", name, i); return 1; }
        if (i && t[i].lo <= t[i - 1].hi) { fprintf(stderr, "%s[%zu] overlaps/unsorted\n", name, i); return 1; }
    }
    return 0;
}

#define TABLE(t) t, N(t)

int main(void) {
    struct { const char *name; const utf8_range *t; size_t n; } all[] = {
        {"ZERO_WIDTH", TABLE(UCD_ZERO_WIDTH)}, {"WIDE", TABLE(UCD_WIDE)},
        {"EXT_PICT", TABLE(UCD_EXT_PICT)}, {"REGIONAL_INDICATOR", TABLE(UCD_REGIONAL_INDICATOR)},
        {"HANGUL_L", TABLE(UCD_HANGUL_L)}, {"HANGUL_V", TABLE(UCD_HANGUL_V)},
        {"HANGUL_T", TABLE(UCD_HANGUL_T)}, {"HANGUL_LV", TABLE(UCD_HANGUL_LV)},
        {"HANGUL_LVT", TABLE(UCD_HANGUL_LVT)}, {"PREPEND", TABLE(UCD_PREPEND)},
        {"SPACINGMARK", TABLE(UCD_SPACINGMARK)}, {"EXTEND", TABLE(UCD_EXTEND)},
        {"CONTROL", TABLE(UCD_CONTROL)}, {"INCB_CONSONANT", TABLE(UCD_INCB_CONSONANT)},
        {"INCB_LINKER", TABLE(UCD_INCB_LINKER)}, {"INCB_EXTEND", TABLE(UCD_INCB_EXTEND)},
    };
    size_t total = 0;
    int fail = 0;
    for (size_t i = 0; i < N(all); i++) {
        fail |= check_sorted(all[i].name, all[i].t, all[i].n);
        total += all[i].n * sizeof(utf8_range);
    }

    fail |= !in_table(TABLE(UCD_ZERO_WIDTH), 0x0301) ;
    fail |= in_table(TABLE(UCD_ZERO_WIDTH), 0x0041);
    fail |= !in_table(TABLE(UCD_WIDE), 0x4E00);
    fail |= !in_table(TABLE(UCD_WIDE), 0x1F600);
    fail |= !in_table(TABLE(UCD_EXT_PICT), 0x1F600);
    fail |= !in_table(TABLE(UCD_REGIONAL_INDICATOR), 0x1F1E6);
    fail |= !in_table(TABLE(UCD_HANGUL_L), 0x1100);
    fail |= !in_table(TABLE(UCD_CONTROL), 0x200B);      /* Cf, GCB Control */
    fail |= in_table(TABLE(UCD_CONTROL), 0x200D);       /* ZWJ is not Control */
    fail |= in_table(TABLE(UCD_CONTROL), 0x200C);       /* nor ZWNJ (Extend) */
    fail |= !in_table(TABLE(UCD_INCB_CONSONANT), 0x0915);
    fail |= !in_table(TABLE(UCD_INCB_LINKER), 0x094D);
    fail |= !in_table(TABLE(UCD_INCB_EXTEND), 0x200D);
    fail |= in_table(TABLE(UCD_INCB_CONSONANT), 0x094D);
    fail |= in_table(TABLE(UCD_WIDE), 0x0041);
    fail |= in_table(TABLE(UCD_EXT_PICT), 0x0041);
    fail |= in_table(TABLE(UCD_HANGUL_L), 0x0041);

    if (fail) {
        fprintf(stderr, "utf8 tables test: FAIL\n");
        return 1;
    }
    printf("utf8 tables test: ok (Unicode %s, table bytes %zu)\n", UNICODE_VERSION, total);
    return 0;
}
