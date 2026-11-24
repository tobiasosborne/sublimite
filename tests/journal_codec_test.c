/* journal codec (L2-2a): round trip of every record kind incl. boundary
 * sizes, rejection of every malformed class in the brief, CRC32C against
 * the standard check value and a bit-wise reference on both CPU paths
 * (forced through --wrap=base_cpu_has_sse42), and stream validation of
 * groups, checkpoints and torn tails. Expected values come from
 * journal.h's wire section and the reference code below, never from the
 * codec's own output. */
#include "journal/journal.h"
#include "os/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/journal_oracle.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    fprintf(stderr, "%s:%d: FAIL ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* ---- CPU path forcing (no env switch; the link seam is the seam) ---- */
static bool force_portable;
bool __real_base_cpu_has_sse42(void);
bool __wrap_base_cpu_has_sse42(void)
{ return !force_portable && __real_base_cpu_has_sse42(); }

/* ---- bit-wise CRC-32/ISCSI reference (no table, no intrinsics) ---- */
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
static uint32_t get32(const uint8_t *p)
{ uint32_t v = 0; for (unsigned i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i); return v; }

/* Frames a record without any payload validation, CRC per journal.h
 * section 3 via the reference CRC: the crafter for decoder rejections. */
static size_t raw_record(uint8_t *out, uint32_t type, uint64_t seq,
                         uint64_t buffer_id, const uint8_t *payload, size_t n)
{
    size_t total = JOURNAL_HEADER_BYTES + n;
    memcpy(out, "SJR1", 4);
    put32(out + 4, (uint32_t)total);
    put32(out + 8, type);
    put32(out + 12, 0);
    put64(out + 16, seq);
    put64(out + 24, buffer_id);
    if (n) memcpy(out + JOURNAL_HEADER_BYTES, payload, n);
    put32(out + 12, ref_crc32c(out, total));
    return total;
}

/* A whole image under construction in caller storage. */
static uint8_t img[1u << 20];
static size_t img_len;
static uint64_t img_seq; /* next non-PAD sequence is img_seq + 1 */

static void img_reset(uint64_t gen_hw, uint64_t buf_hw)
{
    journal_preamble_encode(img, gen_hw, buf_hw);
    img_len = JOURNAL_PREAMBLE_BYTES;
    img_seq = 0;
}
static size_t img_stream_len(void) { return img_len - JOURNAL_PREAMBLE_BYTES; }
static uint8_t *plan_space;
static size_t plan_capacity;

static journal_codec_err validate_stream(const uint8_t *bytes, size_t length,
                                         journal_stream_result *out)
{
    base_arena arena = { plan_space, plan_capacity, 0 };
    journal_logical_plan plan;
    journal_codec_err err = journal_plan_read(bytes, length, &arena, &plan);
    *out = plan.prefix;
    return err;
}

/* Appends one framed record through the codec; returns its stream offset. */
static size_t img_add(journal_record_type type, uint64_t buffer_id,
                      const uint8_t *payload, size_t n)
{
    size_t at = img_stream_len();
    size_t out_len = 0;
    uint64_t seq = type == JOURNAL_REC_PAD ? 0 : img_seq + 1;
    journal_codec_err e = journal_record_encode(img + img_len, sizeof img - img_len,
                                                &out_len, type, seq, buffer_id,
                                                payload, n, (uint64_t)at);
    CHECK(e == JOURNAL_CODEC_OK, "img_add type %d: err %d", (int)type, (int)e);
    if (e != JOURNAL_CODEC_OK) return at;
    img_len += out_len;
    if (type != JOURNAL_REC_PAD) img_seq++;
    return at;
}

static bool identity_eq(const journal_wire_identity *a, const journal_wire_identity *b)
{
    return a->volume == b->volume && a->index_low == b->index_low &&
           a->index_high == b->index_high && a->size == b->size &&
           a->mtime_ns == b->mtime_ns && a->ctime_ns == b->ctime_ns;
}
static journal_wire_identity sample_identity(uint64_t size)
{
    journal_wire_identity id = { 0x1234, 99, 0, size, -1234567, 7654321 };
    return id;
}
static void fill_pattern(uint8_t *p, size_t n, unsigned seed)
{ for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i * 31u + (i >> 3)); }

/* Encodes payload bytes, frames them, decodes the frame back. */
static void roundtrip_frame(journal_record_type type, uint64_t buffer_id,
                            const uint8_t *payload, size_t n, journal_frame *frame)
{
    static uint8_t rec[1u << 20];
    size_t total = 0;
    journal_codec_err e = journal_record_encode(rec, sizeof rec, &total, type, 7,
                                                buffer_id, payload, n, 0);
    CHECK(e == JOURNAL_CODEC_OK, "frame encode type %d: %d", (int)type, (int)e);
    CHECK(total == JOURNAL_HEADER_BYTES + n, "frame size type %d", (int)type);
    CHECK(memcmp(rec, "SJR1", 4) == 0 && get32(rec + 4) == total,
          "frame header is explicit bytes, type %d", (int)type);
    e = journal_record_decode(rec, total, 0, 7, frame);
    CHECK(e == JOURNAL_CODEC_OK, "frame decode type %d: %d", (int)type, (int)e);
    CHECK(frame->type == type && frame->sequence == 7 &&
          frame->buffer_id == buffer_id && frame->payload_len == n &&
          frame->total_len == total &&
          (n == 0 || memcmp(frame->payload, payload, n) == 0),
          "frame fields type %d", (int)type);
}

static void test_crc(void)
{
    static uint8_t buf[100003];
    fill_pattern(buf, sizeof buf, 5);
    for (int path = 0; path < 2; path++) {
        force_portable = path == 0;
        CHECK(journal_crc32c(NULL, 0) == 0, "crc empty path %d", path);
        CHECK(journal_crc32c((const uint8_t *)"123456789", 9) == 0xE3069283u,
              "crc standard check value path %d", path);
        CHECK(journal_crc32c(buf, sizeof buf) == ref_crc32c(buf, sizeof buf),
              "crc matches bitwise reference path %d", path);
        CHECK(journal_crc32c(buf, 1) == ref_crc32c(buf, 1), "crc 1 byte path %d", path);
    }
    force_portable = false;
    uint32_t hw = journal_crc32c(buf, sizeof buf);
    force_portable = true;
    CHECK(journal_crc32c(buf, sizeof buf) == hw, "crc paths agree");
    force_portable = false;
}

static void test_preamble(void)
{
    uint8_t bytes[JOURNAL_PREAMBLE_BYTES];
    journal_preamble out;
    journal_preamble_encode(bytes, 41, 7);
    CHECK(memcmp(bytes, "SJNL", 4) == 0, "preamble magic bytes");
    CHECK(get32(bytes + 4) == 1 && get32(bytes + 8) == 0, "preamble version+features LE");
    CHECK(get32(bytes + 28) == ref_crc32c(bytes, 28), "preamble CRC covers [0,28)");
    CHECK(journal_preamble_decode(bytes, sizeof bytes, &out) == JOURNAL_CODEC_OK,
          "preamble round trip");
    CHECK(out.version == 1 && out.required_features == 0 &&
          out.generation_high_water == 41 && out.buffer_high_water == 7,
          "preamble fields");
    uint8_t bad[JOURNAL_PREAMBLE_BYTES];
    memcpy(bad, bytes, sizeof bad); bad[12] ^= 1u;
    CHECK(journal_preamble_decode(bad, sizeof bad, &out) == JOURNAL_CODEC_INVALID,
          "preamble bad CRC");
    memcpy(bad, bytes, sizeof bad); put32(bad + 4, 2); put32(bad + 28, ref_crc32c(bad, 28));
    CHECK(journal_preamble_decode(bad, sizeof bad, &out) == JOURNAL_CODEC_UNSUPPORTED,
          "preamble unknown version");
    memcpy(bad, bytes, sizeof bad); put32(bad + 8, 1u); put32(bad + 28, ref_crc32c(bad, 28));
    CHECK(journal_preamble_decode(bad, sizeof bad, &out) == JOURNAL_CODEC_UNSUPPORTED,
          "preamble unknown required feature");
    uint8_t v1[JOURNAL_PREAMBLE_BYTES] = {0};
    memcpy(v1, "JNL1", 4);
    CHECK(journal_preamble_decode(v1, sizeof v1, &out) == JOURNAL_CODEC_FOREIGN,
          "v1 JNL1 refused as foreign");
    memcpy(bad, bytes, sizeof bad); memcpy(bad, "XXXX", 4);
    CHECK(journal_preamble_decode(bad, sizeof bad, &out) == JOURNAL_CODEC_FOREIGN,
          "unknown magic foreign");
    CHECK(journal_preamble_decode(bytes, 31, &out) == JOURNAL_CODEC_TRUNCATED,
          "preamble truncated at 31");
    CHECK(journal_preamble_decode(bytes, 0, &out) == JOURNAL_CODEC_TRUNCATED,
          "preamble truncated at 0");
    CHECK(out.generation_high_water == 0 && out.buffer_high_water == 0,
          "preamble out zeroed on error");
}

