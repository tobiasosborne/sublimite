#ifndef EDIT_XCURSOR_H
#define EDIT_XCURSOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xcb/xcb.h>
/* Xcursor image view. Pixels are borrowed, little-endian premultiplied ARGB32.
 * Decode is allocation-free; image/TOC/header extents are validated first. */
typedef struct xcursor_image {
    uint32_t nominal, width, height, xhot, yhot;
    const uint8_t *pixels;
} xcursor_image;
bool xcursor_decode(const uint8_t *, size_t, uint32_t wanted, xcursor_image *);
/* INIT only. On success storage owns the file bytes; free it after upload.
 * Search the standard user/system roots, following Inherits without cycles. */
bool xcursor_load(const char *home_dir, const char *theme, const char *name,
                  uint32_t wanted, xcursor_image *, uint8_t **storage);
void xcursor_settings(const char *resources, size_t length, const char *theme_env,
                      const char *size_env, char *theme, size_t capacity, uint32_t *size);
xcb_cursor_t xcursor_upload(xcb_connection_t *, xcb_window_t root, const xcursor_image *);
#endif
