/* Scalar differential coverage for all scan APIs, with protected boundaries. */
#include "scan/scan.h"
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static scan_counts scalar_count(const uint8_t *p, size_t n)
{
    scan_counts result = {0, 0};
    for (size_t i = 0; i < n; i++) {
        result.newlines += p[i] == '\n' ? 1u : 0u;
        result.nonascii += p[i] >= 0x80 ? 1u : 0u;
    }
    return result;
}

static const uint8_t *scalar_byte(const uint8_t *p, size_t n, uint8_t byte)
{
    for (size_t i = 0; i < n; i++) if (p[i] == byte) return p + i;
    return NULL;
}

static const uint8_t *scalar_nth(const uint8_t *p, size_t n, uint64_t k)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\n') {
            if (k == 0) return p + i;
            k--;
        }
    }
    return NULL;
}

static void check(const uint8_t *p, size_t n, uint64_t k, uint8_t byte)
{
    scan_counts expected = scalar_count(p, n), got = scan_count(p, n);
    assert(got.newlines == expected.newlines && got.nonascii == expected.nonascii);
    assert(scan_find_byte(p, n, byte) == scalar_byte(p, n, byte));
    if (n) {
        assert(scan_find_byte(p, n, p[n - 1]) == scalar_byte(p, n, p[n - 1]));
    }
    /* Short cases exhaust byte queries; generated/range cases include all byte
     * values as data. Rank queries cover absence, the last match and overflow. */
    if (n <= 256) {
        for (unsigned c = 0; c < 256; c++)
            assert(scan_find_byte(p, n, (uint8_t)c) == scalar_byte(p, n, (uint8_t)c));
    }
    uint64_t ranks[] = {0, 1, k, expected.newlines ? expected.newlines - 1 : 0,
                        expected.newlines, expected.newlines + 1, UINT64_MAX - 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof ranks / sizeof ranks[0]; i++)
        assert(scan_find_nth_newline(p, n, ranks[i]) == scalar_nth(p, n, ranks[i]));

    /* A caller split need not coincide with a SIMD block or accumulator chunk. */
    size_t split = n ? (size_t)(k % (uint64_t)(n + 1)) : 0;
    scan_counts left = scan_count(p, split), right = scan_count(p + split, n - split);
    assert(left.newlines + right.newlines == expected.newlines);
    assert(left.nonascii + right.nonascii == expected.nonascii);
    const uint8_t *first = scan_find_byte(p, split, byte);
    if (!first) first = scan_find_byte(p + split, n - split, byte);
    assert(first == scalar_byte(p, n, byte));
    scan_counts left_expected = scalar_count(p, split);
    const uint8_t *nth = k < left_expected.newlines
        ? scan_find_nth_newline(p, split, k)
        : scan_find_nth_newline(p + split, n - split, k - left_expected.newlines);
    assert(nth == scalar_nth(p, n, k));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 1024u * 1024u) return 0;
    size_t header = size < 16 ? size : 16;
    size_t payload = size - header, n = payload;
    uint64_t k = 0;
    for (size_t i = 0; i < header && i < 8; i++) k = (k << 8) | data[i];
    uint8_t mode = size ? data[0] : 0;
    uint8_t byte = size > 8 ? data[8] : 0;
    if (mode & 1u) {
        const size_t boundaries[] = {0, 1, 15, 16, 17, 31, 32, 33, 4079, 4080,
                                     4081, 4095, 4096, 4113, 65535, 65536, 65537, 65553};
        n = boundaries[byte % (sizeof boundaries / sizeof boundaries[0])];
    }
    long page_value = sysconf(_SC_PAGESIZE);
    if (page_value <= 0) return 0;
    size_t page = (size_t)page_value;
    size_t accessible = ((n + page - 1) / page) * page;
    if (!accessible) accessible = page;
    size_t mapped = accessible + 2 * page;
    uint8_t *mapping = mmap(NULL, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return 0;
    uint8_t *begin = mapping + page;
    if (mprotect(begin, accessible, PROT_READ | PROT_WRITE) != 0) {
        (void)munmap(mapping, mapped); return 0;
    }
    /* Most ranges end exactly at the guard. Alternate cases start at the left
     * guard. No page slack can mask forward reads in the right-bound cases. */
    uint8_t *p = mode & 2u ? begin : begin + accessible - n;
    for (size_t i = 0; i < n; i++) {
        if (mode & 4u) p[i] = (uint8_t)i;
        else if (mode & 8u) p[i] = byte;
        else p[i] = payload ? data[header + i % payload] : 0;
    }
    check(p, n, k, byte);
    /* Empty ranges at an inaccessible pointer must perform no reads. */
    check(begin + accessible, 0, UINT64_MAX, byte);
    assert(munmap(mapping, mapped) == 0);
    return 0;
}