static void test_pad(void)
{
    /* (P) Absolute residue includes the 32-byte preamble. The
     * exact-header and short cases are one stream byte apart. */
    CHECK(journal_pad_length(0) == 4064, "PAD at stream zero");
    CHECK(journal_pad_length(4032) == 32, "PAD fits exactly one header");
    CHECK(journal_pad_length(4033) == 4127, "PAD short remainder runs long");
    CHECK(journal_pad_length(4063) == 4097, "PAD one-byte remainder");
    CHECK(journal_pad_length(4064) == 0, "absolute alignment needs no PAD");
    CHECK(journal_pad_length(4096) == 4064, "PAD repeats after one page");
    CHECK(journal_pad_length(UINT64_MAX) == 4065, "PAD residue avoids u64 wrap");
    static uint8_t zeros[8192];
    journal_frame frame;
    uint8_t out[8192]; size_t out_len = 0;
    size_t pad_payload = journal_pad_length(64) - JOURNAL_HEADER_BYTES;
    CHECK(journal_record_encode(out, sizeof out, &out_len, JOURNAL_REC_PAD, 0, 0,
                                zeros, pad_payload, 64) == JOURNAL_CODEC_OK,
          "PAD encode at offset 64");
    CHECK(out_len == journal_pad_length(64), "PAD total length");
    CHECK(journal_record_decode(out, out_len, 64, 1, &frame) == JOURNAL_CODEC_OK &&
          frame.type == JOURNAL_REC_PAD && frame.sequence == 0,
          "PAD round trip");
    /* Encode rejects: nonzero PAD payload, non-canonical length. */
    uint8_t nonzero[JOURNAL_PAGE_BYTES] = {0}; nonzero[3] = 1;
    CHECK(journal_record_encode(out, sizeof out, &out_len, JOURNAL_REC_PAD, 0, 0,
                                nonzero, journal_pad_length(64) - JOURNAL_HEADER_BYTES, 64)
          == JOURNAL_CODEC_INVALID, "PAD payload must be zero");
    CHECK(journal_record_encode(out, sizeof out, &out_len, JOURNAL_REC_PAD, 0, 0,
                                zeros, 8, 64) == JOURNAL_CODEC_INVALID,
          "PAD non-canonical length rejected");
    /* Decode rejects a PAD at an aligned offset (writer emits none). */
    size_t n = raw_record(out, JOURNAL_REC_PAD, 0, 0, zeros, 4032);
    CHECK(journal_record_decode(out, n, 0, 1, &frame) == JOURNAL_CODEC_OK,
          "PAD at stream zero seals the absolute end");
    CHECK(journal_record_decode(out, n, 4064, 1, &frame) == JOURNAL_CODEC_INVALID,
          "PAD at aligned offset invalid");
    /* (P) Same residue as stream zero on the last representable page,
     * but there is no room for the preamble and seal at that position. */
    uint64_t last_page = UINT64_MAX - (JOURNAL_PAGE_BYTES - 1u);
    CHECK(journal_record_decode(out, n, last_page, 1, &frame) == JOURNAL_CODEC_INVALID &&
          journal_record_encode(out, sizeof out, &out_len, JOURNAL_REC_PAD, 0, 0,
                                zeros, n - JOURNAL_HEADER_BYTES, last_page) == JOURNAL_CODEC_INVALID,
          "full PAD append must fit absolute file position");
}

static void test_generation(void)
{
    static uint8_t payload[8192], path[JOURNAL_PATH_MAX_BYTES];
    path[0] = '/'; memset(path + 1, 'a', sizeof path - 1);
    journal_frame frame; size_t n = 0;
    journal_generation in = { .key = 9, .identity = sample_identity(100000),
        .prefix_len = JOURNAL_PREFIX_BYTES, .prefix_crc32c = 0xdeadbeefu,
        .flags = JOURNAL_GENERATION_VALUE_ONLY, .location = path,
        .location_len = sizeof path };
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_OK,
          "GENERATION encode longest path");
    CHECK(n == 72 + sizeof path, "GENERATION size 104+path total basis");
    roundtrip_frame(JOURNAL_REC_GENERATION, 0, payload, n, &frame);
    journal_generation out;
    CHECK(journal_generation_decode(&frame, &out) == JOURNAL_CODEC_OK, "GENERATION decode");
    CHECK(out.key == 9 && identity_eq(&out.identity, &in.identity) &&
          out.prefix_len == in.prefix_len && out.prefix_crc32c == in.prefix_crc32c &&
          out.flags == in.flags && out.location_len == sizeof path &&
          memcmp(out.location, path, sizeof path) == 0, "GENERATION fields");
    /* Smallest: empty source, one-byte location "/". */
    in.identity = sample_identity(0); in.prefix_len = 0; in.prefix_crc32c = 0;
    in.flags = 0; in.location = (const uint8_t *)"/"; in.location_len = 1;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_OK &&
          n == 73, "GENERATION smallest");
    /* Rejections on encode. */
    in.key = 0;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "key 0");
    in.key = 9; in.prefix_len = 3; /* empty source wants 0 */
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "prefix_len != min(size,4096)");
    in.prefix_len = 0; in.flags = 2u;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "unknown flag bit");
    in.flags = 0; in.location = (const uint8_t *)"rel"; in.location_len = 3;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "location not absolute");
    static const uint8_t nul_path[] = { '/', 'a', 0, 'b' };
    in.location = nul_path; in.location_len = sizeof nul_path;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "location embedded NUL");
    in.location = path; in.location_len = sizeof path + 1;
    CHECK(journal_generation_encode(payload, sizeof payload, &n, &in) == JOURNAL_CODEC_INVALID, "location overlong");
    in.location_len = 8;
    CHECK(journal_generation_encode(payload, 16, &n, &in) == JOURNAL_CODEC_NO_SPACE, "GENERATION no space");
}

static void test_base_target(void)
{
    uint8_t payload[8192]; journal_frame frame; size_t n = 0;
    journal_base bin = { 0, 55 }, bout;
    CHECK(journal_base_encode(payload, sizeof payload, &n, &bin) == JOURNAL_CODEC_OK && n == 16,
          "BASE empty origin 48 B record basis");
    roundtrip_frame(JOURNAL_REC_BASE, 3, payload, n, &frame);
    CHECK(journal_base_decode(&frame, &bout) == JOURNAL_CODEC_OK &&
          bout.generation_key == 0 && bout.content_id == 55, "BASE fields");
    bin.generation_key = UINT64_MAX; bin.content_id = UINT64_MAX;
    CHECK(journal_base_encode(payload, sizeof payload, &n, &bin) == JOURNAL_CODEC_OK,
          "BASE boundary values");

    static uint8_t path[JOURNAL_PATH_MAX_BYTES];
    path[0] = '/'; memset(path + 1, 't', sizeof path - 1);
    journal_target tin = { .binding = JOURNAL_TARGET_BOUND, .presence = JOURNAL_PRESENT,
        .expected = sample_identity(42), .path = path, .path_len = sizeof path }, tout;
    CHECK(journal_target_encode(payload, sizeof payload, &n, &tin) == JOURNAL_CODEC_OK &&
          n == 60 + sizeof path, "TARGET longest path");
    roundtrip_frame(JOURNAL_REC_TARGET, 3, payload, n, &frame);
    CHECK(journal_target_decode(&frame, &tout) == JOURNAL_CODEC_OK &&
          tout.binding == JOURNAL_TARGET_BOUND && tout.presence == JOURNAL_PRESENT &&
          identity_eq(&tout.expected, &tin.expected) && tout.path_len == sizeof path &&
          memcmp(tout.path, path, sizeof path) == 0, "TARGET fields");
    journal_target untitled = { .binding = JOURNAL_TARGET_UNTITLED,
        .presence = JOURNAL_ABSENT, .expected = {0}, .path = NULL, .path_len = 0 };
    CHECK(journal_target_encode(payload, sizeof payload, &n, &untitled) == JOURNAL_CODEC_OK && n == 60,
          "TARGET untitled");
    untitled.presence = JOURNAL_PRESENT;
    CHECK(journal_target_encode(payload, sizeof payload, &n, &untitled) == JOURNAL_CODEC_INVALID,
          "untitled must be absent");
    untitled.presence = JOURNAL_ABSENT; untitled.expected.size = 1;
    CHECK(journal_target_encode(payload, sizeof payload, &n, &untitled) == JOURNAL_CODEC_INVALID,
          "absent identity must be zero");
    tin.binding = (journal_target_binding)9;
    CHECK(journal_target_encode(payload, sizeof payload, &n, &tin) == JOURNAL_CODEC_INVALID,
          "unknown binding");
}

static void test_text_records(void)
{
    static uint8_t payload[1u << 20], bytes[1u << 20];
    journal_frame frame; size_t n = 0;
    journal_insert iin = { 10, (const uint8_t *)"x", 1 }, iout;
    CHECK(journal_insert_encode(payload, sizeof payload, &n, &iin) == JOURNAL_CODEC_OK && n == 9,
          "INSERT one byte");
    roundtrip_frame(JOURNAL_REC_INSERT, 4, payload, n, &frame);
    CHECK(journal_insert_decode(&frame, &iout) == JOURNAL_CODEC_OK &&
          iout.destination == 10 && iout.len == 1 && iout.bytes[0] == 'x', "INSERT fields");
    iin.len = 0;
    CHECK(journal_insert_encode(payload, sizeof payload, &n, &iin) == JOURNAL_CODEC_INVALID,
          "INSERT empty rejected");
    /* Largest legal record exactly: total == JOURNAL_MAX_RECORD_BYTES. */
    size_t big = (size_t)JOURNAL_MAX_RECORD_BYTES - 40;
    fill_pattern(bytes, big, 3);
    iin.destination = 0; iin.bytes = bytes; iin.len = big;
    CHECK(journal_insert_encode(payload, sizeof payload, &n, &iin) == JOURNAL_CODEC_OK,
          "INSERT at record maximum payload");
    roundtrip_frame(JOURNAL_REC_INSERT, 4, payload, n, &frame);
    iin.len = big + 1;
    uint8_t *rec = img; /* scratch frame target; expect refusal */
    size_t out_len = 0;
    CHECK(journal_insert_encode(payload, sizeof payload, &n, &iin) == JOURNAL_CODEC_OK,
          "payload alone may exceed a record");
    CHECK(journal_record_encode(rec, JOURNAL_MAX_RECORD_BYTES, &out_len,
                                JOURNAL_REC_INSERT, 1, 4, payload, n, 0)
          == JOURNAL_CODEC_INVALID, "record past maximum refused before sizing");

    journal_delete din = { UINT64_MAX - 5, 5 }, dout;
    CHECK(journal_delete_encode(payload, sizeof payload, &n, &din) == JOURNAL_CODEC_OK && n == 16,
          "DELETE at overflow boundary");
    roundtrip_frame(JOURNAL_REC_DELETE, 4, payload, n, &frame);
    CHECK(journal_delete_decode(&frame, &dout) == JOURNAL_CODEC_OK &&
          dout.destination == din.destination && dout.len == 5, "DELETE fields");
    din.len = 6;
    CHECK(journal_delete_encode(payload, sizeof payload, &n, &din) == JOURNAL_CODEC_INVALID,
          "DELETE destination+length overflow");
    din.len = 0;
    CHECK(journal_delete_encode(payload, sizeof payload, &n, &din) == JOURNAL_CODEC_INVALID,
          "DELETE zero length");

    journal_restore rin = { 7, 9, UINT64_MAX - 3, 3 }, rout;
    CHECK(journal_restore_encode(payload, sizeof payload, &n, &rin) == JOURNAL_CODEC_OK && n == 32,
          "RESTORE 64 B record basis");
    roundtrip_frame(JOURNAL_REC_RESTORE_BASE_RANGE, 4, payload, n, &frame);
    CHECK(journal_restore_decode(&frame, &rout) == JOURNAL_CODEC_OK &&
          rout.destination == 7 && rout.generation_key == 9 &&
          rout.source_offset == UINT64_MAX - 3 && rout.len == 3, "RESTORE fields");
    rin.len = 4;
    CHECK(journal_restore_encode(payload, sizeof payload, &n, &rin) == JOURNAL_CODEC_INVALID,
          "RESTORE source overflow");
    rin = (journal_restore){ UINT64_MAX - 1, 9, 0, 2 };
    CHECK(journal_restore_encode(payload, sizeof payload, &n, &rin) == JOURNAL_CODEC_INVALID,
          "RESTORE destination overflow");
    rin = (journal_restore){ 0, 0, 0, 1 };
    CHECK(journal_restore_encode(payload, sizeof payload, &n, &rin) == JOURNAL_CODEC_INVALID,
          "RESTORE key 0");
}

