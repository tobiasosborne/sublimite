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
    xcb_get_property_reply_t *r = xcb_get_property_reply(p->conn,
        xcb_get_property(p->conn, 0, p->win, key, XCB_GET_PROPERTY_TYPE_ANY, 0, 64), NULL);
    T(r != NULL && r->type == type && r->format == 8);
    T(xcb_get_property_value_length(r) == (int)len);
    T(memcmp(xcb_get_property_value(r), want, len) == 0); free(r); return 0;
}
int main(void)
{
    T(getenv("DISPLAY") != NULL && strcmp(getenv("DISPLAY"), ":99") == 0);
    plat p; plat_config cfg = {NULL, 100, 100, false, -1, 0};
    T(plat_init(&p, &cfg) == PLAT_OK);
    static const char cls[] = "sublimite\0sublimite";
    int failed = 0;
    failed |= property(&p, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, cls, sizeof cls);
    failed |= property(&p, intern(p.conn, "_NET_WM_NAME"), intern(p.conn, "UTF8_STRING"), "sublimité", strlen("sublimité"));
    failed |= property(&p, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, "sublimite", 9);
    plat_shutdown(&p); T(failed == 0);
    puts("x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99");
    return 0;
}
