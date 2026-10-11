#ifndef EDIT_EDITOR_RUNTIME_H
#define EDIT_EDITOR_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
/* (P) one typographic inch is 72 points. (E) default matches the bead's
 * Sublime reference, 10 pt. (P) DejaVu: units/em=2048, hhea=1901/-483,
 * ASCII advance=1233. At em=62, rounded cell=37*73; the 95-cell ASCII band
 * is 256595 pixels. At em=63 it is 38*73*95=263530 pixels, exceeding the
 * existing GL_FRAME_PIXEL_LIMIT=262144 (G, gl/renderer.inc). Thus 62 is the
 * largest whole em fitting that prepaid per-frame coverage bound. */
#define EDITOR_FONT_DEFAULT_PT 10.0
#define EDITOR_FONT_MAX_PX 62u
typedef struct editor_display_size {
    double dpi;
    uint32_t work_width, work_height;
} editor_display_size;
double editor_runtime_dpi(double xft, uint32_t width, uint32_t height,
                          uint32_t mm_width, uint32_t mm_height);
double editor_runtime_xft_dpi(const char *resources, size_t len);
uint32_t editor_runtime_em_px(double dpi, double points);
void editor_runtime_window(uint32_t work_width, uint32_t work_height,
                            uint32_t *width, uint32_t *height);
/* INIT only: X resources, active RANDR output physical size, EWMH work area.
 * Returns EDITOR_ERR_IO when the display cannot be opened. */
int editor_runtime_display_size(editor_display_size *size);
/* Pure selection: any non-NULL allow opts in, matching the test guard.
 * NULL display stays NULL when allowed. Caller applies the returned setting. */
const char *editor_runtime_display(const char *display, const char *safe, const char *allow);
/* Resolve without creating: absolute XDG_CONFIG_HOME or HOME/.config.
 * Setup only; returns EDITOR_OK / EDITOR_ERR_ARG. */
int editor_config_dir(char *out, size_t cap);
/* Setup only: create an absolute directory chain, synchronizing each parent.
 * NULL callback uses fsync; the callback seam must provide that barrier. */
int editor_make_directory(const char *path, int (*sync_parent)(void *, int), void *ctx);
#endif
