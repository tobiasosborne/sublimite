/* Pure wire codec for journal.h sections 1-8 (L2-2a): encode, decode,
 * validate. No allocation, no I/O, no globals: the CRC table is built
 * in a caller-frame context per operation, and all state is local. */
#include "journal/journal.h"
#include "base/base.h"
#include "os/os.h"

#include <string.h>
#if defined(__x86_64__)
#include <nmmintrin.h>
#endif

/* ---- field-wise little-endian access (never struct-as-memory) ---- */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}
static int64_t rd_i64(const uint8_t *p)
{
    uint64_t u = rd64(p);
    int64_t v;
    memcpy(&v, &u, sizeof v);
    return v;
}
static void wr32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void wr64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void wr_i64(uint8_t *p, int64_t v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    wr64(p, u);
}
static void wr_identity(uint8_t *p, const journal_wire_identity *id)
{
    wr64(p, id->volume);
    wr64(p + 8, id->index_low);
    wr64(p + 16, id->index_high);
    wr64(p + 24, id->size);
    wr_i64(p + 32, id->mtime_ns);
    wr_i64(p + 40, id->ctime_ns);
}
static void rd_identity(const uint8_t *p, journal_wire_identity *id)
{
    id->volume = rd64(p);
    id->index_low = rd64(p + 8);
    id->index_high = rd64(p + 16);
    id->size = rd64(p + 24);
    id->mtime_ns = rd_i64(p + 32);
    id->ctime_ns = rd_i64(p + 40);
}
static bool identity_is_zero_bytes(const uint8_t *p)
{
    for (size_t i = 0; i < JOURNAL_IDENTITY_BYTES; i++)
        if (p[i]) return false;
    return true;
}
static bool identity_is_zero(const journal_wire_identity *id)
{
    return !id->volume && !id->index_low && !id->index_high && !id->size &&
           !id->mtime_ns && !id->ctime_ns;
}
static bool path_valid(const uint8_t *p, size_t n)
{
    if (!p || n == 0 || n > JOURNAL_PATH_MAX_BYTES || p[0] != '/') return false;
    return memchr(p, 0, n) == NULL;
}
static uint32_t prefix_expected(uint64_t size)
{
    return size < JOURNAL_PREFIX_BYTES ? (uint32_t)size : JOURNAL_PREFIX_BYTES;
}

/* ---- CRC32C: one context per operation, hardware or portable ---- */
typedef struct journal_crc {
    uint32_t table[256];
    bool hardware;
} journal_crc;

static void crc_init(journal_crc *crc)
{
    crc->hardware = false;
#if defined(__x86_64__)
    crc->hardware = base_cpu_has_sse42();
#endif
    if (!crc->hardware) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (unsigned k = 0; k < 8; k++)
                v = (v >> 1) ^ ((v & 1u) ? 0x82f63b78u : 0u);
            crc->table[i] = v;
        }
    }
}
#if defined(__x86_64__)
__attribute__((target("sse4.2")))
static uint32_t crc_hardware(uint32_t c, const uint8_t *p, size_t n)
{
    while (n >= 8) {
        uint64_t word;
        memcpy(&word, p, sizeof word);
        c = (uint32_t)_mm_crc32_u64(c, word);
        p += 8;
        n -= 8;
    }
    while (n) {
        c = _mm_crc32_u8(c, *p++);
        n--;
    }
    return c;
}
#endif
static uint32_t crc_update(const journal_crc *crc, uint32_t c,
                           const uint8_t *p, size_t n)
{
#if defined(__x86_64__)
    if (crc->hardware) return crc_hardware(c, p, n);
#endif
    for (size_t i = 0; i < n; i++)
        c = crc->table[(c ^ p[i]) & 255u] ^ (c >> 8);
    return c;
}
/* Section 3 coverage: the whole record with its CRC field as zeroes. */
static uint32_t crc_record(const journal_crc *crc, const uint8_t *p, size_t n)
{
    static const uint8_t zero[4] = {0};
    uint32_t c = crc_update(crc, UINT32_MAX, p, 12);
    c = crc_update(crc, c, zero, sizeof zero);
    return ~crc_update(crc, c, p + 16, n - 16);
}

