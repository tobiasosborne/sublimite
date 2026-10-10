#ifndef EDIT_GL_H
#define EDIT_GL_H
#include "render/render.h"
/* Opt-in P2.4b experiment: EDIT_GL_UPLOAD=subdata|orphan|persistent.
 * Unset keeps the existing renderer and EDIT_GL_VBO behavior.
 * Init-only environment: EDIT_GL_VBO=orphan|persistent (default orphan),
 * EDIT_GL_SWAP_INTERVAL=0|1 (default 0). Init-only loader fault seams:
 * EDIT_GL_EGL_LIBRARY / EDIT_GL_GL_LIBRARY override the default sonames.
 * No GL/EGL libraries are linked. */
#define GL_POLL_MESSAGE UINT32_C(0x474c504c)
int render_gl_backend(render_backend *b);
/* UI: route plat's PresentCompleteNotify callback; ignores stale swaps.
 * Uses observation CLOCK_MONOTONIC, never treats raw X UST as that clock. */
int gl_present_complete(render_backend *b, uint32_t serial, uint64_t ust, uint64_t msc);
/* UI: after draining the shared work mailbox, check for a latched device
 * failure (Skip, reset, failed fence, missing completion). No further submits
 * or presents succeed after failure; shut down and choose a fresh backend.
 * Pending frames schedule bounded GL_POLL_MESSAGE work automatically; their
 * per-job receiver routes them to event(), including with a shared dispatcher.
 * Init requires a live work pool with a raster or dedicated foreground lane;
 * completion never queues behind blocking bulk work. Capacity admission bounds each frame to
 * 4096 glyphs, 64 pages, 16 MiB atlas storage and 262144 referenced pixels. */
int gl_completion_status(const render_backend *b);
const char *gl_buffer_mode(const render_backend *b);
const char *gl_device_name(const render_backend *b);
uint64_t gl_displayed_msc(const render_backend *b);
/* Test-only diagnostic: retained framebuffer, top-left RGBA8, caller storage.
 * glReadPixels is synchronous; never use on a production typing path. */
int gl_read_pixels(render_backend *b, uint8_t *rgba, size_t bytes);
/* Experimental mapped-cell lease (UI, allocation/native-call free). Acquire
 * only when idle; BUSY leaves grid/output unchanged. preserve copies retained
 * cells between mapped slots for partial layout. Full layout uses false.
 * Fill grid.cells directly, then gl_cells_submit: on success cells becomes
 * NULL and all saved aliases are read-only until that slot is reacquired.
 * Ordinary render_backend_submit with caller-owned cells still copies them.
 * Do not use render_backend_submit directly with a writable leased grid. */
int gl_cells_acquire(render_backend *b, render_grid *g, bool preserve);
int gl_cells_submit(render_backend *b, render_grid *g,
                    const render_strip *strips, size_t count);
#endif
