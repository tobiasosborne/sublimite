/* trace_fmt.h - dump file format and gate arithmetic shared by tests and tools/tracedump.
 *
 * File layout (native little-endian, x86 only):
 *   char     magic[8] = "EDTRACE1"
 *   uint32_t version (1), nrings, rec_size (16), ring_cap (65536)
 *   per ring: uint32_t ring_index, uint32_t count, count x trace_rec (oldest first)
 * Optional tagged section after the rings (P0.6; header version stays 1, old
 * loaders ignore trailing bytes):
 *   uint32_t tag 'INPT' (0x54504e49), version (1), rec_size (48), count (<= 16384)
 *   uint64_t dropped
 *   count x trace_input_rec (oldest first, seq == dropped + index)
 * Nothing may follow the section. See docs/decisions/P0.6.md.
 */
#ifndef EDITOR_TRACE_TRACE_FMT_H
#define EDITOR_TRACE_TRACE_FMT_H

#include "trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACE_FMT_INPT_TAG 0x54504e49u

_Static_assert(sizeof(trace_rec) == 16, "trace_rec must be 16 bytes");

typedef struct trace_gate_pair {
    uint32_t frame_id;
    uint64_t ns;
} trace_gate_pair;

/* A loaded dump. Free with trace_fmt_dump_free. */
typedef struct trace_dump {
    trace_rec *recs;            /* timing records, all rings concatenated */
    size_t nrecs;
    int has_input;              /* 1 if an INPT section was present */
    trace_input_rec *in;        /* input records, oldest first */
    size_t nin;
    uint64_t in_dropped;        /* events overwritten before the dump */
} trace_loaded;

/* Loads and validates a dump (old version-1 or with an INPT section). Returns 0 on
 * success; on failure returns -1 and *d is zeroed (nothing to free). Defined in trace_load.c. */
int trace_fmt_load_dump(FILE *f, trace_loaded *d);
void trace_fmt_dump_free(trace_loaded *d);

/* Timing records only (legacy entry point). Caller frees *out. Returns 0 on success. */
static inline int trace_fmt_load(FILE *f, trace_rec **out, size_t *n_out) {
    trace_loaded d;
    *out = NULL;
    *n_out = 0;
    if (trace_fmt_load_dump(f, &d) != 0) return -1;
    free(d.in);
    *out = d.recs;
    *n_out = d.nrecs;
    return 0;
}

static inline int trace_fmt_cmp_pair(const void *a, const void *b) {
    const trace_gate_pair *x = a, *y = b;
    if (x->frame_id != y->frame_id) return x->frame_id < y->frame_id ? -1 : 1;
    if (x->ns != y->ns) return x->ns < y->ns ? -1 : 1;
    return 0;
}

static inline int trace_fmt_cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* First occurrence (earliest ns) of event ev per frame_id, sorted by frame_id. */
static inline int trace_fmt_firsts(const trace_rec *r, size_t n, enum trace_ev ev,
                                   trace_gate_pair **out, size_t *nout) {
    size_t m = 0;
    trace_gate_pair *p = malloc((n ? n : 1) * sizeof *p);
    if (p == NULL) return -1;
    for (size_t i = 0; i < n; i++) {
        if (r[i].ev == (uint16_t)ev) {
            p[m].frame_id = r[i].frame_id;
            p[m].ns = r[i].ns;
            m++;
        }
    }
    qsort(p, m, sizeof *p, trace_fmt_cmp_pair);
    size_t k = 0;
    for (size_t i = 0; i < m; i++) {
        if (k == 0 || p[k - 1].frame_id != p[i].frame_id) {
            p[k++] = p[i];
        }
    }
    *out = p;
    *nout = k;
    return 0;
}

/* Latency to_ev - from_ev per frame_id (both first occurrences), in ns.
 * Frames missing either endpoint are skipped. Caller frees *lat. */
static inline int trace_fmt_latencies(const trace_rec *r, size_t n, enum trace_ev from,
                                      enum trace_ev to, uint64_t **lat, size_t *nlat) {
    trace_gate_pair *a = NULL, *b = NULL;
    size_t na = 0, nb = 0, i = 0, j = 0, k = 0;
    uint64_t *v = NULL;
    *lat = NULL;
    *nlat = 0;
    if (trace_fmt_firsts(r, n, from, &a, &na) != 0) return -1;
    if (trace_fmt_firsts(r, n, to, &b, &nb) != 0) { free(a); return -1; }
    v = malloc((na < nb ? na : nb) * sizeof *v + 1);
    if (v == NULL) { free(a); free(b); return -1; }
    while (i < na && j < nb) {
        if (a[i].frame_id < b[j].frame_id) {
            i++;
        } else if (b[j].frame_id < a[i].frame_id) {
            j++;
        } else {
            if (b[j].ns >= a[i].ns) v[k++] = b[j].ns - a[i].ns;
            i++;
            j++;
        }
    }
    free(a);
    free(b);
    *lat = v;
    *nlat = k;
    return 0;
}

/* Nearest-rank percentile, pct in 1..100. Sorts v in place. Returns 0 if n == 0. */
static inline uint64_t trace_fmt_pct(uint64_t *v, size_t n, unsigned pct) {
    size_t rank;
    if (n == 0) return 0;
    qsort(v, n, sizeof *v, trace_fmt_cmp_u64);
    rank = (n * pct + 99u) / 100u;   /* ceil(n * pct / 100) */
    if (rank < 1) rank = 1;
    return v[rank - 1];
}

#endif
