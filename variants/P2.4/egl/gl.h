#ifndef EDIT_GL_H
#define EDIT_GL_H
#include "render/render.h"
/* Init-only environment: EDIT_GL_VBO=orphan|persistent (default orphan),
 * EDIT_GL_SWAP_INTERVAL=0|1 (default 0). Init-only loader fault seams:
 * EDIT_GL_EGL_LIBRARY / EDIT_GL_GL_LIBRARY override the default sonames.
 * No GL/EGL libraries are linked. */
#define GL_POLL_MESSAGE UINT32_C(0x474c504c)
int render_gl_backend(render_backend *b);
/* UI: route plat's PresentCompleteNotify callback; ignores stale swaps.
 * Uses observation CLOCK_MONOTONIC, never treats raw X UST as that clock. */
int gl_present_complete(render_backend *b, uint32_t serial, uint64_t ust, uint64_t msc);
const char *gl_buffer_mode(const render_backend *b);
const char *gl_device_name(const render_backend *b);
uint64_t gl_displayed_msc(const render_backend *b);
/* Test-only diagnostic: retained framebuffer, top-left RGBA8, caller storage.
 * glReadPixels is synchronous; never use on a production typing path. */
int gl_read_pixels(render_backend *b, uint8_t *rgba, size_t bytes);
#endif
