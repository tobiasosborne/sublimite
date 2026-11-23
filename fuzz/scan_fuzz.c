/* scan: scalar differential with every span flush against a PROT_NONE page.
 *
 * Input: bytes 0..7 big-endian rank k; byte 0 bit 0 picks a structural
 * length, bit 1 the left guard instead of the right, bits 2/3 a counting or
 * constant fill; byte 8 the fill byte and length index; bytes 16.. payload. */
#include "scan/scan.h"
#include "os/os.h"

#include <sys/mman.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

/* (P) SSE2 block, 255-block accumulator chunk, x86-64 base page, lineidx's
 * 64 KiB caller chunk (docs/decisions/wave1-L1-harvest.md §0.2). */
enum { HEADER = 16, BLOCK = 16, CHUNK = 255 * BLOCK, PAGE = 4096, LINEIDX_CHUNK = 65536 };
/* v1's input cap, 16 lineidx chunks: past the largest structural edge below
 * a longer input only repeats chunks and costs a larger mapping per run. */
#define MAX_INPUT ((size_t)16 * LINEIDX_CHUNK)

static scan_counts scalar_count(const uint8_t *p, size_t n)
{
    scan_counts r = {0, 0};
    for (size_t i = 0; i < n; i++) {
        r.newlines += p[i] == '\n';
        r.nonascii += p[i] >= 0x80;
    }
    return r;
}

static const uint8_t *scalar_nth(const uint8_t *p, size_t n, uint64_t k)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] == '\n' && k-- == 0) return p + i;
    return NULL;
}

static void check(const uint8_t *p, size_t n, uint64_t k)
{
    scan_counts want = scalar_count(p, n), got = scan_count(p, n);
    REQUIRE(got.newlines == want.newlines && got.nonascii == want.nonascii);
    /* Absence, first, the fuzzed rank, the last match and overflow. */
    uint64_t total = want.newlines;
    const uint64_t ranks[] = {0, 1, k, total - 1, total, total + 1, UINT64_MAX - 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof ranks / sizeof ranks[0]; i++)
        REQUIRE(scan_find_nth_newline(p, n, ranks[i]) == scalar_nth(p, n, ranks[i]));

    /* A caller split need not coincide with a block or chunk edge. */
    size_t split = (size_t)(k % ((uint64_t)n + 1));
    scan_counts left = scan_count(p, split), right = scan_count(p + split, n - split);
    REQUIRE(left.newlines + right.newlines == want.newlines);
    REQUIRE(left.nonascii + right.nonascii == want.nonascii);
    const uint8_t *nth = k < left.newlines ? scan_find_nth_newline(p, split, k)
                                           : scan_find_nth_newline(p + split, n - split, k - left.newlines);
    REQUIRE(nth == scalar_nth(p, n, k));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > MAX_INPUT) return 0;
    size_t header = size < HEADER ? size : HEADER;
    size_t payload = size - header, n = payload;
    uint64_t k = 0;
    for (size_t i = 0; i < header && i < 8; i++) k = (k << 8) | data[i];
    uint8_t mode = size ? data[0] : 0;
    uint8_t byte = size > 8 ? data[8] : 0;
    if (mode & 1u) {
        /* Block, chunk, page and lineidx-chunk edges, each -1/0/+1 or + one block. */
        static const size_t edges[] = {0, 1, BLOCK - 1, BLOCK, BLOCK + 1, 2 * BLOCK - 1, 2 * BLOCK,
                                       2 * BLOCK + 1, CHUNK - 1, CHUNK, CHUNK + 1, PAGE - 1, PAGE,
                                       PAGE + BLOCK + 1, LINEIDX_CHUNK - 1, LINEIDX_CHUNK,
                                       LINEIDX_CHUNK + 1, LINEIDX_CHUNK + BLOCK + 1};
        n = edges[byte % (sizeof edges / sizeof edges[0])];
    }
    size_t page = os_page_size();
    size_t span = (n + page - 1) / page * page;
    if (!span) span = page;
    size_t mapped = span + 2 * page;
    /* [PROT_NONE page][begin: RW span][PROT_NONE page] */
    void *m = mmap(NULL, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(m != MAP_FAILED);
    uint8_t *begin = (uint8_t *)m + page;
    REQUIRE(mprotect(begin, span, PROT_READ | PROT_WRITE) == 0);
    uint8_t *p = mode & 2u ? begin : begin + span - n;
    for (size_t i = 0; i < n; i++) {
        if (mode & 4u) p[i] = (uint8_t)i;
        else if (mode & 8u) p[i] = byte;
        else p[i] = payload ? data[header + i % payload] : 0;
    }
    check(p, n, k);
    /* An empty span at the guard byte itself performs no read. */
    check(begin + span, 0, k);
    REQUIRE(munmap(m, mapped) == 0);
    return 0;
}
