/* Internal XDND target; raw selection bytes remain in clip.c's bounded storage. */
#ifndef EDIT_X11_DND_H
#define EDIT_X11_DND_H
#include "plat.h"
#include <xcb/xcb.h>
int x11_dnd_init(plat *p);
void x11_dnd_destroy(plat *p);
void x11_dnd_event(plat *p, const xcb_client_message_event_t *event);
bool x11_dnd_poll(plat *p);
uint64_t x11_dnd_deadline(const plat *p);
void x11_dnd_complete(plat *p, bool ok);
const uint8_t *x11_plat_drop_data(const plat *p, size_t *len);
#endif