static void test_checkpoint_save_view_tabs(void)
{
    static uint8_t payload[16384];
    journal_frame frame; size_t n = 0;
    journal_checkpoint cin = { 12, 34, 30, 9999, JOURNAL_CHECKPOINT_HAS_SAVED }, cout;
    CHECK(journal_checkpoint_encode(payload, sizeof payload, &n, &cin) == JOURNAL_CODEC_OK && n == 36,
          "CHECKPOINT 68 B record basis");
    roundtrip_frame(JOURNAL_REC_CHECKPOINT_BY_VALUE, 5, payload, n, &frame);
    CHECK(journal_checkpoint_decode(&frame, &cout) == JOURNAL_CODEC_OK &&
          cout.covered_sequence == 12 && cout.content_id == 34 &&
          cout.saved_content_id == 30 && cout.text_len == 9999 &&
          cout.flags == JOURNAL_CHECKPOINT_HAS_SAVED, "CHECKPOINT fields");
    cin.flags = 2u;
    CHECK(journal_checkpoint_encode(payload, sizeof payload, &n, &cin) == JOURNAL_CODEC_INVALID,
          "checkpoint flags beyond HAS_SAVED gone (amendment C)");

    static const uint8_t dest[] = "/home/u/file.txt";
    journal_save sin = { .phase = JOURNAL_SAVE_PREPARED, .token = 77,
        .prepared = { .previous_key = 3, .new_key = 4, .snapshot_sequence = 20,
            .saved_content_id = 8, .expected_presence = JOURNAL_PRESENT,
            .expected = sample_identity(50),
            .witness = { 0x1234, 123, 0, 60, 0xabcdef01u },
            .path = dest, .path_len = sizeof dest - 1 } }, sout;
    CHECK(journal_save_encode(payload, sizeof payload, &n, &sin) == JOURNAL_CODEC_OK &&
          n == 136 + (sizeof dest - 1), "SAVE PREPARED size");
    roundtrip_frame(JOURNAL_REC_SAVE, 5, payload, n, &frame);
    CHECK(journal_save_decode(&frame, &sout) == JOURNAL_CODEC_OK &&
          sout.phase == JOURNAL_SAVE_PREPARED && sout.token == 77 &&
          sout.prepared.previous_key == 3 && sout.prepared.new_key == 4 &&
          sout.prepared.snapshot_sequence == 20 && sout.prepared.saved_content_id == 8 &&
          sout.prepared.expected_presence == JOURNAL_PRESENT &&
          identity_eq(&sout.prepared.expected, &sin.prepared.expected) &&
          sout.prepared.witness.size == 60 &&
          sout.prepared.witness.content_crc32c == 0xabcdef01u &&
          sout.prepared.path_len == sizeof dest - 1 &&
          memcmp(sout.prepared.path, dest, sizeof dest - 1) == 0, "SAVE PREPARED fields");
    journal_save fin = { .phase = JOURNAL_SAVE_FINISHED, .token = 77,
        .finished = { .installed_key = 4, .snapshot_sequence = 2,
            .saved_content_id = 8, .path = dest, .path_len = sizeof dest - 1 } };
    CHECK(journal_save_encode(payload, sizeof payload, &n, &fin) == JOURNAL_CODEC_OK &&
          n == 40 + (sizeof dest - 1), "SAVE FINISHED size");
    roundtrip_frame(JOURNAL_REC_SAVE, 5, payload, n, &frame);
    CHECK(journal_save_decode(&frame, &sout) == JOURNAL_CODEC_OK &&
          sout.phase == JOURNAL_SAVE_FINISHED && sout.finished.installed_key == 4,
          "SAVE FINISHED fields");
    journal_save can = { .phase = JOURNAL_SAVE_CANCELLED, .token = 77,
        .cancelled = { .checkpoint_commit_sequence = 31, .saved_content_id = 8,
            .outcome = JOURNAL_SAVE_CANCEL_CONFLICT,
            .flags = JOURNAL_SAVE_CANCEL_HAS_SAVED } };
    CHECK(journal_save_encode(payload, sizeof payload, &n, &can) == JOURNAL_CODEC_OK && n == 36,
          "SAVE CANCELLED size");
    roundtrip_frame(JOURNAL_REC_SAVE, 5, payload, n, &frame);
    CHECK(journal_save_decode(&frame, &sout) == JOURNAL_CODEC_OK &&
          sout.phase == JOURNAL_SAVE_CANCELLED &&
          sout.cancelled.outcome == JOURNAL_SAVE_CANCEL_CONFLICT &&
          sout.cancelled.checkpoint_commit_sequence == 31, "SAVE CANCELLED fields");
    can.phase = (journal_save_phase)9;
    CHECK(journal_save_encode(payload, sizeof payload, &n, &can) == JOURNAL_CODEC_INVALID,
          "unknown SAVE phase");
    sin.token = 0;
    CHECK(journal_save_encode(payload, sizeof payload, &n, &sin) == JOURNAL_CODEC_INVALID,
          "SAVE token 0");
    sin.token = 77; sin.prepared.new_key = 0;
    CHECK(journal_save_encode(payload, sizeof payload, &n, &sin) == JOURNAL_CODEC_INVALID,
          "PREPARED new key 0");
    sin.prepared.new_key = 3; /* equals previous */
    CHECK(journal_save_encode(payload, sizeof payload, &n, &sin) == JOURNAL_CODEC_OK,
          "PREPARED keys may name same generation value-wise is caller policy");
    /* Unknown phase on the wire (raw, CRC-valid). */
    uint8_t raw[256]; uint8_t pay[64] = {0}; put32(pay, 9); put64(pay + 4, 1);
    size_t rn = raw_record(raw, JOURNAL_REC_SAVE, 1, 5, pay, sizeof pay);
    CHECK(journal_record_decode(raw, rn, 0, 1, &frame) == JOURNAL_CODEC_INVALID,
          "wire unknown SAVE phase");

    static uint8_t opaque[300]; fill_pattern(opaque, sizeof opaque, 9);
    journal_view vin = { 4242, opaque, 0 }, vout;
    CHECK(journal_view_encode(payload, sizeof payload, &n, &vin) == JOURNAL_CODEC_OK && n == 4,
          "VIEW empty opaque, unknown version carried");
    vin.opaque_len = JOURNAL_VIEW_MAX_OPAQUE;
    CHECK(journal_view_encode(payload, sizeof payload, &n, &vin) == JOURNAL_CODEC_OK &&
          n == 4 + JOURNAL_VIEW_MAX_OPAQUE, "VIEW at bound");
    roundtrip_frame(JOURNAL_REC_VIEW, 5, payload, n, &frame);
    CHECK(journal_view_decode(&frame, &vout) == JOURNAL_CODEC_OK && vout.version == 4242 &&
          vout.opaque_len == JOURNAL_VIEW_MAX_OPAQUE &&
          memcmp(vout.opaque, opaque, JOURNAL_VIEW_MAX_OPAQUE) == 0, "VIEW fields");
    vin.opaque_len = JOURNAL_VIEW_MAX_OPAQUE + 1;
    CHECK(journal_view_encode(payload, sizeof payload, &n, &vin) == JOURNAL_CODEC_INVALID,
          "VIEW past bound");

    journal_tabs_entry ein[3] = { {10, JOURNAL_TABS_OPEN},
                                  {11, JOURNAL_TABS_CLOSED_KEPT},
                                  {12, JOURNAL_TABS_OPEN} };
    journal_tabs_entry eout[3]; uint32_t count = 0, active = 0;
    CHECK(journal_tabs_encode(payload, sizeof payload, &n, ein, 3, 2) == JOURNAL_CODEC_OK &&
          n == 8 + 3 * JOURNAL_TABS_ENTRY_BYTES, "TABS size");
    roundtrip_frame(JOURNAL_REC_TABS, 0, payload, n, &frame);
    CHECK(journal_tabs_decode(&frame, eout, 3, &count, &active) == JOURNAL_CODEC_OK &&
          count == 3 && active == 2 && eout[1].buffer_id == 11 &&
          eout[1].membership == JOURNAL_TABS_CLOSED_KEPT, "TABS fields");
    CHECK(journal_tabs_decode(&frame, eout, 2, &count, &active) == JOURNAL_CODEC_NO_SPACE &&
          count == 3, "TABS decode reports needed count");
    CHECK(journal_tabs_encode(payload, sizeof payload, &n, ein, 3, 1) == JOURNAL_CODEC_INVALID,
          "TABS active must name an OPEN entry");
    CHECK(journal_tabs_encode(payload, sizeof payload, &n, ein, 3, 3) == JOURNAL_CODEC_INVALID,
          "TABS active out of range");
    CHECK(journal_tabs_encode(payload, sizeof payload, &n, NULL, 0, JOURNAL_TABS_NO_ACTIVE)
          == JOURNAL_CODEC_OK && n == 8, "TABS empty");
    /* Product capacity from section 3: 1000 live + 16 closed-kept. */
    static journal_tabs_entry many[1016], many_out[1016];
    for (uint32_t i = 0; i < 1016; i++)
        many[i] = (journal_tabs_entry){ i + 1,
            i < 1000 ? JOURNAL_TABS_OPEN : JOURNAL_TABS_CLOSED_KEPT };
    CHECK(journal_tabs_encode(payload, sizeof payload, &n, many, 1016, 999) == JOURNAL_CODEC_OK &&
          n + JOURNAL_HEADER_BYTES == 12232, "TABS at product capacity is 12,232 B");
    roundtrip_frame(JOURNAL_REC_TABS, 0, payload, n, &frame);
    CHECK(journal_tabs_decode(&frame, many_out, 1016, &count, &active) == JOURNAL_CODEC_OK &&
          count == 1016 && many_out[1015].buffer_id == 1016, "TABS capacity round trip");
    /* Both owned marker pairs round trip with empty payloads. */
    roundtrip_frame(JOURNAL_REC_COMMAND_BEGIN, 5, NULL, 0, &frame);
    roundtrip_frame(JOURNAL_REC_COMMAND_COMMIT, 5, NULL, 0, &frame);
    roundtrip_frame(JOURNAL_REC_CHECKPOINT_BEGIN, 5, NULL, 0, &frame);
    roundtrip_frame(JOURNAL_REC_CHECKPOINT_COMMIT, 5, NULL, 0, &frame);
}

