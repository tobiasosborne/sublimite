/* trace_load.c - dump loader with full validation (P0.6). Off the typing path:
 * uses malloc. Rejects anything malformed without reading or writing out of bounds. */
#include "trace_fmt.h"

void trace_fmt_dump_free(trace_loaded *d) {
    free(d->recs);
    free(d->in);
    memset(d, 0, sizeof *d);
}

static int load_input(FILE *f, trace_loaded *d) {
    uint32_t sh[4];
    uint64_t dropped;
    size_t n;
    if (fread(sh, sizeof sh[0], 4, f) != 4) return -1;
    if (sh[0] != TRACE_FMT_INPT_TAG || sh[1] != 1u) return -1;
    if (sh[2] != sizeof(trace_input_rec) || sh[3] > TRACE_INPUT_CAP) return -1;
    if (fread(&dropped, sizeof dropped, 1, f) != 1) return -1;
    n = sh[3];
    if (n > 0) {
        d->in = malloc(n * sizeof *d->in);
        if (d->in == NULL) return -1;
        if (fread(d->in, sizeof *d->in, n, f) != n) return -1;
    }
    for (size_t i = 0; i < n; i++) {
        const trace_input_rec *r = &d->in[i];
        if (r->seq != dropped + i) return -1;
        if (r->kind == TRACE_IN_INVALID || r->kind >= TRACE_IN_KIND_COUNT) return -1;
        if (i > 0 && r->t0_ns < d->in[i - 1].t0_ns) return -1;
        if (r->kind == TRACE_IN_KEY_DOWN || r->kind == TRACE_IN_KEY_UP) {
            if (r->p.key.utf8_len > sizeof r->p.key.utf8) return -1;
        }
    }
    d->nin = n;
    d->in_dropped = dropped;
    d->has_input = 1;
    return 0;
}

int trace_fmt_load_dump(FILE *f, trace_loaded *d) {
    char magic[8];
    uint32_t hdr[4];
    size_t cap = 0;
    int c;

    memset(d, 0, sizeof *d);
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "EDTRACE1", 8) != 0) return -1;
    if (fread(hdr, sizeof hdr[0], 4, f) != 4) goto fail;
    if (hdr[0] != 1u || hdr[2] != sizeof(trace_rec) || hdr[3] != TRACE_RING_CAP) goto fail;
    if (hdr[1] > TRACE_MAX_THREADS) goto fail;

    for (uint32_t r = 0; r < hdr[1]; r++) {
        uint32_t blk[2];
        if (fread(blk, sizeof blk[0], 2, f) != 2) goto fail;
        if (blk[1] > hdr[3]) goto fail;
        if (d->nrecs + blk[1] > cap) {
            size_t ncap = cap ? cap * 2 : 4096;
            while (ncap < d->nrecs + blk[1]) ncap *= 2;
            trace_rec *tmp = realloc(d->recs, ncap * sizeof *tmp);
            if (tmp == NULL) goto fail;
            d->recs = tmp;
            cap = ncap;
        }
        if (blk[1] > 0 && fread(d->recs + d->nrecs, sizeof *d->recs, blk[1], f) != blk[1]) goto fail;
        d->nrecs += blk[1];
    }
    c = fgetc(f);
    if (c != EOF) {
        ungetc(c, f);
        if (load_input(f, d) != 0) goto fail;
        if (fgetc(f) != EOF) goto fail;   /* nothing may follow the section */
    }
    return 0;
fail:
    trace_fmt_dump_free(d);
    return -1;
}