uint32_t journal_crc32c(const uint8_t *data, size_t len)
{
    ASSERT(data != NULL || len == 0);
    journal_crc crc;
    crc_init(&crc);
    return ~crc_update(&crc, UINT32_MAX, data, len);
}

/* ---- preamble ---- */
void journal_preamble_encode(uint8_t out[JOURNAL_PREAMBLE_BYTES],
                             uint64_t generation_high_water,
                             uint64_t buffer_high_water)
{
    ASSERT(out != NULL);
    memcpy(out, "SJNL", 4);
    wr32(out + 4, JOURNAL_WIRE_VERSION);
    wr32(out + 8, JOURNAL_REQUIRED_FEATURES);
    wr64(out + 12, generation_high_water);
    wr64(out + 20, buffer_high_water);
    journal_crc crc;
    crc_init(&crc);
    wr32(out + 28, ~crc_update(&crc, UINT32_MAX, out, 28));
}

journal_codec_err journal_preamble_decode(const uint8_t *bytes, size_t len,
                                          journal_preamble *out)
{
    ASSERT(out != NULL);
    ASSERT(bytes != NULL || len == 0);
    *out = (journal_preamble){0};
    if (len < JOURNAL_PREAMBLE_BYTES) return JOURNAL_CODEC_TRUNCATED;
    if (memcmp(bytes, "SJNL", 4) != 0) return JOURNAL_CODEC_FOREIGN;
    journal_crc crc;
    crc_init(&crc);
    if (~crc_update(&crc, UINT32_MAX, bytes, 28) != rd32(bytes + 28))
        return JOURNAL_CODEC_INVALID;
    uint32_t version = rd32(bytes + 4);
    uint32_t features = rd32(bytes + 8);
    if (version != JOURNAL_WIRE_VERSION || features != JOURNAL_REQUIRED_FEATURES)
        return JOURNAL_CODEC_UNSUPPORTED;
    out->version = version;
    out->required_features = features;
    out->generation_high_water = rd64(bytes + 12);
    out->buffer_high_water = rd64(bytes + 20);
    return JOURNAL_CODEC_OK;
}

size_t journal_pad_length(uint64_t stream_offset)
{
    uint64_t residue = ((stream_offset % JOURNAL_PAGE_BYTES) + JOURNAL_PREAMBLE_BYTES)
                     % JOURNAL_PAGE_BYTES;
    if (residue == 0) return 0;
    size_t length = JOURNAL_PAGE_BYTES - (size_t)residue;
    return length < JOURNAL_HEADER_BYTES ? length + JOURNAL_PAGE_BYTES : length;
}

/* ---- per-payload validity (section 6), shared by encode and decode ---- */
static bool save_payload_valid(const uint8_t *p, size_t n)
{
    if (n < 12 || rd64(p + 4) == 0) return false;
    switch ((journal_save_phase)rd32(p)) {
    case JOURNAL_SAVE_PREPARED: {
        if (n < 136 || rd64(p + 20) == 0) return false;
        uint32_t presence = rd32(p + 44);
        if (presence > JOURNAL_PRESENT) return false;
        if (presence == JOURNAL_ABSENT && !identity_is_zero_bytes(p + 48))
            return false;
        uint32_t path_len = rd32(p + 132);
        return n == 136u + (size_t)path_len && path_valid(p + 136, path_len);
    }
    case JOURNAL_SAVE_FINISHED: {
        if (n < 40 || rd64(p + 12) == 0) return false;
        uint32_t path_len = rd32(p + 36);
        return n == 40u + (size_t)path_len && path_valid(p + 40, path_len);
    }
    case JOURNAL_SAVE_CANCELLED:
        return n == 36 && rd32(p + 28) <= JOURNAL_SAVE_CANCEL_UNTITLED &&
               (rd32(p + 32) & ~JOURNAL_SAVE_CANCEL_HAS_SAVED) == 0;
    }
    return false; /* unknown phase value */
}