static void test_frame_rejections(void)
{
    uint8_t rec[512], bad[512]; journal_frame frame;
    uint8_t pay[16] = {0}; put64(pay, 0); put64(pay + 8, 5); /* DELETE 0,5 */
    size_t n = raw_record(rec, JOURNAL_REC_DELETE, 3, 4, pay, sizeof pay);
    CHECK(journal_record_decode(rec, n, 0, 3, &frame) == JOURNAL_CODEC_OK, "raw baseline");
    memcpy(bad, rec, n); memcpy(bad, "JNL1", 4);
    CHECK(journal_record_decode(bad, n, 0, 3, &frame) == JOURNAL_CODEC_INVALID, "bad record magic");
    memcpy(bad, rec, n); bad[40] ^= 1u;
    CHECK(journal_record_decode(bad, n, 0, 3, &frame) == JOURNAL_CODEC_INVALID, "bad CRC");
    CHECK(journal_record_decode(rec, n, 0, 2, &frame) == JOURNAL_CODEC_SEQUENCE, "out of sequence");
    CHECK(journal_record_decode(rec, n - 1, 0, 3, &frame) == JOURNAL_CODEC_TRUNCATED,
          "length crossing buffer end");
    CHECK(journal_record_decode(rec, 20, 0, 3, &frame) == JOURNAL_CODEC_TRUNCATED,
          "short header truncated");
    size_t m = raw_record(bad, 99, 3, 4, pay, sizeof pay);
    CHECK(journal_record_decode(bad, m, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "unknown record type refused, never skipped");
    m = raw_record(bad, JOURNAL_REC_DELETE, 3, 0, pay, sizeof pay);
    CHECK(journal_record_decode(bad, m, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "buffer id 0 on DELETE");
    m = raw_record(bad, JOURNAL_REC_DELETE, 3, 4, pay, 8);
    CHECK(journal_record_decode(bad, m, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "DELETE wrong payload size");
    /* A length field past the maximum is INVALID, never a size to read. */
    memcpy(bad, rec, n); put32(bad + 4, JOURNAL_MAX_RECORD_BYTES + 1u);
    CHECK(journal_record_decode(bad, sizeof bad, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "length past maximum");
    memcpy(bad, rec, n); put32(bad + 4, 31);
    CHECK(journal_record_decode(bad, n, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "length below header");
    /* RESTORE arithmetic overflow on the wire, CRC-valid. */
    uint8_t rp[32] = {0};
    put64(rp, 0); put64(rp + 8, 9); put64(rp + 16, UINT64_MAX - 2); put64(rp + 24, 5);
    m = raw_record(bad, JOURNAL_REC_RESTORE_BASE_RANGE, 3, 4, rp, sizeof rp);
    CHECK(journal_record_decode(bad, m, 0, 3, &frame) == JOURNAL_CODEC_INVALID,
          "wire RESTORE overflow");
    /* Sequence at the ceiling: UINT64_MAX is decodable as the last one. */
    m = raw_record(bad, JOURNAL_REC_DELETE, UINT64_MAX, 4, pay, sizeof pay);
    CHECK(journal_record_decode(bad, m, 0, UINT64_MAX, &frame) == JOURNAL_CODEC_OK,
          "sequence ceiling record");
    /* Encode-side capacity. */
    size_t out_len = 0;
    CHECK(journal_record_encode(bad, n - 1, &out_len, JOURNAL_REC_DELETE, 3, 4,
                                pay, sizeof pay, 0) == JOURNAL_CODEC_NO_SPACE,
          "frame encode no space");
    /* Payload decode of the wrong frame type is INVALID, out untouched. */
    journal_base bout = { 111, 222 };
    CHECK(journal_base_decode(&frame, &bout) == JOURNAL_CODEC_INVALID &&
          bout.generation_key == 111, "typed decode wrong type leaves out untouched");
}

/* Appends one encoded payload through img_add from a typed encoder call. */
#define IMG_PAYLOAD(type, buffer, enc_call) do { \
    uint8_t pay_[4200]; size_t pn_ = 0; \
    CHECK((enc_call) == JOURNAL_CODEC_OK, "img payload %s", #enc_call); \
    img_add(type, buffer, pay_, pn_); } while (0)

static void test_stream_valid(void)
{
    img_reset(3, 9);
    journal_generation gen = { .key = 3, .identity = sample_identity(4),
        .prefix_len = 4, .prefix_crc32c = journal_crc32c((const uint8_t *)"abcd", 4),
        .flags = 0, .location = (const uint8_t *)"/tmp/x", .location_len = 6 };
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    journal_base base = { 3, 1 };
    IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
    journal_insert ins = { 4, (const uint8_t *)"!", 1 };
    IMG_PAYLOAD(JOURNAL_REC_INSERT, 9, journal_insert_encode(pay_, sizeof pay_, &pn_, &ins));
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    journal_restore rst = { 5, 3, 0, 4 };
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &rst));
    journal_delete del = { 0, 1 };
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_DELETE, 9, journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    /* PAD to the sync boundary, then one more record after it. */
    size_t pad = journal_pad_length((uint64_t)img_stream_len());
    static uint8_t zeros[8192];
    CHECK(pad > 0, "stream not accidentally aligned");
    img_add(JOURNAL_REC_PAD, 0, zeros, pad - JOURNAL_HEADER_BYTES);
    CHECK(img_len % JOURNAL_PAGE_BYTES == 0, "PAD lands on boundary");
    journal_view view = { 1, (const uint8_t *)"abc", 3 };
    IMG_PAYLOAD(JOURNAL_REC_VIEW, 9, journal_view_encode(pay_, sizeof pay_, &pn_, &view));

    journal_stream_result res;
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK, "stream valid");
    CHECK(res.valid_bytes == img_len && res.last_sequence == img_seq &&
          res.preamble.generation_high_water == 3 && res.preamble.buffer_high_water == 9,
          "stream result fields");
    CHECK(res.records == img_seq, "records counts non-PAD records");

    /* Header-only image is logically empty and valid. */
    img_reset(0, 0);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK &&
          res.records == 0 && res.last_sequence == 0, "header-only image");

    /* Portable-encoded image validates on the hardware path and back. */
    force_portable = true;
    img_reset(1, 1);
    IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
    force_portable = false;
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK,
          "portable image on hardware path");
}

/* Builds: BASE(buf 9) then returns the stream offset of what follows. */
static size_t img_base_only(void)
{
    img_reset(0, 9);
    journal_base base = { 0, 1 };
    IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
    return img_stream_len();
}

