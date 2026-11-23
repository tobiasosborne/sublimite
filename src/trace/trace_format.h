#ifndef TRACE_FORMAT_H
#define TRACE_FORMAT_H

#include "trace.h"

/* EDTRACE1 wire layout (native little-endian, x86). The file header is
 * followed by nrings blocks, each a header and count timing records.
 * Startup stamps are prepended to slot zero within its record budget.
 * An optional INPT header followed by count ingress records ends the file.
 * The retained ingress sequence starts at dropped. */
#define TRACE_FMT_MAGIC "EDTRACE1"
#define TRACE_FMT_VERSION 1u
#define TRACE_FMT_INPT_TAG 0x54504e49u

typedef struct trace_file_header {
    char magic[sizeof TRACE_FMT_MAGIC - 1];
    uint32_t version, nrings, rec_size, ring_cap;
} trace_file_header;

typedef struct trace_ring_header {
    uint32_t slot, count;
} trace_ring_header;

typedef struct trace_input_header {
    uint32_t tag, version, rec_size, count;
    uint64_t dropped;
} trace_input_header;

_Static_assert(sizeof(trace_file_header) == 24, "EDTRACE1 file header");
_Static_assert(sizeof(trace_ring_header) == 8, "EDTRACE1 ring header");
_Static_assert(sizeof(trace_input_header) == 24, "EDTRACE1 input header");
_Static_assert(sizeof(trace_rec) == 16, "EDTRACE1 timing record");
_Static_assert(sizeof(trace_input_rec) == 48, "EDTRACE1 input record");

#endif