static bool payload_valid(journal_record_type type, const uint8_t *p, size_t n)
{
    switch (type) {
    case JOURNAL_REC_PAD:
        for (size_t i = 0; i < n; i++)
            if (p[i]) return false;
        return true;
    case JOURNAL_REC_GENERATION: {
        if (n < 72 || rd64(p) == 0) return false;
        if (rd32(p + 56) != prefix_expected(rd64(p + 8 + 24))) return false;
        if (rd32(p + 56) == 0 && rd32(p + 60) != 0) return false;
        if ((rd32(p + 64) & ~JOURNAL_GENERATION_VALUE_ONLY) != 0) return false;
        uint32_t loc_len = rd32(p + 68);
        return n == 72u + (size_t)loc_len && path_valid(p + 72, loc_len);
    }
    case JOURNAL_REC_BASE:
        return n == 16;
    case JOURNAL_REC_CHECKPOINT_TARGET:
    case JOURNAL_REC_TARGET: {
        if (n < 60) return false;
        uint32_t binding = rd32(p), presence = rd32(p + 4);
        uint32_t path_len = rd32(p + 56);
        if (binding > JOURNAL_TARGET_DETACHED || presence > JOURNAL_PRESENT)
            return false;
        if (n != 60u + (size_t)path_len) return false;
        if (binding == JOURNAL_TARGET_UNTITLED)
            return presence == JOURNAL_ABSENT && path_len == 0 &&
                   identity_is_zero_bytes(p + 8);
        if (presence == JOURNAL_ABSENT && !identity_is_zero_bytes(p + 8))
            return false;
        return path_valid(p + 60, path_len);
    }
    case JOURNAL_REC_COMMAND_INSERT:
    case JOURNAL_REC_CHECKPOINT_INSERT:
    case JOURNAL_REC_INSERT:
        return n >= 9;
    case JOURNAL_REC_COMMAND_DELETE:
    case JOURNAL_REC_DELETE:
        return n == 16 && rd64(p + 8) != 0 &&
               rd64(p) <= UINT64_MAX - rd64(p + 8);
    case JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE:
    case JOURNAL_REC_RESTORE_BASE_RANGE:
        return n == 32 && rd64(p + 8) != 0 && rd64(p + 24) != 0 &&
               rd64(p + 16) <= UINT64_MAX - rd64(p + 24) &&
               rd64(p) <= UINT64_MAX - rd64(p + 24);
    case JOURNAL_REC_CHECKPOINT_BEGIN:
    case JOURNAL_REC_COMMAND_BEGIN:
    case JOURNAL_REC_CHECKPOINT_COMMIT:
    case JOURNAL_REC_COMMAND_COMMIT:
        return n == 0;
    case JOURNAL_REC_CHECKPOINT_BY_VALUE:
        return n == 36 && (rd32(p + 32) & ~JOURNAL_CHECKPOINT_HAS_SAVED) == 0;
    case JOURNAL_REC_SAVE:
        return save_payload_valid(p, n);
    case JOURNAL_REC_VIEW:
        return n >= 4 && n <= 4u + JOURNAL_VIEW_MAX_OPAQUE;
    case JOURNAL_REC_TABS: {
        if (n < 8 || (n - 8) % JOURNAL_TABS_ENTRY_BYTES != 0) return false;
        uint32_t count = rd32(p), active = rd32(p + 4);
        if ((uint64_t)count != (n - 8) / JOURNAL_TABS_ENTRY_BYTES) return false;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *e = p + 8 + (size_t)i * JOURNAL_TABS_ENTRY_BYTES;
            if (rd64(e) == 0 || rd32(e + 8) > JOURNAL_TABS_CLOSED_KEPT)
                return false;
        }
        if (active == JOURNAL_TABS_NO_ACTIVE) return true;
        if (active >= count) return false;
        const uint8_t *e = p + 8 + (size_t)active * JOURNAL_TABS_ENTRY_BYTES;
        return rd32(e + 8) == JOURNAL_TABS_OPEN;
    }
    }
    return false;
}

static bool buffer_rule_ok(journal_record_type type, uint64_t buffer_id)
{
    bool session = type == JOURNAL_REC_PAD || type == JOURNAL_REC_GENERATION ||
                   type == JOURNAL_REC_TABS;
    return session == (buffer_id == 0);
}

