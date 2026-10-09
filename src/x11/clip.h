/* clip.h - X selections (P2.2): own and request PRIMARY / CLIPBOARD as UTF8_STRING. Internal to x11. */
#ifndef EDITOR_X11_CLIP_H
#define EDITOR_X11_CLIP_H
#include "plat.h"
#include <xcb/xcb.h>
bool x11_push_event(plat *p, const plat_event *ev);   /* x11.c: append to the input queue */
int  x11_clip_init(plat *p);
void x11_clip_destroy(plat *p);
/* Handles SELECTION_REQUEST/CLEAR/NOTIFY and PROPERTY_NOTIFY; returns true and fills ev if a plat event should be queued. */
bool x11_clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev);
/* Nonblocking reply completion; true when state changed (the loop must drain again). */
bool x11_clip_poll(plat *p);
/* Limits (defaults: up to 256 KiB INCR chunk and threshold, 5 s peer timeout, 1 s clipboard-manager save wait).
 * Any argument 0 keeps the current value, except save_timeout_ms where UINT32_MAX disables the save. Tests shrink them. */
void x11_clip_set_limits(plat *p, size_t incr_chunk, uint32_t timeout_ms, uint32_t save_timeout_ms);
/* Memory and slice accounting (P2.2d). mem = every clipboard byte alive: owned/shared blobs, the paste buffer and
 * receive buffers (capacity). budget defaults to 128 MiB; tests shrink it. max_slice = most bytes copied, converted or
 * written inside one x11_clip_poll / x11_clip_event call since the last reset (reset != 0 clears it). */
size_t x11_clip_mem(const plat *p);
void   x11_clip_set_budget(plat *p, size_t bytes);
size_t x11_clip_max_slice(plat *p, bool reset);
/* In-flight transfers (serving INCR/MULTIPLE jobs plus receives that are not idle): 0 when quiescent. */
size_t x11_clip_busy(const plat *p);
/* Earliest monotonic deadline (ns, trace_now_ns clock) the loop must wake for, 0 = none. */
uint64_t x11_clip_deadline(const plat *p);
/* On exit: ask a CLIPBOARD_MANAGER to save what we own (bounded wait). Called by plat_shutdown. */
void x11_clip_save_on_exit(plat *p);
/* Bounded wire reply decoders shared with tests/fuzzing; no X connection or allocation.
 * decode_pairs needs out[2*cap]; a MULTIPLE list is ATOM_PAIR/32, nonempty and even. */
typedef struct x11_clip_property {
    const uint8_t *data;
    size_t len;
    uint32_t type, after;
    uint8_t format;
} x11_clip_property;
/* Match the complete conversion tuple; callers additionally require an active request. */
bool x11_clip_notify_matches(const xcb_selection_notify_event_t *n, xcb_window_t win, xcb_atom_t selection,
                             xcb_atom_t target, xcb_atom_t property, uint32_t time);
bool x11_clip_decode_property(const uint8_t *bytes, size_t size, x11_clip_property *v);
/* Returns -1 invalid, 0 text, 1 INCR header. INCR lower bounds above 64 MiB are rejected. */
int x11_clip_decode_transfer(const x11_clip_property *v, xcb_atom_t utf8, xcb_atom_t textplain,
                             xcb_atom_t incr, xcb_atom_t expected, bool allow_incr);
bool x11_clip_decode_pairs(const x11_clip_property *v, xcb_atom_t atom_pair, xcb_atom_t *out, size_t cap, size_t *n);
/* Pure helpers, exposed for tests. latin1: UTF-8 -> ISO-8859-1 ('?' for unrepresentable). Returns bytes written (<= len). */
size_t x11_utf8_to_latin1(const uint8_t *s, size_t len, uint8_t *out);
/* latin1 -> UTF-8; out needs 2*len bytes. */
size_t x11_latin1_to_utf8(const uint8_t *s, size_t len, uint8_t *out);
#endif
