#if !defined(_WIN32)
#include "x11/plat.h"
#include "wayland/plat.h"
#include "editor/runtime.h"
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

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
static int x11_display_size(plat_display_info *size)
{
    if (!size) return PLAT_ERR_FAIL;
    int screen_number = 0;
    xcb_connection_t *c = xcb_connect(NULL, &screen_number);
    if (!c || xcb_connection_has_error(c)) { if (c) xcb_disconnect(c); return PLAT_ERR_NO_DISPLAY; }
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c));
    while (screen_number > 0 && it.rem) { xcb_screen_next(&it); screen_number--; }
    if (!it.rem) { xcb_disconnect(c); return PLAT_ERR_NO_DISPLAY; }
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
    free(r); xcb_disconnect(c); return PLAT_OK;
}

int plat_display_size(plat_backend backend, plat_display_info *info)
{
    if (!info) return PLAT_ERR_FAIL;
    if (backend == PLAT_BACKEND_X11) return x11_display_size(info);
    if (backend != PLAT_BACKEND_WAYLAND) return PLAT_ERR_FAIL;
    wayland_display_info output;
    int rc = wayland_display_size(&output);
    if (!rc) *info = (plat_display_info){output.dpi, output.width, output.height};
    return rc;
}

/* Startup only. RandR mode timing provides (P) total pixel clocks per frame
 * divided by pixel clocks/second; ns/second is the SI unit conversion.
 * Unknown/zero clocks remain unknown, never a fabricated fallback rate. */
uint64_t plat_display_interval_ns(const plat *p)
{
    if (!p || !p->conn || p->backend!=PLAT_BACKEND_X11) return 0;
    xcb_connection_t *connection=p->conn;
    xcb_screen_iterator_t screens=xcb_setup_roots_iterator(xcb_get_setup(connection));
    if (!screens.rem) return 0;
    xcb_randr_get_screen_resources_current_reply_t *resources=xcb_randr_get_screen_resources_current_reply(connection,
        xcb_randr_get_screen_resources_current(connection,screens.data->root),NULL);
    if (!resources) return 0;
    xcb_randr_crtc_t *crtcs=xcb_randr_get_screen_resources_current_crtcs(resources);
    xcb_randr_mode_info_t *modes=xcb_randr_get_screen_resources_current_modes(resources);
    int crtc_count=xcb_randr_get_screen_resources_current_crtcs_length(resources);
    int mode_count=xcb_randr_get_screen_resources_current_modes_length(resources);
    uint64_t interval=0;
    for (int i=0;i<crtc_count && !interval;i++) {
        xcb_randr_get_crtc_info_reply_t *info=xcb_randr_get_crtc_info_reply(connection,
            xcb_randr_get_crtc_info(connection,crtcs[i],resources->config_timestamp),NULL);
        if (info) for (int j=0;j<mode_count;j++) if (modes[j].id==info->mode && modes[j].dot_clock) {
            uint64_t clocks=(uint64_t)modes[j].htotal*modes[j].vtotal;
            interval=clocks*UINT64_C(1000000000)/modes[j].dot_clock;
            if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) interval+=interval;
            if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) interval/=2u; /* two fields/frame */
        }
        free(info);
    }
    free(resources); return interval;
}
#endif