journal_codec_err journal_record_encode(uint8_t *out, size_t cap,
                                        size_t *out_len,
                                        journal_record_type type,
                                        uint64_t sequence, uint64_t buffer_id,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        uint64_t stream_offset)
{
    ASSERT(out != NULL && out_len != NULL);
    if ((uint32_t)type > (uint32_t)JOURNAL_REC_CHECKPOINT_TARGET) return JOURNAL_CODEC_INVALID;
    if (payload_len > 0 && payload == NULL) return JOURNAL_CODEC_INVALID;
    if (payload_len > (size_t)JOURNAL_MAX_RECORD_BYTES - JOURNAL_HEADER_BYTES)
        return JOURNAL_CODEC_INVALID;
    if (!buffer_rule_ok(type, buffer_id)) return JOURNAL_CODEC_INVALID;
    if (type == JOURNAL_REC_PAD) {
        if (sequence != 0) return JOURNAL_CODEC_INVALID;
        if (JOURNAL_HEADER_BYTES + payload_len != journal_pad_length(stream_offset))
            return JOURNAL_CODEC_INVALID;
    } else if (sequence == 0) {
        return JOURNAL_CODEC_INVALID;
    }
    if (!payload_valid(type, payload, payload_len)) return JOURNAL_CODEC_INVALID;
    size_t total = JOURNAL_HEADER_BYTES + payload_len;
    if (stream_offset > UINT64_MAX - JOURNAL_PREAMBLE_BYTES - total)
        return JOURNAL_CODEC_INVALID;
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    memcpy(out, "SJR1", 4);
    wr32(out + 4, (uint32_t)total);
    wr32(out + 8, (uint32_t)type);
    wr32(out + 12, 0);
    wr64(out + 16, sequence);
    wr64(out + 24, buffer_id);
    if (payload_len) memcpy(out + JOURNAL_HEADER_BYTES, payload, payload_len);
    journal_crc crc;
    crc_init(&crc);
    wr32(out + 12, crc_record(&crc, out, total));
    *out_len = total;
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_record_decode(const uint8_t *bytes, size_t len,
                                        uint64_t stream_offset,
                                        uint64_t expected_sequence,
                                        journal_frame *out)
{
    ASSERT(out != NULL);
    ASSERT(bytes != NULL || len == 0);
    ASSERT(expected_sequence >= 1);
    if (len < JOURNAL_HEADER_BYTES) return JOURNAL_CODEC_TRUNCATED;
    if (memcmp(bytes, "SJR1", 4) != 0) return JOURNAL_CODEC_INVALID;
    uint32_t total = rd32(bytes + 4);
    if (total < JOURNAL_HEADER_BYTES || total > JOURNAL_MAX_RECORD_BYTES)
        return JOURNAL_CODEC_INVALID;
    if (stream_offset > UINT64_MAX - JOURNAL_PREAMBLE_BYTES - total)
        return JOURNAL_CODEC_INVALID;
    if (total > len) return JOURNAL_CODEC_TRUNCATED;
    journal_crc crc;
    crc_init(&crc);
    if (crc_record(&crc, bytes, total) != rd32(bytes + 12))
        return JOURNAL_CODEC_INVALID;
    uint32_t type_word = rd32(bytes + 8);
    if (type_word > (uint32_t)JOURNAL_REC_CHECKPOINT_TARGET) return JOURNAL_CODEC_INVALID;
    journal_record_type type = (journal_record_type)type_word;
    uint64_t sequence = rd64(bytes + 16);
    uint64_t buffer_id = rd64(bytes + 24);
    if (type == JOURNAL_REC_PAD) {
        if (sequence != 0 || buffer_id != 0 ||
            (size_t)total != journal_pad_length(stream_offset))
            return JOURNAL_CODEC_INVALID;
    } else {
        if (!buffer_rule_ok(type, buffer_id)) return JOURNAL_CODEC_INVALID;
        if (sequence != expected_sequence) return JOURNAL_CODEC_SEQUENCE;
    }
    const uint8_t *payload = bytes + JOURNAL_HEADER_BYTES;
    size_t payload_len = (size_t)total - JOURNAL_HEADER_BYTES;
    if (!payload_valid(type, payload, payload_len)) return JOURNAL_CODEC_INVALID;
    out->type = type;
    out->sequence = sequence;
    out->buffer_id = buffer_id;
    out->payload = payload;
    out->payload_len = payload_len;
    out->total_len = total;
    return JOURNAL_CODEC_OK;
}

/* ---- typed payloads ----
 * Each encoder computes its exact size from the struct, refuses a
 * struct section 6 forbids (INVALID, checked before capacity), then
 * capacity (NO_SPACE), then writes field by field. Each decoder
 * gates on payload_valid first, so field reads are in bounds by
 * construction. */
static bool frame_is(const journal_frame *f, journal_record_type type)
{
    return f->type == type && payload_valid(type, f->payload, f->payload_len);
}

journal_codec_err journal_generation_encode(uint8_t *out, size_t cap,
                                            size_t *out_len,
                                            const journal_generation *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->key == 0 || in->prefix_len != prefix_expected(in->identity.size) ||
        (in->prefix_len == 0 && in->prefix_crc32c != 0) ||
        (in->flags & ~JOURNAL_GENERATION_VALUE_ONLY) != 0 ||
        !path_valid(in->location, in->location_len))
        return JOURNAL_CODEC_INVALID;
    size_t total = 72 + in->location_len;
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->key);
    wr_identity(out + 8, &in->identity);
    wr32(out + 56, in->prefix_len);
    wr32(out + 60, in->prefix_crc32c);
    wr32(out + 64, in->flags);
    wr32(out + 68, (uint32_t)in->location_len);
    memcpy(out + 72, in->location, in->location_len);
    *out_len = total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_generation_decode(const journal_frame *frame,
                                            journal_generation *out)
{
    ASSERT(frame != NULL && out != NULL);
    if (!frame_is(frame, JOURNAL_REC_GENERATION)) return JOURNAL_CODEC_INVALID;
    const uint8_t *p = frame->payload;
    out->key = rd64(p);
    rd_identity(p + 8, &out->identity);
    out->prefix_len = rd32(p + 56);
    out->prefix_crc32c = rd32(p + 60);
    out->flags = rd32(p + 64);
    out->location_len = rd32(p + 68);
    out->location = p + 72;
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_base_encode(uint8_t *out, size_t cap,
                                      size_t *out_len, const journal_base *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (cap < 16) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->generation_key);
    wr64(out + 8, in->content_id);
    *out_len = 16;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_base_decode(const journal_frame *frame, journal_base *out)
{
    ASSERT(frame != NULL && out != NULL);
    if (!frame_is(frame, JOURNAL_REC_BASE)) return JOURNAL_CODEC_INVALID;
    out->generation_key = rd64(frame->payload);
    out->content_id = rd64(frame->payload + 8);
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_target_encode(uint8_t *out, size_t cap,
                                        size_t *out_len, const journal_target *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if ((uint32_t)in->binding > JOURNAL_TARGET_DETACHED ||
        (uint32_t)in->presence > JOURNAL_PRESENT)
        return JOURNAL_CODEC_INVALID;
    if (in->binding == JOURNAL_TARGET_UNTITLED) {
        if (in->presence != JOURNAL_ABSENT || in->path_len != 0 ||
            !identity_is_zero(&in->expected))
            return JOURNAL_CODEC_INVALID;
    } else {
        if (!path_valid(in->path, in->path_len)) return JOURNAL_CODEC_INVALID;
        if (in->presence == JOURNAL_ABSENT && !identity_is_zero(&in->expected))
            return JOURNAL_CODEC_INVALID;
    }
    size_t total = 60 + in->path_len;
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr32(out, (uint32_t)in->binding);
    wr32(out + 4, (uint32_t)in->presence);
    wr_identity(out + 8, &in->expected);
    wr32(out + 56, (uint32_t)in->path_len);
    if (in->path_len) memcpy(out + 60, in->path, in->path_len);
    *out_len = total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_target_decode(const journal_frame *frame, journal_target *out)
{
    ASSERT(frame != NULL && out != NULL);
    if ((frame->type != JOURNAL_REC_TARGET &&
        frame->type != JOURNAL_REC_CHECKPOINT_TARGET) ||
        !payload_valid(frame->type, frame->payload, frame->payload_len))
        return JOURNAL_CODEC_INVALID;
    const uint8_t *p = frame->payload;
    out->binding = (journal_target_binding)rd32(p);
    out->presence = (journal_presence)rd32(p + 4);
    rd_identity(p + 8, &out->expected);
    out->path_len = rd32(p + 56);
    out->path = out->path_len ? p + 60 : NULL;
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_insert_encode(uint8_t *out, size_t cap,
                                        size_t *out_len, const journal_insert *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->len == 0 || in->bytes == NULL) return JOURNAL_CODEC_INVALID;
    if (in->len > SIZE_MAX - 8) return JOURNAL_CODEC_INVALID;
    size_t total = 8 + in->len;
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->destination);
    memcpy(out + 8, in->bytes, in->len);
    *out_len = total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_insert_decode(const journal_frame *frame, journal_insert *out)
{
    ASSERT(frame != NULL && out != NULL);
    if ((frame->type != JOURNAL_REC_INSERT &&
        frame->type != JOURNAL_REC_COMMAND_INSERT &&
        frame->type != JOURNAL_REC_CHECKPOINT_INSERT) ||
        !payload_valid(frame->type, frame->payload, frame->payload_len))
        return JOURNAL_CODEC_INVALID;
    out->destination = rd64(frame->payload);
    out->bytes = frame->payload + 8;
    out->len = frame->payload_len - 8;
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_delete_encode(uint8_t *out, size_t cap,
                                        size_t *out_len, const journal_delete *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->len == 0 || in->destination > UINT64_MAX - in->len)
        return JOURNAL_CODEC_INVALID;
    if (cap < 16) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->destination);
    wr64(out + 8, in->len);
    *out_len = 16;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_delete_decode(const journal_frame *frame, journal_delete *out)
{
    ASSERT(frame != NULL && out != NULL);
    if ((frame->type != JOURNAL_REC_DELETE &&
        frame->type != JOURNAL_REC_COMMAND_DELETE) ||
        !payload_valid(frame->type, frame->payload, frame->payload_len))
        return JOURNAL_CODEC_INVALID;
    out->destination = rd64(frame->payload);
    out->len = rd64(frame->payload + 8);
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_restore_encode(uint8_t *out, size_t cap,
                                         size_t *out_len, const journal_restore *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->generation_key == 0 || in->len == 0 ||
        in->source_offset > UINT64_MAX - in->len ||
        in->destination > UINT64_MAX - in->len)
        return JOURNAL_CODEC_INVALID;
    if (cap < 32) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->destination);
    wr64(out + 8, in->generation_key);
    wr64(out + 16, in->source_offset);
    wr64(out + 24, in->len);
    *out_len = 32;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_restore_decode(const journal_frame *frame, journal_restore *out)
{
    ASSERT(frame != NULL && out != NULL);
    if ((frame->type != JOURNAL_REC_RESTORE_BASE_RANGE &&
        frame->type != JOURNAL_REC_COMMAND_RESTORE_BASE_RANGE) ||
        !payload_valid(frame->type, frame->payload, frame->payload_len))
        return JOURNAL_CODEC_INVALID;
    out->destination = rd64(frame->payload);
    out->generation_key = rd64(frame->payload + 8);
    out->source_offset = rd64(frame->payload + 16);
    out->len = rd64(frame->payload + 24);
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_checkpoint_encode(uint8_t *out, size_t cap,
                                            size_t *out_len,
                                            const journal_checkpoint *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if ((in->flags & ~JOURNAL_CHECKPOINT_HAS_SAVED) != 0) return JOURNAL_CODEC_INVALID;
    if (cap < 36) return JOURNAL_CODEC_NO_SPACE;
    wr64(out, in->covered_sequence);
    wr64(out + 8, in->content_id);
    wr64(out + 16, in->saved_content_id);
    wr64(out + 24, in->text_len);
    wr32(out + 32, in->flags);
    *out_len = 36;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_checkpoint_decode(const journal_frame *frame,
                                            journal_checkpoint *out)
{
    ASSERT(frame != NULL && out != NULL);
    if (!frame_is(frame, JOURNAL_REC_CHECKPOINT_BY_VALUE)) return JOURNAL_CODEC_INVALID;
    const uint8_t *p = frame->payload;
    out->covered_sequence = rd64(p);
    out->content_id = rd64(p + 8);
    out->saved_content_id = rd64(p + 16);
    out->text_len = rd64(p + 24);
    out->flags = rd32(p + 32);
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_save_encode(uint8_t *out, size_t cap,
                                      size_t *out_len, const journal_save *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->token == 0) return JOURNAL_CODEC_INVALID;
    size_t total;
    switch (in->phase) {
    case JOURNAL_SAVE_PREPARED:
        if (in->prepared.new_key == 0 ||
            (uint32_t)in->prepared.expected_presence > JOURNAL_PRESENT ||
            (in->prepared.expected_presence == JOURNAL_ABSENT &&
             !identity_is_zero(&in->prepared.expected)) ||
            !path_valid(in->prepared.path, in->prepared.path_len))
            return JOURNAL_CODEC_INVALID;
        total = 136 + in->prepared.path_len;
        break;
    case JOURNAL_SAVE_FINISHED:
        if (in->finished.installed_key == 0 ||
            !path_valid(in->finished.path, in->finished.path_len))
            return JOURNAL_CODEC_INVALID;
        total = 40 + in->finished.path_len;
        break;
    case JOURNAL_SAVE_CANCELLED:
        if ((uint32_t)in->cancelled.outcome > JOURNAL_SAVE_CANCEL_UNTITLED ||
            (in->cancelled.flags & ~JOURNAL_SAVE_CANCEL_HAS_SAVED) != 0)
            return JOURNAL_CODEC_INVALID;
        total = 36;
        break;
    default:
        return JOURNAL_CODEC_INVALID;
    }
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr32(out, (uint32_t)in->phase);
    wr64(out + 4, in->token);
    switch (in->phase) {
    case JOURNAL_SAVE_PREPARED: {
        const struct journal_save_prepared *s = &in->prepared;
        wr64(out + 12, s->previous_key);
        wr64(out + 20, s->new_key);
        wr64(out + 28, s->snapshot_sequence);
        wr64(out + 36, s->saved_content_id);
        wr32(out + 44, (uint32_t)s->expected_presence);
        wr_identity(out + 48, &s->expected);
        wr64(out + 96, s->witness.volume);
        wr64(out + 104, s->witness.index_low);
        wr64(out + 112, s->witness.index_high);
        wr64(out + 120, s->witness.size);
        wr32(out + 128, s->witness.content_crc32c);
        wr32(out + 132, (uint32_t)s->path_len);
        memcpy(out + 136, s->path, s->path_len);
        break;
    }
    case JOURNAL_SAVE_FINISHED: {
        const struct journal_save_finished *s = &in->finished;
        wr64(out + 12, s->installed_key);
        wr64(out + 20, s->snapshot_sequence);
        wr64(out + 28, s->saved_content_id);
        wr32(out + 36, (uint32_t)s->path_len);
        memcpy(out + 40, s->path, s->path_len);
        break;
    }
    case JOURNAL_SAVE_CANCELLED: {
        const struct journal_save_cancelled *s = &in->cancelled;
        wr64(out + 12, s->checkpoint_commit_sequence);
        wr64(out + 20, s->saved_content_id);
        wr32(out + 28, (uint32_t)s->outcome);
        wr32(out + 32, s->flags);
        break;
    }
    }
    *out_len = total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_save_decode(const journal_frame *frame, journal_save *out)
{
    ASSERT(frame != NULL && out != NULL);
    if (!frame_is(frame, JOURNAL_REC_SAVE)) return JOURNAL_CODEC_INVALID;
    const uint8_t *p = frame->payload;
    journal_save result = {0};
    result.phase = (journal_save_phase)rd32(p);
    result.token = rd64(p + 4);
    switch (result.phase) {
    case JOURNAL_SAVE_PREPARED: {
        struct journal_save_prepared *s = &result.prepared;
        s->previous_key = rd64(p + 12);
        s->new_key = rd64(p + 20);
        s->snapshot_sequence = rd64(p + 28);
        s->saved_content_id = rd64(p + 36);
        s->expected_presence = (journal_presence)rd32(p + 44);
        rd_identity(p + 48, &s->expected);
        s->witness.volume = rd64(p + 96);
        s->witness.index_low = rd64(p + 104);
        s->witness.index_high = rd64(p + 112);
        s->witness.size = rd64(p + 120);
        s->witness.content_crc32c = rd32(p + 128);
        s->path_len = rd32(p + 132);
        s->path = p + 136;
        break;
    }
    case JOURNAL_SAVE_FINISHED: {
        struct journal_save_finished *s = &result.finished;
        s->installed_key = rd64(p + 12);
        s->snapshot_sequence = rd64(p + 20);
        s->saved_content_id = rd64(p + 28);
        s->path_len = rd32(p + 36);
        s->path = p + 40;
        break;
    }
    case JOURNAL_SAVE_CANCELLED: {
        struct journal_save_cancelled *s = &result.cancelled;
        s->checkpoint_commit_sequence = rd64(p + 12);
        s->saved_content_id = rd64(p + 20);
        s->outcome = (journal_save_cancel_outcome)rd32(p + 28);
        s->flags = rd32(p + 32);
        break;
    }
    }
    *out = result;
    return JOURNAL_CODEC_OK;
}

journal_codec_err journal_view_encode(uint8_t *out, size_t cap,
                                      size_t *out_len, const journal_view *in)
{
    ASSERT(out != NULL && out_len != NULL && in != NULL);
    if (in->opaque_len > JOURNAL_VIEW_MAX_OPAQUE) return JOURNAL_CODEC_INVALID;
    if (in->opaque_len > 0 && in->opaque == NULL) return JOURNAL_CODEC_INVALID;
    size_t total = 4 + in->opaque_len;
    if (cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr32(out, in->version);
    if (in->opaque_len) memcpy(out + 4, in->opaque, in->opaque_len);
    *out_len = total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_view_decode(const journal_frame *frame, journal_view *out)
{
    ASSERT(frame != NULL && out != NULL);
    if (!frame_is(frame, JOURNAL_REC_VIEW)) return JOURNAL_CODEC_INVALID;
    out->version = rd32(frame->payload);
    out->opaque_len = frame->payload_len - 4;
    out->opaque = out->opaque_len ? frame->payload + 4 : NULL;
    return JOURNAL_CODEC_OK;
}

static bool tabs_shape_ok(const journal_tabs_entry *entries, uint32_t count,
                          uint32_t active)
{
    if (count > 0 && entries == NULL) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].buffer_id == 0 ||
            (uint32_t)entries[i].membership > JOURNAL_TABS_CLOSED_KEPT)
            return false;
    }
    if (active == JOURNAL_TABS_NO_ACTIVE) return true;
    return active < count && entries[active].membership == JOURNAL_TABS_OPEN;
}
journal_codec_err journal_tabs_encode(uint8_t *out, size_t cap,
                                      size_t *out_len,
                                      const journal_tabs_entry *entries,
                                      uint32_t count, uint32_t active)
{
    ASSERT(out != NULL && out_len != NULL);
    if (!tabs_shape_ok(entries, count, active)) return JOURNAL_CODEC_INVALID;
    uint64_t total = 8u + (uint64_t)count * JOURNAL_TABS_ENTRY_BYTES;
    if ((uint64_t)cap < total) return JOURNAL_CODEC_NO_SPACE;
    wr32(out, count);
    wr32(out + 4, active);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *e = out + 8 + (size_t)i * JOURNAL_TABS_ENTRY_BYTES;
        wr64(e, entries[i].buffer_id);
        wr32(e + 8, (uint32_t)entries[i].membership);
    }
    *out_len = (size_t)total;
    return JOURNAL_CODEC_OK;
}
journal_codec_err journal_tabs_decode(const journal_frame *frame,
                                      journal_tabs_entry *entries,
                                      uint32_t cap, uint32_t *count_out,
                                      uint32_t *active_out)
{
    ASSERT(frame != NULL && count_out != NULL && active_out != NULL);
    ASSERT(entries != NULL || cap == 0);
    if (!frame_is(frame, JOURNAL_REC_TABS)) return JOURNAL_CODEC_INVALID;
    const uint8_t *p = frame->payload;
    uint32_t count = rd32(p);
    *count_out = count;
    *active_out = rd32(p + 4);
    if (cap < count) return JOURNAL_CODEC_NO_SPACE;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = p + 8 + (size_t)i * JOURNAL_TABS_ENTRY_BYTES;
        entries[i].buffer_id = rd64(e);
        entries[i].membership = (journal_tabs_membership)rd32(e + 8);
    }
    return JOURNAL_CODEC_OK;
}
