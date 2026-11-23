/* libFuzzer target for the journal wire codec (L2-2a): decode and
 * validate only -- no file, no repair. Properties, each checked
 * against a statement in journal.h rather than the codec's word:
 *   - stream validation reports a valid prefix inside the input, and
 *     that prefix alone revalidates as a whole valid image;
 *   - every record in a valid prefix decodes typed and re-encodes to
 *     the identical payload bytes, and reframes byte-identically;
 *   - a structured variant (valid preamble, one CRC-correct record
 *     with an arbitrary payload) reaches the schema checks instead of
 *     dying at magic/CRC, and anything it accepts is canonical;
 *   - journal_crc32c equals a bit-wise reference below.
 * libFuzzer hands the input over in an exact-size heap buffer, so any
 * read past the end is an ASan error. */
#include "journal/journal.h"

#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) __builtin_trap(); } while (0)

static uint32_t ref_crc32c(const uint8_t *p, size_t n)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (unsigned b = 0; b < 8; b++)
            c = (c >> 1) ^ ((c & 1u) ? 0x82f63b78u : 0u);
    }
    return ~c;
}
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v)
{ for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* Typed decode + payload re-encode must reproduce the frame's payload. */
static void check_typed(const journal_frame *f)
{
    static uint8_t re[1u << 20];
    size_t n = 0;
    journal_codec_err e = JOURNAL_CODEC_OK;
    switch (f->type) {
    case JOURNAL_REC_PAD:
    case JOURNAL_REC_GROUP_BEGIN:
    case JOURNAL_REC_GROUP_COMMIT:
        if (f->type != JOURNAL_REC_PAD) REQUIRE(f->payload_len == 0);
        return;
    case JOURNAL_REC_GENERATION: {
        journal_generation v;
        REQUIRE(journal_generation_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_generation_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_BASE: {
        journal_base v;
        REQUIRE(journal_base_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_base_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_TARGET: {
        journal_target v;
        REQUIRE(journal_target_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_target_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_INSERT: {
        journal_insert v;
        REQUIRE(journal_insert_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_insert_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_DELETE: {
        journal_delete v;
        REQUIRE(journal_delete_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_delete_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_RESTORE_BASE_RANGE: {
        journal_restore v;
        REQUIRE(journal_restore_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_restore_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_CHECKPOINT_BY_VALUE: {
        journal_checkpoint v;
        REQUIRE(journal_checkpoint_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_checkpoint_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_SAVE: {
        journal_save v;
        REQUIRE(journal_save_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_save_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_VIEW: {
        journal_view v;
        REQUIRE(journal_view_decode(f, &v) == JOURNAL_CODEC_OK);
        e = journal_view_encode(re, sizeof re, &n, &v);
        break;
    }
    case JOURNAL_REC_TABS: {
        uint32_t count = 0, active = 0;
        e = journal_tabs_decode(f, NULL, 0, &count, &active);
        REQUIRE(e == JOURNAL_CODEC_OK || e == JOURNAL_CODEC_NO_SPACE);
        journal_tabs_entry *entries = malloc((count ? count : 1) * sizeof *entries);
        REQUIRE(entries != NULL);
        REQUIRE(journal_tabs_decode(f, entries, count, &count, &active) == JOURNAL_CODEC_OK);
        e = journal_tabs_encode(re, sizeof re, &n, entries, count, active);
        free(entries);
        break;
    }
    }
    REQUIRE(e == JOURNAL_CODEC_OK);
    REQUIRE(n == f->payload_len && memcmp(re, f->payload, n) == 0);
    /* Reframing the validated record reproduces it byte for byte. */
    static uint8_t reframed[1u << 20];
    size_t total = 0;
    REQUIRE(journal_record_encode(reframed, sizeof reframed, &total, f->type,
                                  f->sequence, f->buffer_id, f->payload,
                                  f->payload_len, 0) == JOURNAL_CODEC_OK ||
            f->type == JOURNAL_REC_PAD); /* PAD length is offset-canonical */
    if (f->type != JOURNAL_REC_PAD) {
        REQUIRE(total == f->total_len);
        REQUIRE(memcmp(reframed + JOURNAL_HEADER_BYTES, f->payload,
                       f->payload_len) == 0);
    }
}

/* Walks a certified prefix record by record, checking each typed. */
static void check_prefix(const uint8_t *data, size_t valid_bytes)
{
    if (valid_bytes < JOURNAL_PREAMBLE_BYTES) {
        REQUIRE(valid_bytes == 0);
        return;
    }
    journal_stream_result again;
    REQUIRE(journal_stream_validate(data, valid_bytes, &again) == JOURNAL_CODEC_OK);
    REQUIRE(again.valid_bytes == valid_bytes);
    const uint8_t *stream = data + JOURNAL_PREAMBLE_BYTES;
    size_t off = 0, stream_len = valid_bytes - JOURNAL_PREAMBLE_BYTES;
    uint64_t expected = 1;
    while (off < stream_len) {
        journal_frame f;
        REQUIRE(journal_record_decode(stream + off, stream_len - off,
                                      (uint64_t)off, expected, &f) == JOURNAL_CODEC_OK);
        check_typed(&f);
        if (f.type != JOURNAL_REC_PAD) expected = f.sequence + 1;
        off += f.total_len;
    }
}

static void structured(const uint8_t *data, size_t size)
{
    if (size == 0 || size > 16384) return;
    static uint8_t img[16448];
    journal_preamble_encode(img, 1, 1);
    /* (P) the type byte modulo the dense type count exercises each type. */
    uint32_t type = data[0] % ((uint32_t)JOURNAL_REC_TABS + 1u);
    size_t n = size - 1;
    size_t total = JOURNAL_HEADER_BYTES + n;
    uint8_t *rec = img + JOURNAL_PREAMBLE_BYTES;
    memcpy(rec, "SJR1", 4);
    put32(rec + 4, (uint32_t)total);
    put32(rec + 8, type);
    put32(rec + 12, 0);
    bool need_buffer = !(type == JOURNAL_REC_PAD ||
                         type == JOURNAL_REC_GENERATION ||
                         type == JOURNAL_REC_GROUP_BEGIN ||
                         type == JOURNAL_REC_GROUP_COMMIT ||
                         type == JOURNAL_REC_TABS);
    put64(rec + 16, type == JOURNAL_REC_PAD ? 0 : 1);
    put64(rec + 24, need_buffer ? 9 : 0);
    memcpy(rec + JOURNAL_HEADER_BYTES, data + 1, n);
    put32(rec + 12, ref_crc32c(rec, total));
    journal_frame f;
    journal_codec_err e = journal_record_decode(rec, total, 0, 1, &f);
    if (e == JOURNAL_CODEC_OK) check_typed(&f);
    journal_stream_result res;
    (void)journal_stream_validate(img, JOURNAL_PREAMBLE_BYTES + total, &res);
    REQUIRE(res.valid_bytes <= JOURNAL_PREAMBLE_BYTES + total);
    check_prefix(img, res.valid_bytes);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size <= 8192)
        REQUIRE(journal_crc32c(data, size) == ref_crc32c(data, size));
    journal_stream_result res;
    journal_codec_err e = journal_stream_validate(data, size, &res);
    REQUIRE(res.valid_bytes <= size);
    if (e == JOURNAL_CODEC_FOREIGN || e == JOURNAL_CODEC_UNSUPPORTED ||
        (e == JOURNAL_CODEC_INVALID && res.valid_bytes == 0) ||
        e == JOURNAL_CODEC_TRUNCATED) {
        /* Preamble-level verdicts carry no certified stream prefix
         * beyond what valid_bytes says; recheck it if it names one. */
        if (res.valid_bytes >= JOURNAL_PREAMBLE_BYTES)
            check_prefix(data, res.valid_bytes);
    } else {
        check_prefix(data, res.valid_bytes);
    }
    structured(data, size);
    return 0;
}