static void test_stream_torn_and_invalid(void)
{
    journal_stream_result res;
    journal_insert ins = { 0, (const uint8_t *)"hello", 5 };
    journal_delete del = { 0, 1 };

    /* Torn ungrouped record: prefix stands before it. */
    size_t base_end = img_base_only();
    size_t at = img_stream_len();
    IMG_PAYLOAD(JOURNAL_REC_INSERT, 9, journal_insert_encode(pay_, sizeof pay_, &pn_, &ins));
    CHECK(at == base_end, "insert follows base");
    CHECK(validate_stream(img, img_len - 2, &res) == JOURNAL_CODEC_TRUNCATED,
          "torn INSERT");
    CHECK(res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end && res.records == 1,
          "torn INSERT prefix");
    /* Corrupt (bad CRC) final record is indistinguishable in effect. */
    img[img_len - 1] ^= 1u;
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end, "corrupt tail");

    /* Torn PAD: dropped like any torn record. */
    base_end = img_base_only();
    static uint8_t zeros[8192];
    size_t pad = journal_pad_length((uint64_t)img_stream_len());
    img_add(JOURNAL_REC_PAD, 0, zeros, pad - JOURNAL_HEADER_BYTES);
    CHECK(validate_stream(img, img_len - 10, &res) == JOURNAL_CODEC_TRUNCATED &&
          res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end, "torn PAD");

    /* An open command survives physically, along with another owner's key. */
    img_base_only();
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_DELETE, 9, journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    IMG_PAYLOAD(JOURNAL_REC_INSERT, 10, journal_insert_encode(pay_, sizeof pay_, &pn_, &ins));
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK &&
          res.valid_bytes == img_len && res.records == img_seq, "open command at EOF");
    /* Corruption stops at the offending member, with BEGIN retained. */
    img_base_only();
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    size_t member = img_len;
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_DELETE, 9, journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    img[member + 40] ^= 1u;
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == member && res.last_sequence == 2, "corrupt command member");
    img_base_only();
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK &&
          res.valid_bytes == img_len, "replacement BEGIN is physical");
    img_base_only();
    size_t commit = img_len;
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == commit, "stray COMMIT");
    img_base_only();
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    commit = img_len;
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == commit, "empty command COMMIT");

    /* CRC-correct illegal combinations stop at this record. */
    uint8_t literal[9] = {0};
    const struct { uint32_t type; uint64_t owner; size_t length; } bad[] = {
        {JOURNAL_REC_COMMAND_BEGIN, 0, 0}, {JOURNAL_REC_CHECKPOINT_COMMIT, 0, 0},
        {JOURNAL_REC_COMMAND_INSERT, 0, sizeof literal},
        {JOURNAL_REC_CHECKPOINT_INSERT, 10, sizeof literal},
        {JOURNAL_REC_COMMAND_INSERT, 10, sizeof literal},
        {JOURNAL_REC_CHECKPOINT_TARGET + 1u, 9, 0}
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        img_base_only();
        img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
        size_t invalid = img_len;
        img_len += raw_record(img + img_len, bad[i].type, img_seq + 1,
                              bad[i].owner, literal, bad[i].length);
        CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
              res.valid_bytes == invalid && res.last_sequence == img_seq,
              "illegal type/role/owner vector %zu", i);
    }

    /* Sequence gap: forge the second record's sequence, CRC refixed. */
    base_end = img_base_only();
    size_t rec_at = img_len;
    IMG_PAYLOAD(JOURNAL_REC_INSERT, 9, journal_insert_encode(pay_, sizeof pay_, &pn_, &ins));
    put64(img + rec_at + 16, 99);
    put32(img + rec_at + 12, 0);
    put32(img + rec_at + 12, ref_crc32c(img + rec_at, img_len - rec_at));
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_SEQUENCE &&
          res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end, "stream sequence gap");

    /* A whole v1 stream is foreign, header and all. */
    uint8_t v1[64] = {0}; memcpy(v1, "JNL1", 4); put32(v1 + 4, 32);
    CHECK(validate_stream(v1, sizeof v1, &res) == JOURNAL_CODEC_FOREIGN &&
          res.valid_bytes == 0, "v1 stream foreign without repair");
    /* Garbage after a valid prefix is never resynchronized over. */
    base_end = img_base_only();
    memcpy(img + img_len, "SJR1garbagegarbagegarbagegarbag", 31);
    CHECK(validate_stream(img, img_len + 31, &res) == JOURNAL_CODEC_TRUNCATED &&
          res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end, "tail garbage stops prefix");
}

/* A checkpoint for "hello", split at the literal boundary hel|lo.
 * Return the offset of the first impossible transition, or the final. */
static size_t img_checkpoint(uint64_t covered, uint64_t text_len,
                             bool second_slice, bool with_target, bool target_then_insert)
{
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    size_t descriptor = img_len;
    journal_checkpoint chk = { covered, 3, 3, text_len, JOURNAL_CHECKPOINT_HAS_SAVED };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, 9,
                journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
    journal_insert s1 = { 0, (const uint8_t *)"hel", 3 };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_INSERT, 9,
                journal_insert_encode(pay_, sizeof pay_, &pn_, &s1));
    journal_target target = { .binding = JOURNAL_TARGET_DETACHED, .presence = JOURNAL_PRESENT,
        .expected = sample_identity(5), .path = (const uint8_t *)"/tmp/y", .path_len = 6 };
    size_t target_at = img_len;
    if (target_then_insert) {
        IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_TARGET, 9,
                    journal_target_encode(pay_, sizeof pay_, &pn_, &target));
    }
    if (second_slice) {
        journal_insert s2 = { 3, (const uint8_t *)"lo", 2 };
        IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_INSERT, 9,
                    journal_insert_encode(pay_, sizeof pay_, &pn_, &s2));
    }
    if (with_target) {
        IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_TARGET, 9,
                    journal_target_encode(pay_, sizeof pay_, &pn_, &target));
    }
    size_t final = img_len;
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    if (covered >= 2) return descriptor; /* Fixture's BASE is seq 1, BEGIN is seq 2. */
    return target_then_insert ? target_at : final;
}

static journal_logical_plan read_plan(size_t length, journal_codec_err verdict)
{
    base_arena arena = { plan_space, plan_capacity, 0 };
    journal_logical_plan plan;
    CHECK(journal_plan_read(img, length, &arena, &plan) == verdict, "plan physical verdict");
    return plan;
}

static void test_stream_checkpoint_and_save(void)
{
    journal_stream_result res;
    img_base_only();
    img_checkpoint(1, 5, true, false, false);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK,
          "checkpoint valid");
    img_base_only();
    img_checkpoint(1, 5, true, true, false);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK,
          "checkpoint with TARGET last valid");
    img_base_only();
    size_t bad = img_checkpoint(1, 5, false, false, false); /* Only hel arrived. */
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == bad, "checkpoint coverage short");
    img_base_only();
    bad = img_checkpoint(2, 5, true, false, false); /* covered == BEGIN sequence */
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == bad, "covered not before BEGIN");
    img_base_only();
    bad = img_checkpoint(1, 5, true, false, true); /* TARGET before end of literal coverage. */
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == bad, "TARGET must be last");
    img_base_only();
    bad = img_len;
    journal_checkpoint chk = { 1, 3, 3, 0, 0 };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, 9,
                journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_INVALID &&
          res.valid_bytes == bad, "checkpoint outside slot");
    img_base_only();
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, 9,
                journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK,
          "empty checkpoint valid");
    size_t base_end = img_base_only();
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    chk.text_len = 5;
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, 9,
                journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
    journal_insert s1 = { 0, (const uint8_t *)"hel", 3 };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_INSERT, 9,
                journal_insert_encode(pay_, sizeof pay_, &pn_, &s1));
    CHECK(validate_stream(img, img_len, &res) == JOURNAL_CODEC_OK &&
          res.valid_bytes == img_len, "open checkpoint physically valid");
    journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
    CHECK(plan.owners && plan.owners->origins && !plan.owners->origins->older &&
          plan.owners->origins->test == JOURNAL_VERIFIED_BASE, "open checkpoint has no effect");

    /* SAVE(PREPARED) torn counts as absent: prefix ends before it. */
    base_end = img_base_only();
    journal_save prep = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
        .prepared = { .previous_key = 0, .new_key = 1, .snapshot_sequence = 1,
            .saved_content_id = 1, .expected_presence = JOURNAL_ABSENT,
            .expected = {0}, .witness = { 1, 2, 0, 0, 0 },
            .path = (const uint8_t *)"/tmp/z", .path_len = 6 } };
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &prep));
    CHECK(validate_stream(img, img_len - 3, &res) == JOURNAL_CODEC_TRUNCATED &&
          res.valid_bytes == JOURNAL_PREAMBLE_BYTES + base_end, "torn PREPARED absent");
    size_t prepared_end = img_len;
    journal_save fin = { .phase = JOURNAL_SAVE_FINISHED, .token = 1,
        .finished = { .installed_key = 1, .snapshot_sequence = 1,
            .saved_content_id = 1, .path = (const uint8_t *)"/tmp/z", .path_len = 6 } };
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &fin));
    plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_operation *last = plan.operations;
    while (last && last->next) last = last->next;
    CHECK(last && last->use == JOURNAL_UNRESOLVED && last->saved == 0,
          "FINISHED without declaration supplies no saved association");
    const journal_origin *pending_origin = plan.owners ? plan.owners->origins : NULL;
    CHECK(pending_origin && pending_origin->test == JOURNAL_INSTALLED_WITNESS &&
          plan.operations && pending_origin->source == plan.operations->next &&
          !pending_origin->source->saved,
          "missing declaration keeps only PREPARED's conditional installed candidate");

    img_len = prepared_end;
    img_seq--;
    const journal_generation gen = { .key = 1, .identity = {1, 2, 0, 0, 0, 0},
        .location = prep.prepared.path, .location_len = prep.prepared.path_len };
    size_t declaration = img_len;
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    plan = read_plan(img_len, JOURNAL_CODEC_OK);
    CHECK(plan.operations && plan.operations->next && plan.operations->next->next &&
          plan.operations->next->next->use == JOURNAL_STAGED,
          "installed declaration is staged before FINISHED");
    size_t finish_offset = img_len;
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &fin));
    plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_origin *origin = plan.owners ? plan.owners->origins : NULL;
    CHECK(origin && origin->source->final == finish_offset && origin->covered == 1 &&
          origin->test == JOURNAL_INSTALLED_WITNESS &&
          origin->source->saved == finish_offset &&
          origin->source->saved_test == JOURNAL_INSTALLED_WITNESS &&
          origin->source->prepared && origin->source->prepared->terminal == origin->source &&
          origin->source->displaced == origin->source->prepared->final,
          "FINISHED gives origin and saved evidence guarded by installed-path verification");

    journal_base installed = { 1, 1 };
    size_t use = img_len;
    IMG_PAYLOAD(JOURNAL_REC_BASE, 10,
                journal_base_encode(pay_, sizeof pay_, &pn_, &installed));
    plan = read_plan(img_len, JOURNAL_CODEC_OK);
    last = plan.operations;
    while (last && last->next) last = last->next;
    CHECK(last && last->final == use && last->use == JOURNAL_ELIGIBLE &&
          last->frames->declaration == declaration && last->frames->activation == finish_offset,
          "later BASE requires the declaration and FINISHED's verified activation");
}

