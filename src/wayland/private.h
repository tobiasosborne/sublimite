#ifndef EDIT_WAYLAND_PRIVATE_H
#define EDIT_WAYLAND_PRIVATE_H
#include "wayland/plat.h"
int x11_plat_init(plat *p, const plat_config *cfg);
void x11_plat_map(plat *p);
void x11_plat_set_blink(plat *p, uint32_t ms);
int x11_plat_set_present_events(plat *p, bool enabled);
void x11_plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz);
bool x11_plat_poll_event(plat *p, plat_event *out);
int x11_plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms);
int x11_plat_run(plat *p, const plat_callbacks *cb);
void x11_plat_shutdown(plat *p);
int x11_plat_clip_set(plat *p, int which, const void *utf8, size_t len);
int x11_plat_clip_request(plat *p, int which);
const uint8_t * x11_plat_clip_data(const plat *p, size_t *len);
/* INIT-only connection; supports long runtime directories through a dirfd. */
int wayland_display_size_on(void *display, wayland_display_info *info);
int wayland_connect(void **display);
bool wayland_present_available(const plat *p);
int wayland_plat_init(plat *p, const plat_config *cfg);
void wayland_plat_map(plat *p);
void wayland_plat_set_blink(plat *p, uint32_t ms);
int wayland_plat_set_present_events(plat *p, bool enabled);
void wayland_plat_set_repeat(plat *p, uint32_t delay_ms, uint32_t rate_hz);
bool wayland_plat_poll_event(plat *p, plat_event *out);
int wayland_plat_run_for(plat *p, const plat_callbacks *cb, int timeout_ms);
int wayland_plat_run(plat *p, const plat_callbacks *cb);
void wayland_plat_shutdown(plat *p);
int wayland_plat_clip_set(plat *p, int which, const void *utf8, size_t len);
int wayland_plat_clip_request(plat *p, int which);
const uint8_t * wayland_plat_clip_data(const plat *p, size_t *len);
/* Decoder seam shared by native keyboard/repeat and allocation regression. */
bool wayland_input_key(plat *p, uint32_t key, uint32_t time, bool press, bool repeat);
#endif
