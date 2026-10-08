/* clip.h - X selections (P2.2): own and request PRIMARY / CLIPBOARD as UTF8_STRING. Internal to x11. */
#ifndef EDITOR_X11_CLIP_H
#define EDITOR_X11_CLIP_H
#include "plat.h"
#include <xcb/xcb.h>
bool x11_push_event(plat *p, const plat_event *ev);   /* x11.c: append to the input queue */
int  x11_clip_init(plat *p);
void x11_clip_destroy(plat *p);
/* Handles SELECTION_REQUEST/CLEAR/NOTIFY; returns true and fills ev if a plat event should be queued. */
bool x11_clip_event(plat *p, const xcb_generic_event_t *e, plat_event *ev);
/* Pure helpers, exposed for tests. latin1: UTF-8 -> ISO-8859-1 ('?' for unrepresentable). Returns bytes written (<= len). */
size_t x11_utf8_to_latin1(const uint8_t *s, size_t len, uint8_t *out);
/* latin1 -> UTF-8; out needs 2*len bytes. */
size_t x11_latin1_to_utf8(const uint8_t *s, size_t len, uint8_t *out);
#endif