static void img_literal(journal_record_type type, uint64_t owner,
                        uint64_t at, const char *text)
{
    journal_insert insert = { at, (const uint8_t *)text, strlen(text) };
    IMG_PAYLOAD(type, owner, journal_insert_encode(pay_, sizeof pay_, &pn_, &insert));
}

static void img_descriptor(uint64_t owner, uint64_t covered, uint64_t length)
{
    journal_checkpoint chk = { covered, 1, 0, length, 0 };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, owner,
                journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
}

static journal_frame plan_frame(size_t offset)
{
    journal_frame frame = {0};
    CHECK(journal_record_decode(img + offset, img_len - offset,
                                offset - JOURNAL_PREAMBLE_BYTES,
                                journal_oracle_word(img + offset + 16, 8),
                                &frame) == JOURNAL_CODEC_OK, "valid plan frame");
    return frame;
}

static void plan_text(const journal_logical_plan *plan, uint64_t owner,
                      bool checkpoint, const char *expected)
{
    const journal_owner_plan *buffer = plan->owners;
    while (buffer && buffer->owner != owner) buffer = buffer->next;
    CHECK(buffer != NULL, "cut names owner %llu", (unsigned long long)owner);
    if (!buffer) return;
    const journal_origin *origin = buffer->origins;
    bool by_value = origin && origin->test == JOURNAL_INDEPENDENT_TEXT;
    CHECK(by_value == checkpoint, "cut selects owner %llu checkpoint %d",
          (unsigned long long)owner, checkpoint);
    char text[sizeof "x!?"] = {0}; /* Longest expected fixture text. */
    size_t length = 0;
    uint64_t cutoff = origin ? origin->covered : 0;
    if (by_value) {
        for (const journal_frame_ref *f = origin->source->frames; f; f = f->next) {
            journal_frame frame = plan_frame(f->offset);
            if (frame.type != JOURNAL_REC_CHECKPOINT_INSERT) continue;
            journal_insert insert;
            if (journal_insert_decode(&frame, &insert) != JOURNAL_CODEC_OK ||
                insert.len >= sizeof text - length) {
                CHECK(false, "exact checkpoint literals");
                return;
            }
            memcpy(text + length, insert.bytes, insert.len);
            length += insert.len;
        }
    }
    for (const journal_operation *op = buffer->commands; op; op = op->next_owner_command) {
        if (op->final_sequence <= cutoff) continue;
        CHECK(op->use == JOURNAL_ELIGIBLE, "complete command is eligible");
        for (const journal_frame_ref *f = op->frames; f; f = f->next) {
            journal_frame frame = plan_frame(f->offset);
            if (frame.type == JOURNAL_REC_COMMAND_DELETE) {
                journal_delete del;
                if (journal_delete_decode(&frame, &del) != JOURNAL_CODEC_OK ||
                    del.destination > length || del.len > length - del.destination) {
                    CHECK(false, "command DELETE fits trial text");
                    return;
                }
                size_t end = (size_t)(del.destination + del.len);
                memmove(text + del.destination, text + end, length - end);
                length -= (size_t)del.len;
            } else {
                journal_insert ins;
                if (journal_insert_decode(&frame, &ins) != JOURNAL_CODEC_OK ||
                    ins.destination > length || ins.len >= sizeof text - length) {
                    CHECK(false, "command INSERT fits trial text");
                    return;
                }
                memmove(text + ins.destination + ins.len, text + ins.destination,
                        length - (size_t)ins.destination);
                memcpy(text + ins.destination, ins.bytes, ins.len);
                length += ins.len;
            }
        }
    }
    CHECK(length == strlen(expected) && memcmp(text, expected, length) == 0,
          "owner %llu expected %s, got %.*s", (unsigned long long)owner,
          expected, (int)length, text);
}

static void check_cut(size_t end, uint64_t owner, bool checkpoint, const char *text)
{
    journal_logical_plan plan = read_plan(end, JOURNAL_CODEC_OK);
    plan_text(&plan, owner, checkpoint, text);
}

static void test_checkpoint_replacement(void)
{
    /* §1.3: empty C, standalone restore of X, then D and one COMMIT.
     * Fixture X exceeds the existing prefix witness by one byte. */
    static char x[JOURNAL_PREFIX_BYTES + 2];
    memset(x, 'x', sizeof x - 1);
    img_reset(0, 0);
    journal_generation gen = { .key = 7, .identity = sample_identity(sizeof x),
        .prefix_len = JOURNAL_PREFIX_BYTES, .prefix_crc32c = journal_crc32c((uint8_t *)x, JOURNAL_PREFIX_BYTES),
        .location = (const uint8_t *)"/x", .location_len = sizeof "/x" - 1 };
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0, journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    journal_base base = { 7, 1 };
    IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
    journal_delete del = { sizeof x - 1, 1 };
    IMG_PAYLOAD(JOURNAL_REC_DELETE, 9, journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    del = (journal_delete){ 0, sizeof x - 1 };
    IMG_PAYLOAD(JOURNAL_REC_DELETE, 9, journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    uint64_t empty = img_seq;
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    img_descriptor(9, empty, 0);
    journal_restore restore = { 0, 7, 0, sizeof x - 1 };
    IMG_PAYLOAD(JOURNAL_REC_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));
    uint64_t covered = img_seq;
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    size_t descriptor = img_len;
    img_descriptor(9, covered, sizeof x - 1);
    img_literal(JOURNAL_REC_CHECKPOINT_INSERT, 9, 0, x);
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    gen.flags = JOURNAL_GENERATION_VALUE_ONLY;
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0, journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_origin *origin = plan.owners ? plan.owners->origins : NULL;
    CHECK(origin && origin->test == JOURNAL_INDEPENDENT_TEXT &&
          origin->covered == covered && origin->source->frames->offset == descriptor &&
          origin->older && !origin->older->older, "§1.3 only the replacement checkpoint committed");
}

static void test_named_cuts(void)
{
    /* §1.5 a–c: prefixes at complete record boundaries model the K/M cuts. */
    img_base_only();
    img_literal(JOURNAL_REC_INSERT, 9, 0, "x");
    uint64_t a = img_seq;
    img_literal(JOURNAL_REC_INSERT, 10, 0, "y");
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    img_descriptor(9, a, 1);
    size_t before_keys = img_len;
    img_literal(JOURNAL_REC_INSERT, 9, 1, "!");
    img_literal(JOURNAL_REC_INSERT, 10, 1, "!");
    check_cut(before_keys, 9, false, "x"); /* a/M: key lost. */
    check_cut(img_len, 9, false, "x!"); /* b/K,M: incomplete A, both keys. */
    check_cut(img_len, 10, false, "y!");
    img_literal(JOURNAL_REC_CHECKPOINT_INSERT, 9, 0, "x");
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    size_t sealed = img_len;
    check_cut(sealed, 9, true, "x!"); /* a/K,M: key inside checkpoint. */
    img_literal(JOURNAL_REC_INSERT, 9, 2, "?");
    check_cut(img_len, 9, true, "x!?"); /* c/K. */
    check_cut(sealed, 9, true, "x!"); /* c/M. */

    /* §1.5 d, e: B's atomic command and checkpoint finish during A's. */
    img_base_only();
    img_literal(JOURNAL_REC_INSERT, 9, 0, "x");
    a = img_seq;
    img_literal(JOURNAL_REC_INSERT, 10, 0, "y");
    uint64_t b = img_seq;
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    img_descriptor(9, a, 1);
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 10, NULL, 0);
    img_descriptor(10, b, 1);
    img_add(JOURNAL_REC_COMMAND_BEGIN, 10, NULL, 0);
    journal_delete del = { 0, 1 };
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_DELETE, 10,
                journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    img_literal(JOURNAL_REC_COMMAND_INSERT, 10, 0, "z");
    size_t before_command = img_len;
    check_cut(before_command, 9, false, "x");
    check_cut(before_command, 10, false, "y");
    img_add(JOURNAL_REC_COMMAND_COMMIT, 10, NULL, 0);
    check_cut(img_len, 10, false, "z");
    img_literal(JOURNAL_REC_CHECKPOINT_INSERT, 10, 0, "y");
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 10, NULL, 0);
    check_cut(img_len, 10, true, "z");
    check_cut(img_len, 9, false, "x");
    img_literal(JOURNAL_REC_CHECKPOINT_INSERT, 9, 0, "x");
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    check_cut(img_len, 9, true, "x");
    check_cut(img_len, 10, true, "z");

    /* §1.5 f: abandoned DELETE, then either kind of replacement. */
    img_base_only();
    img_literal(JOURNAL_REC_INSERT, 9, 0, "x");
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_DELETE, 9,
                journal_delete_encode(pay_, sizeof pay_, &pn_, &del));
    img_literal(JOURNAL_REC_INSERT, 10, 0, "y");
    size_t abandoned = img_len;
    uint64_t at_abort = img_seq;
    check_cut(abandoned, 9, false, "x");
    check_cut(abandoned, 10, false, "y");
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    img_literal(JOURNAL_REC_COMMAND_INSERT, 9, 1, "!");
    check_cut(img_len, 9, false, "x");
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    check_cut(img_len, 9, false, "x!");
    img_len = abandoned;
    img_seq = at_abort;
    img_literal(JOURNAL_REC_INSERT, 9, 1, "?");
    check_cut(img_len, 9, false, "x?");
}

static void test_flat_oracle(void)
{
    /* Include the EOF offset as well as one offset for every frame. */
    size_t offsets[JOURNAL_ORACLE_RECORDS + 1];
    uint8_t schedule[JOURNAL_ORACLE_RECORDS];
    for (size_t seed = 0; seed < sizeof schedule; seed++) {
        for (size_t i = 0; i < sizeof schedule; i++)
            schedule[i] = (uint8_t)((UINT64_C(0x9e3779b97f4a7c15) * (i + seed)) >> (i % 64));
        size_t frames = journal_oracle_image(img, sizeof img, offsets, schedule, sizeof schedule);
        CHECK(frames != 0, "oracle builds interleaved image");
        if (!frames) return;
        for (size_t i = 0; i <= frames; i++) {
            journal_logical_plan plan = read_plan(offsets[i], JOURNAL_CODEC_OK);
            CHECK(journal_oracle_compare(img, offsets, i, &plan),
                  "flat oracle schedule %zu at prefix %zu", seed, i);
        }
    }
}

