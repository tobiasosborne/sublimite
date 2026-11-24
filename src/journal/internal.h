/* journal module internals shared by journal.c (the live writer) and
 * recover.c (recovery). Not a public header: nothing outside
 * src/journal/ includes it. */
#ifndef JOURNAL_INTERNAL_H
#define JOURNAL_INTERNAL_H

#include "journal/journal.h"

#include <string.h>

/* The tag of every journal job; the completion message carries it. */
#define JOURNAL_JOB_GENERATION UINT32_C(0x4a524e32) /* "JRN2" */
#define JOURNAL_MSG_DONE UINT32_C(0x4a444f4e)       /* "JDON" */

/* A sibling name "<name>.<tag>-<16 hex digits of v>-<2 hex of n>" in out
 * (OS_NAME_MAX). (P) 1 + 6 + 1 + 16 + 1 + 2 = 27 B of suffix at most for
 * a tag of up to 6 bytes; the caller's name is ASSERTed to leave room
 * (JOURNAL_NAME_MAX below). */
#define JOURNAL_NAME_SUFFIX_MAX 27u
#define JOURNAL_NAME_MAX (OS_NAME_MAX - 1u - JOURNAL_NAME_SUFFIX_MAX)
static inline void journal_sibling_name(char out[OS_NAME_MAX], const char *name,
                                        const char *tag, uint64_t v, uint32_t n)
{
    static const char hex[] = "0123456789abcdef";
    size_t len = strlen(name), tl = strlen(tag);
    ASSERT(len <= JOURNAL_NAME_MAX && tl <= 6u);
    size_t at = 0;
    memcpy(out, name, len);
    at = len;
    out[at++] = '.';
    memcpy(out + at, tag, tl);
    at += tl;
    out[at++] = '-';
    for (int i = 15; i >= 0; i--) out[at++] = hex[(v >> (4 * i)) & 15u];
    out[at++] = '-';
    out[at++] = hex[(n >> 4) & 15u];
    out[at++] = hex[n & 15u];
    out[at] = 0;
}

/* Restarting positioned transfers (off the typing path). */
static inline os_err journal_write_all(os_file *f, const void *buf, size_t n, uint64_t off)
{
    const uint8_t *p = buf;
    while (n) {
        size_t done = 0;
        os_err e = os_file_pwrite(f, p, n, off, &done);
        if (e != OS_OK) return e;
        if (done == 0) return OS_E_IO;
        p += done;
        n -= done;
        off += done;
    }
    return OS_OK;
}

static inline os_err journal_read_all(os_file *f, void *buf, size_t n, uint64_t off, size_t *got)
{
    uint8_t *p = buf;
    *got = 0;
    while (*got < n) {
        size_t k = 0;
        os_err e = os_file_pread(f, p + *got, n - *got, off + *got, &k);
        if (e != OS_OK) return e;
        if (k == 0) break;
        *got += k;
    }
    return OS_OK;
}

/* Exclusive creation of a fresh sibling, retrying the counter on a
 * collision. (G) 256 attempts: the counter's two hex digits. */
static inline os_err journal_create_sibling(const os_dir *dir, const char *name,
                                            const char *tag, char out[OS_NAME_MAX],
                                            os_file *f)
{
    uint64_t v = os_wall_ns();
    for (uint32_t n = 0; n < 256u; n++) {
        journal_sibling_name(out, name, tag, v, n);
        os_err e = os_file_open_at(f, dir, out, OS_OPEN_RDWR | OS_OPEN_CREATE |
                                                OS_OPEN_EXCL | OS_OPEN_NOFOLLOW);
        if (e != OS_E_EXIST) return e;
    }
    return OS_E_EXIST;
}

/* The section-5 projection of a stat. */
static inline journal_wire_identity journal_identity_of(const os_stat *st)
{
    journal_wire_identity id = {
        .volume = st->id.volume, .index_low = st->id.index[0], .index_high = st->id.index[1],
        .size = st->size, .mtime_ns = st->mtime_ns, .ctime_ns = st->ctime_ns};
    return id;
}

static inline bool journal_identity_same(const journal_wire_identity *a,
                                         const journal_wire_identity *b)
{
    return a->volume == b->volume && a->index_low == b->index_low &&
           a->index_high == b->index_high && a->size == b->size &&
           a->mtime_ns == b->mtime_ns && a->ctime_ns == b->ctime_ns;
}

#endif
