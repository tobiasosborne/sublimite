#include "wayland/private.h"
#include <stdlib.h>
#include <string.h>
#ifdef EDIT_HAVE_WAYLAND
#include <wayland-client.h>
#include <limits.h>

typedef struct output_mode {
    struct output_mode *next;
    struct wl_output *proxy;
    uint32_t width, height, scale;
    int32_t transform;
} output_mode;
typedef struct output_query { output_mode *outputs; bool failed; } output_query;
static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
    int32_t mm_w, int32_t mm_h, int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    (void)output; (void)x; (void)y; (void)mm_w; (void)mm_h; (void)subpixel; (void)make; (void)model;
    ((output_mode *)data)->transform = transform;
}
static void output_mode_event(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    (void)output; (void)refresh; output_mode *mode = data;
    if ((flags & WL_OUTPUT_MODE_CURRENT) && width > 0 && height > 0) {
        mode->width = (uint32_t)width; mode->height = (uint32_t)height;
    }
}
static void output_done(void *data, struct wl_output *output) { (void)data; (void)output; }
static void output_scale(void *data, struct wl_output *output, int32_t scale)
{ (void)output; if (scale > 0) ((output_mode *)data)->scale = (uint32_t)scale; }
static const struct wl_output_listener output_listener = {.geometry = output_geometry,
    .mode = output_mode_event, .done = output_done, .scale = output_scale};
static void output_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    output_query *query = data;
    if (strcmp(interface, "wl_output")) return;
    output_mode *mode = calloc(1, sizeof *mode); /* INIT only; exactly one node per advertised output */
    if (!mode) { query->failed = true; return; }
    mode->scale = 1; /* (P) protocol default: unity before any scale event */
    mode->next = query->outputs; query->outputs = mode;
    mode->proxy = wl_registry_bind(registry, name, &wl_output_interface,
        version < WL_OUTPUT_SCALE_SINCE_VERSION ? version : WL_OUTPUT_SCALE_SINCE_VERSION);
    if (!mode->proxy || wl_output_add_listener(mode->proxy, &output_listener, mode) < 0) query->failed = true;
}
static void output_remove(void *data, struct wl_registry *registry, uint32_t name)
{ (void)data; (void)registry; (void)name; /* init snapshot; runtime output moves are separate */ }
static const struct wl_registry_listener output_registry = {.global = output_global, .global_remove = output_remove};
int wayland_display_size_on(void *display, wayland_display_info *info)
{
    if (!display || !info) return PLAT_ERR_FAIL;
    /* (E) Conventional desktop fallback = 96 logical px/inch, matching Xft
     * and editor_runtime_dpi. Effective buffer DPI multiplies by wl_output.scale. */
    wayland_display_info result = {.dpi = 96.0, .scale = 1};
    output_query query = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) return PLAT_ERR_FAIL;
    int rc = PLAT_ERR_FAIL;
    if (wl_registry_add_listener(registry, &output_registry, &query) < 0) goto done;
    /* (P) Registry announcement, then bound-output events: each requires its
     * own roundtrip, since bind requests arise during the first dispatch. */
    if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0 || query.failed) goto done;
    for (output_mode *mode = query.outputs; mode; mode = mode->next) {
        if (!mode->width || !mode->height || mode->scale > INT32_MAX) continue;
        if (result.width && mode->scale < result.scale) continue;
        result = (wayland_display_info){96.0 * mode->scale, mode->width, mode->height, mode->scale};
        if (mode->transform == WL_OUTPUT_TRANSFORM_90 || mode->transform == WL_OUTPUT_TRANSFORM_270 ||
            mode->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || mode->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
            result.width = mode->height; result.height = mode->width;
        }
    }
    *info = result; rc = PLAT_OK;
done:
    while (query.outputs) {
        output_mode *mode = query.outputs; query.outputs = mode->next;
        if (mode->proxy) wl_output_destroy(mode->proxy);
        free(mode);
    }
    wl_registry_destroy(registry);
    return rc;
}
int wayland_display_size(wayland_display_info *info)
{
    if (!info) return PLAT_ERR_FAIL;
    void *display = NULL; int rc = wayland_connect(&display);
    if (rc) return rc;
    rc = wayland_display_size_on(display, info);
    wl_display_disconnect(display);
    return rc;
}
#else
int wayland_display_size_on(void *display, wayland_display_info *info)
{ (void)display; (void)info; return PLAT_ERR_NO_DISPLAY; }
int wayland_display_size(wayland_display_info *info)
{ (void)info; return PLAT_ERR_NO_DISPLAY; }
#endif
