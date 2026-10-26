#include "x11/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "x11_identity_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
static xcb_atom_t intern(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE; free(r); return a;
}
static int property(plat *p, xcb_atom_t key, xcb_atom_t type, const char *want, size_t len)
{
    xcb_get_property_reply_t *r = xcb_get_property_reply(plat_native_x11(p).connection,
        xcb_get_property(plat_native_x11(p).connection, 0, plat_native_x11(p).window, key, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    T(r != NULL && r->type == type && r->format == 8);
    T(xcb_get_property_value_length(r) == (int)len);
    T(memcmp(xcb_get_property_value(r), want, len) == 0); free(r); return 0;
}
int main(void)
{
    /* The force-included display guard has already redirected a real display;
     * accept whichever private display it left (:99 under make check). */
    const char *display = getenv("DISPLAY");
    T(display != NULL && display[0] != '\0' && !edit_display_is_real(display));
    plat p; plat_config cfg = {NULL, 100, 100, false, NULL, 0};
    T(plat_init(&p, &cfg) == PLAT_OK);
    static const char cls[] = "sublimite\0sublimite";
    int failed = 0;
    failed |= property(&p, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, cls, sizeof cls);
    failed |= property(&p, intern(plat_native_x11(&p).connection, "_NET_WM_NAME"), intern(plat_native_x11(&p).connection, "UTF8_STRING"), "sublimité", strlen("sublimité"));
    failed |= property(&p, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, "sublimite", 9);
    plat_shutdown(&p); T(failed == 0);
    printf("x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on %s\n", display);
    return 0;
}