static const journal_operation *operation_at(const journal_logical_plan *plan, size_t offset)
{
    for (const journal_operation *op = plan->operations; op; op = op->next)
        if (op->final == offset) return op;
    CHECK(false, "complete operation at file offset %zu", offset);
    return NULL;
}

static void test_operand_policy(void)
{
    img_base_only();
    journal_generation gen = { .key = 7, .identity = sample_identity(1),
        .prefix_len = 1, .location = (const uint8_t *)"/a", .location_len = sizeof "/a" - 1 };
    journal_restore restore = { 0, 7, 0, 1 };
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));
    size_t declaration = img_len;
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    size_t first_commit = img_len;
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));
    gen.flags = JOURNAL_GENERATION_VALUE_ONLY;
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    size_t second_commit = img_len;
    img_add(JOURNAL_REC_COMMAND_COMMIT, 9, NULL, 0);
    gen.flags = 0;
    size_t redeclaration = img_len;
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    size_t after_embargo = img_len;
    IMG_PAYLOAD(JOURNAL_REC_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));

    /* An abandoned member's key and a TABS-only id still reserve space
     * in the lineage. Neither requires a logical operation on its owner. */
    img_add(JOURNAL_REC_COMMAND_BEGIN, 9, NULL, 0);
    restore.generation_key = 99;
    IMG_PAYLOAD(JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE, 9,
                journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));
    journal_tabs_entry entry = { 77, JOURNAL_TABS_OPEN };
    IMG_PAYLOAD(JOURNAL_REC_TABS, 0,
                journal_tabs_encode(pay_, sizeof pay_, &pn_, &entry, 1, 0));
    journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_operation *op = operation_at(&plan, first_commit);
    CHECK(op && op->use == JOURNAL_UNRESOLVED && op->frames->use == JOURNAL_UNRESOLVED,
          "declaration after member cannot authorize COMMIT");
    op = operation_at(&plan, second_commit);
    CHECK(op && op->use == JOURNAL_UNRESOLVED && op->frames->use == JOURNAL_ELIGIBLE &&
          op->frames->declaration == declaration, "COMMIT honors intervening embargo");
    op = operation_at(&plan, redeclaration);
    CHECK(op && op->use == JOURNAL_UNRESOLVED, "redeclaration cannot remove embargo");
    op = operation_at(&plan, after_embargo);
    CHECK(op && op->use == JOURNAL_UNRESOLVED, "standalone restore still embargoed");
    CHECK(plan.generation_high_water == 99 && plan.buffer_high_water == 77,
          "abandoned range operand and TABS entry count toward high-waters");
}

static void test_cancelled_save(void)
{
    img_base_only();
    journal_save prep = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
        .prepared = { .new_key = 1, .snapshot_sequence = 1, .saved_content_id = 1,
            .witness = {1, 2, 0, 0, 0}, .path = (const uint8_t *)"/a",
            .path_len = sizeof "/a" - 1 } };
    size_t prepare = img_len;
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &prep));
    journal_generation gen = { .key = 1, .identity = {1, 2, 0, 0, 0, 0},
        .location = prep.prepared.path, .location_len = prep.prepared.path_len };
    IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0, journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
    journal_base staged = { 1, 1 };
    size_t before = img_len;
    IMG_PAYLOAD(JOURNAL_REC_BASE, 10, journal_base_encode(pay_, sizeof pay_, &pn_, &staged));
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    img_descriptor(9, 1, 0);
    size_t checkpoint = img_len;
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    journal_save cancel = { .phase = JOURNAL_SAVE_CANCELLED, .token = 1,
        .cancelled = { .checkpoint_commit_sequence = img_seq,
            .outcome = JOURNAL_SAVE_CANCEL_CONFLICT } };
    size_t orphan = img_len;
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 10, journal_save_encode(pay_, sizeof pay_, &pn_, &cancel));
    size_t terminal = img_len;
    IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &cancel));
    size_t after = img_len;
    IMG_PAYLOAD(JOURNAL_REC_BASE, 11, journal_base_encode(pay_, sizeof pay_, &pn_, &staged));

    journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_operation *op = operation_at(&plan, orphan);
    CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->prepared, "orphan terminal isolated by owner");
    op = operation_at(&plan, terminal);
    CHECK(op && op->use == JOURNAL_ELIGIBLE && op->prepared &&
          op->prepared->final == prepare && op->prepared->terminal == op &&
          op->checkpoint && op->checkpoint->final == checkpoint &&
          op->saved_test == JOURNAL_EXPECTED_BINDING && !op->displaced,
          "CANCELLED selects its checkpoint and can preserve only prior saved evidence");
    op = operation_at(&plan, before);
    CHECK(op && op->use == JOURNAL_UNRESOLVED, "BASE cannot use a staged declaration");
    op = operation_at(&plan, after);
    CHECK(op && op->use == JOURNAL_UNRESOLVED, "CANCELLED cannot activate a declaration");
}

static void test_checkpoint_metadata(void)
{
    img_base_only();
    img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
    img_descriptor(9, 1, 0);
    journal_target target = { .binding = JOURNAL_TARGET_UNTITLED };
    size_t independent = img_len;
    IMG_PAYLOAD(JOURNAL_REC_TARGET, 9, journal_target_encode(pay_, sizeof pay_, &pn_, &target));
    target = (journal_target){ .binding = JOURNAL_TARGET_BOUND,
        .path = (const uint8_t *)"/old", .path_len = sizeof "/old" - 1 };
    IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_TARGET, 9,
                journal_target_encode(pay_, sizeof pay_, &pn_, &target));
    size_t final = img_len;
    img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
    journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
    const journal_operation *op = operation_at(&plan, independent);
    CHECK(op && op->binding == independent, "independent TARGET remains effective");
    op = operation_at(&plan, final);
    CHECK(op && op->use == JOURNAL_ELIGIBLE && !op->binding && !op->saved,
          "stale checkpoint metadata cannot replace the independent binding");
    CHECK(plan.owners && plan.owners->origins && plan.owners->origins->source == op,
          "stale metadata still leaves the checkpoint text eligible");
}

