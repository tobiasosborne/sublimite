#ifndef EDIT_GL_H
#define EDIT_GL_H
#include "render/render.h"
/* EDIT_GL_VBO=persistent|orphan (default orphan). Persistent requests fall
 * back to orphan when ARB_buffer_storage is absent; query reports the choice. */
int render_gl_backend(render_backend *b);
/* Native callback routed through render_backend_event(WORK), using the active
 * original frame ID. Poll with a work message carrying GL_POLL_MSG_KIND. */
#define GL_PRESENT_MSG_KIND UINT32_C(0x474c5052)
#define GL_POLL_MSG_KIND UINT32_C(0x474c504f)
typedef struct gl_present_message { uint32_t serial, reserved; uint64_t ust, msc; } gl_present_message;
typedef struct gl_backend_status {
    bool persistent_requested, persistent_active, present_verified;
    uint64_t displayed_msc;
    uint32_t swap_serial;
} gl_backend_status;
int gl_backend_query(const render_backend *b, gl_backend_status *out);
/* UI: bounded, zero-timeout fence poll. Route the platform callback below. */
int gl_backend_poll(render_backend *b);
int gl_backend_present_complete(render_backend *b, uint32_t serial,
                                uint64_t ust, uint64_t msc);
/* Diagnostic readback of retained RGBA8 surface, bottom row first. */
int gl_backend_read_pixels(render_backend *b, uint8_t *rgba, size_t bytes);
#endif
