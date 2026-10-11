#include "editor/runtime.h"
#include "editor/editor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

uint32_t editor_runtime_em_px(double dpi, double points)
{
    if (!isfinite(dpi) || !isfinite(points) || dpi <= 0 || points <= 0) return 0;
    /* (P) round points * pixels/inch / 72 points/inch to nearest pixel.
     * (G) one positive whole pixel is the minimum accepted by the font sizing API. */
    double em = points * (dpi / 72.0);
    if (em >= EDITOR_FONT_MAX_PX) return EDITOR_FONT_MAX_PX;
    if (em < 1.0) return 1;
    return (uint32_t)floor(em + 0.5);
}
double editor_runtime_dpi(double xft, uint32_t width, uint32_t height,
                          uint32_t mm_width, uint32_t mm_height)
{
    if (isfinite(xft) && xft > 0) return xft;
    /* (P) diagonal DPI = diagonal pixels / (diagonal mm / 25.4 mm/inch).
     * Diagonals also preserve DPI for a rotated output. */
    if (width && height && mm_width && mm_height)
        return hypot((double)width, (double)height) * 25.4 /
               hypot((double)mm_width, (double)mm_height);
    return 96.0; /* (E) conventional Xft fallback when physical size is absent. */
}
double editor_runtime_xft_dpi(const char *resources, size_t len)
{
    if (!resources) return 0;
    double result = 0;
    size_t at = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && resources[end] != '\n') end++;
        size_t key = at;
        while (key < end && (resources[key] == ' ' || resources[key] == '\t')) key++;
        size_t colon = key;
        while (colon < end && resources[colon] != ':') colon++;
        size_t key_end = colon;
        while (key_end > key && (resources[key_end-1] == ' ' || resources[key_end-1] == '\t')) key_end--;
        const char *names[] = {"Xft.dpi", "Xft* dpi", "Xft*dpi", "Xft.Dpi"};
        bool matches = false;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (key_end - key == strlen(names[i]) && !memcmp(resources + key, names[i], key_end - key)) matches = true;
        if (matches && colon < end) {
            /* A double's text fits DBL_DECIMAL_DIG significant digits plus
             * sign/exponent; this also accepts padded xrdb values. */
            char value[sizeof(double) * 8];
            size_t start = colon + 1;
            while (start < end && (resources[start] == ' ' || resources[start] == '\t')) start++;
            size_t stop = end;
            while (stop > start && (resources[stop-1] == ' ' || resources[stop-1] == '\t' || resources[stop-1] == '\r')) stop--;
            if (stop - start < sizeof value) {
                memcpy(value, resources + start, stop - start); value[stop - start] = '\0';
                char *tail = NULL; double dpi = strtod(value, &tail);
                if (tail != value && !*tail && isfinite(dpi) && dpi > 0) result = dpi;
            }
        }
        at = end < len ? end + 1 : end;
    }
    return result;
}
void editor_runtime_window(uint32_t work_width, uint32_t work_height,
                            uint32_t *width, uint32_t *height)
{
    /* (E) bead's launch policy: 60% width and 70% height, floor to pixels. */
    if (width) *width = (uint32_t)((uint64_t)work_width * 60u / 100u);
    if (height) *height = (uint32_t)((uint64_t)work_height * 70u / 100u);
}
static xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 1, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE; free(r); return a;
}
static xcb_get_property_reply_t *property(xcb_connection_t *c, xcb_window_t root,
                                         const char *name, xcb_atom_t type)
{
    xcb_atom_t a = atom(c, name);
    if (!a) return NULL;
    /* Existing editor stage bound (G): cap setup reply size at its byte capacity. */
    return xcb_get_property_reply(c, xcb_get_property(c, 0, root, a, type, 0,
        (512u * 1024u) / sizeof(uint32_t)), NULL);
}
int editor_runtime_display_size(editor_display_size *size)
{
    if (!size) return EDITOR_ERR_ARG;
    int screen_number = 0;
    xcb_connection_t *c = xcb_connect(NULL, &screen_number);
    if (!c || xcb_connection_has_error(c)) { if (c) xcb_disconnect(c); return EDITOR_ERR_IO; }
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c));
    while (screen_number > 0 && it.rem) { xcb_screen_next(&it); screen_number--; }
    if (!it.rem) { xcb_disconnect(c); return EDITOR_ERR_IO; }
    xcb_screen_t *screen = it.data;
    uint32_t width = screen->width_in_pixels, height = screen->height_in_pixels;
    uint32_t mm_w = screen->width_in_millimeters, mm_h = screen->height_in_millimeters;
    int32_t x = 0, y = 0;
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_randr_id);
    if (ext && ext->present) {
        xcb_randr_get_output_primary_reply_t *primary = xcb_randr_get_output_primary_reply(c,
            xcb_randr_get_output_primary(c, screen->root), NULL);
        xcb_randr_get_screen_resources_current_reply_t *res = xcb_randr_get_screen_resources_current_reply(c,
            xcb_randr_get_screen_resources_current(c, screen->root), NULL);
        if (res) {
            xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(res);
            int count = xcb_randr_get_screen_resources_current_outputs_length(res);
            /* Prefer the primary, otherwise the first connected active output. */
            for (int pass = 0; pass < 2; pass++) {
                bool found = false;
                for (int i = 0; i < count && !found; i++) {
                    if (!pass && (!primary || outputs[i] != primary->output)) continue;
                    xcb_randr_get_output_info_reply_t *o = xcb_randr_get_output_info_reply(c,
                        xcb_randr_get_output_info(c, outputs[i], res->config_timestamp), NULL);
                    if (o && o->connection == XCB_RANDR_CONNECTION_CONNECTED && o->crtc) {
                        xcb_randr_get_crtc_info_reply_t *crtc = xcb_randr_get_crtc_info_reply(c,
                            xcb_randr_get_crtc_info(c, o->crtc, res->config_timestamp), NULL);
                        if (crtc && crtc->width && crtc->height) {
                            width = crtc->width; height = crtc->height; x = crtc->x; y = crtc->y;
                            /* Xvfb outputs omit mm; retain the root's -dpi size. */
                            if (o->mm_width && o->mm_height) { mm_w = o->mm_width; mm_h = o->mm_height; }
                            else if (width != screen->width_in_pixels || height != screen->height_in_pixels) mm_w = mm_h = 0;
                            found = true;
                        }
                        free(crtc);
                    }
                    free(o);
                }
                if (found) break;
            }
        }
        free(res); free(primary);
    }
    xcb_get_property_reply_t *r = property(c, screen->root, "RESOURCE_MANAGER", XCB_ATOM_STRING);
    double xft = r && r->type == XCB_ATOM_STRING && r->format == 8 ?
        editor_runtime_xft_dpi(xcb_get_property_value(r), (size_t)xcb_get_property_value_length(r)) : 0;
    free(r);
    size->dpi = editor_runtime_dpi(xft, width, height, mm_w, mm_h);
    size->work_width = width; size->work_height = height;
    uint32_t desktop = 0;
    r = property(c, screen->root, "_NET_CURRENT_DESKTOP", XCB_ATOM_CARDINAL);
    if (r && r->type == XCB_ATOM_CARDINAL && r->format == 32 && xcb_get_property_value_length(r) == (int)sizeof desktop)
        memcpy(&desktop, xcb_get_property_value(r), sizeof desktop);
    free(r);
    r = property(c, screen->root, "_NET_WORKAREA", XCB_ATOM_CARDINAL);
    /* EWMH rectangle consists of x, y, width, height CARDINALs. */
    size_t offset = (size_t)desktop * 4u;
    if (r && r->type == XCB_ATOM_CARDINAL && r->format == 32 &&
        offset + 4u <= (size_t)xcb_get_property_value_length(r) / sizeof(uint32_t)) {
        uint32_t area[4]; memcpy(area, (const uint32_t *)xcb_get_property_value(r) + offset, sizeof area);
        int64_t left = x > (int64_t)area[0] ? x : (int64_t)area[0];
        int64_t top = y > (int64_t)area[1] ? y : (int64_t)area[1];
        int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
        if (right > (int64_t)area[0] + area[2]) right = (int64_t)area[0] + area[2];
        if (bottom > (int64_t)area[1] + area[3]) bottom = (int64_t)area[1] + area[3];
        if (right > left && bottom > top) { size->work_width = (uint32_t)(right-left); size->work_height = (uint32_t)(bottom-top); }
    }
    free(r); xcb_disconnect(c); return EDITOR_OK;
}
