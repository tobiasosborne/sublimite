/* Owned editor clipboard storage. Linux retains its zero-copy X selection
 * handoff. Windows copies into the native clipboard worker's owned queue. */
#ifndef EDIT_EDITOR_CLIP_STORAGE_H
#define EDIT_EDITOR_CLIP_STORAGE_H
#if !defined(_WIN32)
#include "x11/clip.h"
#else
#include "x11/plat.h"
#define X11_CLIP_DATA_MAX (64u * 1024u * 1024u)
#define X11_CLIP_OWN_BUDGET (2u * X11_CLIP_DATA_MAX)
typedef void *(*editor_clip_alloc_fn)(void *, size_t);
typedef void (*editor_clip_free_fn)(void *, void *, size_t);
typedef struct editor_clip_storage {
    size_t len;
    editor_clip_free_fn release;
    void *ctx;
    uint8_t data[];
} x11_clip_buf;
static inline x11_clip_buf *x11_clip_buf_new_alloc(size_t len, editor_clip_alloc_fn alloc,
                                                  editor_clip_free_fn release, void *ctx)
{
    if (len > X11_CLIP_DATA_MAX) return NULL;
    x11_clip_buf *b = alloc(ctx, sizeof *b + len);
    if (b) { b->len = len; b->release = release; b->ctx = ctx; }
    return b;
}
static inline void x11_clip_buf_free(x11_clip_buf *b)
{ if (b) b->release(b->ctx, b, sizeof *b + b->len); }
static inline uint8_t *x11_clip_buf_data(x11_clip_buf *b) { return b->data; }
static inline size_t x11_clip_buf_len(const x11_clip_buf *b) { return b ? b->len : 0; }
static inline int x11_clip_set_buf(plat *p, int which, x11_clip_buf *b)
{
    int rc = plat_clip_set(p, which, b->data, b->len);
    x11_clip_buf_free(b); return rc;
}
#endif
#endif