/* Astra wave1-N2 MUST-FIX 1-4: her byte-level inputs are the fixtures. */
static void test_astra_n2(void)
{
    /* 1 (v2-c42): GENERATION key 1, identity volume 1 / inode low 2 /
     * size 0, prefix_len 0 but prefix CRC 1, path "/a": total 106 B. */
    {
        uint8_t pay[74] = {0}, rec[128];
        put64(pay, 1); put64(pay + 8, 1); put64(pay + 16, 2);
        put32(pay + 60, 1); put32(pay + 68, 2);
        pay[72] = '/'; pay[73] = 'a';
        size_t rn = raw_record(rec, JOURNAL_REC_GENERATION, 1, 0, pay, sizeof pay);
        journal_frame frame;
        CHECK(rn == 106, "Astra 1 frame is 106 B");
        CHECK(journal_record_decode(rec, rn, 0, 1, &frame) == JOURNAL_CODEC_INVALID,
              "Astra 1 empty prefix needs CRC 0 (decode)");
        journal_generation bad = { .key = 1, .identity = {1, 2, 0, 0, 0, 0},
            .prefix_crc32c = 1, .location = (const uint8_t *)"/a", .location_len = 2 };
        uint8_t out[128]; size_t pn = 0;
        CHECK(journal_generation_encode(out, sizeof out, &pn, &bad) == JOURNAL_CODEC_INVALID,
              "Astra 1 empty prefix needs CRC 0 (encode)");
        img_reset(0, 9);
        img_len += raw_record(img + img_len, JOURNAL_REC_GENERATION, 1, 0,
                              pay, sizeof pay);
        CHECK(journal_oracle_empty_prefix_crc(img, img_len) == JOURNAL_PREAMBLE_BYTES,
              "Astra 1 oracle flags the GENERATION frame");
        journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_INVALID);
        CHECK(plan.prefix.valid_bytes == JOURNAL_PREAMBLE_BYTES &&
              plan.prefix.records == 0 && plan.prefix.last_sequence == 0 &&
              !plan.operations,
              "Astra 1 INVALID at 32 with no declaration");
    }

    /* 2 (v2-o0t): preamble generation high-water 7, PREPARED new key 7. */
    {
        img_reset(7, 9);
        journal_base base = { 0, 17 };
        IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
        journal_save prep = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
            .prepared = { .previous_key = 0, .new_key = 7, .snapshot_sequence = 1,
                .saved_content_id = 17, .expected_presence = JOURNAL_ABSENT,
                .witness = {1, 2, 0, 0, 0}, .path = (const uint8_t *)"/a",
                .path_len = sizeof "/a" - 1 } };
        size_t prepared = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &prep));
        journal_generation gen = { .key = 7, .identity = {1, 2, 0, 0, 0, 0},
            .location = prep.prepared.path, .location_len = prep.prepared.path_len };
        IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                    journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
        journal_save fin = { .phase = JOURNAL_SAVE_FINISHED, .token = 1,
            .finished = { .installed_key = 7, .snapshot_sequence = 1,
                .saved_content_id = 17, .path = prep.prepared.path,
                .path_len = prep.prepared.path_len } };
        size_t finished = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &fin));
        journal_base reuse = { 7, 17 };
        size_t later = img_len;
        IMG_PAYLOAD(JOURNAL_REC_BASE, 10,
                    journal_base_encode(pay_, sizeof pay_, &pn_, &reuse));
        CHECK(journal_oracle_reused_new_key(img, img_len) == prepared,
              "Astra 2 oracle flags reused new key 7");
        journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
        CHECK(plan.prefix.valid_bytes == img_len && plan.prefix.records == 5,
              "Astra 2 records stay in the physical prefix");
        const journal_operation *op = operation_at(&plan, prepared);
        CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->terminal,
              "Astra 2 reused-key PREPARED claim is unresolved");
        op = operation_at(&plan, finished);
        CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->saved && !op->declaration,
              "Astra 2 FINISHED neither activates nor gives saved evidence");
        op = operation_at(&plan, later);
        CHECK(op && op->use == JOURNAL_UNRESOLVED,
              "Astra 2 following BASE(7) gets no activation");
    }

    /* 3 (v2-kbj): BASE(0,17) at s=1, PREPARED claims id 23, size 1 "x". */
    {
        img_reset(0, 9);
        journal_base base = { 0, 17 };
        IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
        uint32_t x_crc = ref_crc32c((const uint8_t *)"x", 1);
        journal_save prep = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
            .prepared = { .previous_key = 0, .new_key = 1, .snapshot_sequence = 1,
                .saved_content_id = 23, .expected_presence = JOURNAL_ABSENT,
                .witness = {1, 2, 0, 1, x_crc}, .path = (const uint8_t *)"/a",
                .path_len = sizeof "/a" - 1 } };
        size_t prepared = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &prep));
        journal_generation gen = { .key = 1, .identity = {1, 2, 0, 1, 0, 0},
            .prefix_len = 1, .prefix_crc32c = x_crc,
            .location = prep.prepared.path, .location_len = prep.prepared.path_len };
        IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0,
                    journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
        journal_save fin = { .phase = JOURNAL_SAVE_FINISHED, .token = 1,
            .finished = { .installed_key = 1, .snapshot_sequence = 1,
                .saved_content_id = 23, .path = prep.prepared.path,
                .path_len = prep.prepared.path_len } };
        size_t finished = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &fin));
        CHECK(journal_oracle_snapshot_vs_base(img, img_len) == prepared,
              "Astra 3 oracle flags snapshot against its BASE");
        journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
        CHECK(plan.prefix.valid_bytes == img_len && plan.prefix.records == 4,
              "Astra 3 physical prefix retained");
        const journal_operation *op = operation_at(&plan, prepared);
        CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->terminal,
              "Astra 3 contradicting snapshot claim is unresolved");
        op = operation_at(&plan, finished);
        CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->saved,
              "Astra 3 FINISHED gives no saved evidence for id 23");
        CHECK(plan.owners && plan.owners->origins &&
              plan.owners->origins->test == JOURNAL_VERIFIED_BASE &&
              !plan.owners->origins->older,
              "Astra 3 BASE origin retained, no installed origin");
    }

    /* 4 (v2-23u): checkpoint covered 1, unresolved restore at 5 is in
     * its required suffix, so CANCELLED at 7 cannot consume token 1. */
    {
        img_reset(0, 9);
        journal_base base = { 0, 17 };
        IMG_PAYLOAD(JOURNAL_REC_BASE, 9, journal_base_encode(pay_, sizeof pay_, &pn_, &base));
        img_add(JOURNAL_REC_CHECKPOINT_BEGIN, 9, NULL, 0);
        journal_checkpoint chk = { 1, 17, 0, 0, 0 };
        IMG_PAYLOAD(JOURNAL_REC_CHECKPOINT_BY_VALUE, 9,
                    journal_checkpoint_encode(pay_, sizeof pay_, &pn_, &chk));
        img_add(JOURNAL_REC_CHECKPOINT_COMMIT, 9, NULL, 0);
        uint64_t checkpoint_seq = img_seq;
        journal_restore restore = { 0, 7, 0, 1 };
        size_t bad_restore = img_len;
        IMG_PAYLOAD(JOURNAL_REC_RESTORE_BASE_RANGE, 9,
                    journal_restore_encode(pay_, sizeof pay_, &pn_, &restore));
        uint32_t x_crc = ref_crc32c((const uint8_t *)"x", 1);
        journal_save prep = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
            .prepared = { .previous_key = 0, .new_key = 8, .snapshot_sequence = 5,
                .saved_content_id = 18, .expected_presence = JOURNAL_ABSENT,
                .witness = {1, 2, 0, 1, x_crc}, .path = (const uint8_t *)"/a",
                .path_len = sizeof "/a" - 1 } };
        size_t prepared = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9, journal_save_encode(pay_, sizeof pay_, &pn_, &prep));
        journal_save cancel = { .phase = JOURNAL_SAVE_CANCELLED, .token = 1,
            .cancelled = { .checkpoint_commit_sequence = checkpoint_seq,
                .saved_content_id = 0, .outcome = JOURNAL_SAVE_CANCEL_CONFLICT,
                .flags = 0 } };
        size_t cancelled = img_len;
        IMG_PAYLOAD(JOURNAL_REC_SAVE, 9,
                    journal_save_encode(pay_, sizeof pay_, &pn_, &cancel));
        CHECK(journal_oracle_cancelled_bad_suffix(img, img_len) == cancelled,
              "Astra 4 oracle flags CANCELLED with unresolved suffix");
        journal_logical_plan plan = read_plan(img_len, JOURNAL_CODEC_OK);
        CHECK(plan.prefix.valid_bytes == img_len && plan.prefix.records == 7,
              "Astra 4 all seven records stay in the physical prefix");
        const journal_operation *op = operation_at(&plan, bad_restore);
        CHECK(op && op->use == JOURNAL_UNRESOLVED,
              "Astra 4 restore on undeclared key 7 is unresolved");
        op = operation_at(&plan, cancelled);
        CHECK(op && op->use == JOURNAL_UNRESOLVED && !op->checkpoint,
              "Astra 4 CANCELLED stays unresolved");
        op = operation_at(&plan, prepared);
        CHECK(op && op->use == JOURNAL_ELIGIBLE && !op->terminal,
              "Astra 4 PREPARED token is not consumed");
        bool expected_branch = false, installed_branch = false;
        if (plan.owners)
            for (const journal_origin *o = plan.owners->origins; o; o = o->older) {
                if (!o->source || o->source->final != prepared) continue;
                if (o->test == JOURNAL_EXPECTED_BINDING) expected_branch = true;
                if (o->test == JOURNAL_INSTALLED_WITNESS) installed_branch = true;
            }
        CHECK(expected_branch && installed_branch,
              "Astra 4 both PREPARED branches are preserved");
    }
}

static void test_workspace(void)
{
    /* (P) One key and token per supported tab (1000 + 16), 5 frames per
     * owner: PREPARED, declaration, both BEGINs and a command member. */
    enum { buffers = 1000 + 16, records = 5 * buffers };
    img_reset(0, 0);
    for (uint64_t i = 1; i <= buffers; i++) {
        journal_save prepared = { .phase = JOURNAL_SAVE_PREPARED, .token = 1,
            .prepared = { .new_key = i, .witness = {1, i, 0, 0, 0},
                .path = (const uint8_t *)"/a", .path_len = sizeof "/a" - 1 } };
        journal_generation gen = { .key = i, .identity = {1, i, 0, 0, 0, 0},
            .location = prepared.prepared.path, .location_len = prepared.prepared.path_len };
        IMG_PAYLOAD(JOURNAL_REC_SAVE, i, journal_save_encode(pay_, sizeof pay_, &pn_, &prepared));
        IMG_PAYLOAD(JOURNAL_REC_GENERATION, 0, journal_generation_encode(pay_, sizeof pay_, &pn_, &gen));
        img_add(JOURNAL_REC_COMMAND_BEGIN, i, NULL, 0);
        img_add(JOURNAL_REC_CHECKPOINT_BEGIN, i, NULL, 0);
        img_literal(JOURNAL_REC_COMMAND_INSERT, i, 0, "x");
    }
    size_t bound = journal_plan_workspace_size(records, buffers, buffers, buffers);
    uint8_t *memory = malloc(bound + 1);
    CHECK(bound && memory, "workspace bound is representable");
    if (!bound || !memory) { free(memory); return; }
    memory[bound] = 0xA5;
    base_arena arena = { memory, bound, 0 };
    journal_logical_plan plan;
    CHECK(journal_plan_read(img, img_len, &arena, &plan) == JOURNAL_CODEC_OK &&
          plan.prefix.records == records && plan.buffer_high_water == buffers &&
          plan.generation_high_water == buffers && arena.used <= bound &&
          memory[bound] == 0xA5, "workspace bound at R, B, D, T");
    CHECK(arena.used > 1, "populated workspace");
    if (arena.used > 1) {
        arena.size = arena.used - 1;
        arena.used = 1;
        CHECK(journal_plan_read(img, img_len, &arena, &plan) == JOURNAL_CODEC_NO_SPACE &&
              arena.used == 1 && !plan.operations && !plan.owners && !plan.prefix.valid_bytes,
              "undersized arena restores mark and certifies no boundary");
    }
    CHECK(journal_plan_workspace_size(SIZE_MAX, 0, 0, 0) == 0, "workspace checked overflow");
    free(memory);
}

int main(void)
{
    /* The oracle script is longer than the other small images.
     * Each frame can introduce at most one indexed owner, key or token.
     * The large workspace fixture supplies its own arena. */
    plan_capacity = journal_plan_workspace_size(JOURNAL_ORACLE_RECORDS, JOURNAL_ORACLE_RECORDS,
                                               JOURNAL_ORACLE_RECORDS, JOURNAL_ORACLE_RECORDS);
    plan_space = malloc(plan_capacity);
    if (!plan_space) return 1;
    test_crc();
    test_preamble();
    test_pad();
    test_generation();
    test_base_target();
    test_text_records();
    test_checkpoint_save_view_tabs();
    test_frame_rejections();
    test_stream_valid();
    test_stream_torn_and_invalid();
    test_stream_checkpoint_and_save();
    test_checkpoint_replacement();
    test_named_cuts();
    test_flat_oracle();
    test_operand_policy();
    test_cancelled_save();
    test_checkpoint_metadata();
    test_astra_n2();
    test_workspace();
    free(plan_space);
    if (failures) {
        fprintf(stderr, "journal_codec_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("journal_codec_test: PASS\n");
    return 0;
}





