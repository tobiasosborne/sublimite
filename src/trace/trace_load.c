#include "trace_format.h"

#include <string.h>

typedef struct trace_reader {
    os_file *file;
    uint64_t offset;
} trace_reader;

typedef enum trace_read_status {
    TRACE_READ_OK,
    TRACE_READ_END,
    TRACE_READ_ERROR,
} trace_read_status;

/* Only zero bytes at the start of a read counts as a clean end. */
static trace_read_status read_exact(trace_reader *r, void *data, size_t len) {
    unsigned char *dst = data;
    for (size_t copied = 0; copied < len;) {
        size_t got;
        if (os_file_pread(r->file, dst + copied, len - copied, r->offset, &got) != OS_OK)
            return TRACE_READ_ERROR;
        if (got == 0) return copied == 0 ? TRACE_READ_END : TRACE_READ_ERROR;
        r->offset += got;
        copied += got;
    }
    return TRACE_READ_OK;
}

void trace_fmt_dump_free(trace_loaded *d) {
    base_arena_free(&d->storage);
    *d = (trace_loaded){0};
}

static int load_input(trace_reader *r, trace_loaded *d) {
    trace_input_header h;
    switch (read_exact(r, &h, sizeof h)) {
    case TRACE_READ_END: return 0;   /* historical timing-only EDTRACE1 */
    case TRACE_READ_ERROR: return -1;
    case TRACE_READ_OK: break;
    }
    if (h.tag != TRACE_FMT_INPT_TAG || h.version != TRACE_FMT_VERSION ||
        h.rec_size != sizeof(trace_input_rec) || h.count > TRACE_INPUT_CAP ||
        h.dropped > UINT64_MAX - h.count) return -1;

    if (h.count) {
        size_t bytes = h.count * sizeof *d->in;
        d->in = base_arena_alloc(&d->storage, bytes, _Alignof(trace_input_rec));
        if (d->in == NULL || read_exact(r, d->in, bytes) != TRACE_READ_OK) return -1;
    }
    for (size_t i = 0; i < h.count; i++) {
        const trace_input_rec *rec = &d->in[i];
        if (rec->seq != h.dropped + i) return -1;
        if (rec->kind == TRACE_IN_INVALID || rec->kind >= TRACE_IN_KIND_COUNT) return -1;
        if (i > 0 && rec->t0_ns < d->in[i - 1].t0_ns) return -1;
        if ((rec->kind == TRACE_IN_KEY_DOWN || rec->kind == TRACE_IN_KEY_UP) &&
            rec->p.key.utf8_len > sizeof rec->p.key.utf8) return -1;
    }
    d->nin = h.count;
    d->in_dropped = h.dropped;
    d->has_input = 1;
    unsigned char trailing;
    return read_exact(r, &trailing, 1) == TRACE_READ_END ? 0 : -1;
}

int trace_fmt_load_dump(os_file *f, trace_loaded *d) {
    *d = (trace_loaded){0};
    trace_reader r = {.file = f};
    trace_file_header h;
    if (read_exact(&r, &h, sizeof h) != TRACE_READ_OK ||
        memcmp(h.magic, TRACE_FMT_MAGIC, sizeof h.magic) != 0 ||
        h.version != TRACE_FMT_VERSION || h.rec_size != sizeof(trace_rec) ||
        h.ring_cap != TRACE_RING_CAP || h.nrings > TRACE_MAX_THREADS) return -1;

    /* A lazy base reservation sized by the header's bounded ring count and
     * the format's input cap. Reading payloads touches only populated pages. */
    size_t timing_bytes = h.nrings * (size_t)TRACE_RING_CAP * sizeof(trace_rec);
    size_t input_bytes = TRACE_INPUT_CAP * sizeof(trace_input_rec);
    if (base_arena_init(&d->storage, timing_bytes + input_bytes) != 0) return -1;
    trace_rec *recs = base_arena_alloc(&d->storage, timing_bytes, _Alignof(trace_rec));
    if (recs == NULL) goto fail;

    uint32_t min_slot = 0;
    for (uint32_t i = 0; i < h.nrings; i++) {
        trace_ring_header b;
        if (read_exact(&r, &b, sizeof b) != TRACE_READ_OK || b.slot < min_slot ||
            b.slot >= TRACE_MAX_THREADS || b.count > TRACE_RING_CAP) goto fail;
        min_slot = b.slot + 1;
        if (!b.count) continue;

        trace_rec *block = recs + d->nrecs;
        if (read_exact(&r, block, b.count * sizeof *block) != TRACE_READ_OK) goto fail;
        for (size_t j = 0; j < b.count; j++) {
            if (block[j].thread != b.slot) goto fail;
        }
        d->nrecs += b.count;
    }

    if (load_input(&r, d) != 0) goto fail;
    if (d->nrecs) d->recs = recs;
    return 0;

fail:
    trace_fmt_dump_free(d);
    return -1;
}
