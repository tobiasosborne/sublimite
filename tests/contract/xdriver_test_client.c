/* Independent XCB client used to observe xdriver and exercise selection peers. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct {
    xcb_connection_t *conn;
    xcb_window_t window;
    xcb_atom_t selection, utf8, incr, property, marker;
} client;

static void require(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "xdriver fixture: %s\n", message);
        exit(1);
    }
}

static void checked(client *c, xcb_void_cookie_t cookie)
{
    xcb_generic_error_t *err = xcb_request_check(c->conn, cookie);
    bool ok = !err;
    free(err);
    require(ok, "checked X request failed");
}

static xcb_atom_t atom(client *c, const char *name)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(c->conn,
        xcb_intern_atom(c->conn, 0, (uint16_t)strlen(name), name), NULL);
    require(reply && reply->atom, "InternAtom failed");
    xcb_atom_t result = reply->atom;
    free(reply);
    return result;
}

static xcb_generic_event_t *event(client *c)
{
    xcb_generic_event_t *e = xcb_wait_for_event(c->conn);
    require(e && e->response_type, "event stream failed");
    return e;
}

static unsigned keycode(client *c, xcb_keysym_t sym)
{
    const xcb_setup_t *setup = xcb_get_setup(c->conn);
    uint8_t min = setup->min_keycode, max = setup->max_keycode;
    xcb_get_keyboard_mapping_reply_t *map = xcb_get_keyboard_mapping_reply(c->conn,
        xcb_get_keyboard_mapping(c->conn, min, (uint8_t)(max - min + 1)), NULL);
    require(map && map->keysyms_per_keycode, "GetKeyboardMapping failed");
    unsigned found = 0;
    xcb_keysym_t *syms = xcb_get_keyboard_mapping_keysyms(map);
    for (unsigned code = min; code <= max; ++code) {
        if (syms[(code - min) * map->keysyms_per_keycode] == sym) {
            found = code;
            break;
        }
    }
    free(map);
    require(found != 0, "required keysym missing");
    return found;
}

static void observe_window(client *c)
{
    printf("READY %" PRIu32 " %u %u %u %u %u\n", c->window, keycode(c, XKB_KEY_a),
           keycode(c, XKB_KEY_Return), keycode(c, XKB_KEY_Shift_L), keycode(c, XKB_KEY_Control_L),
           keycode(c, XKB_KEY_1));
    for (;;) {
        xcb_generic_event_t *e = event(c);
        uint8_t type = e->response_type & 0x7fu;
        switch (type) {
        case XCB_KEY_PRESS:
        case XCB_KEY_RELEASE: {
            const xcb_key_press_event_t *key = (const void *)e;
            printf("%s %u %u\n", type == XCB_KEY_PRESS ? "down" : "up", key->detail, key->state);
            break;
        }
        case XCB_MOTION_NOTIFY: {
            const xcb_motion_notify_event_t *motion = (const void *)e;
            printf("motion %d %d\n", motion->event_x, motion->event_y);
            break;
        }
        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE: {
            const xcb_button_press_event_t *button = (const void *)e;
            printf("%s %u\n", type == XCB_BUTTON_PRESS ? "button-down" : "button-up", button->detail);
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            const xcb_client_message_event_t *msg = (const void *)e;
            require(msg->window == c->window && msg->type == c->marker && msg->format == 32 &&
                    (e->response_type & 0x80u), "wrong marker envelope");
            printf("marker");
            for (size_t i = 0; i < sizeof msg->data.data32 / sizeof msg->data.data32[0]; ++i)
                printf(" %" PRIu32, msg->data.data32[i]);
            putchar('\n');
            break;
        }
        }
        free(e);
    }
}

static void serve(client *c, bool partial)
{
    static const uint8_t bytes[] = {0, 255, '\n', ' ', 0xc3, 0xa9, 0};
    checked(c, xcb_set_selection_owner_checked(c->conn, c->window, c->selection, XCB_CURRENT_TIME));
    printf("READY %" PRIu32 "\n", c->window);
    xcb_window_t requestor = XCB_NONE;
    xcb_atom_t property = XCB_NONE;
    for (;;) {
        xcb_generic_event_t *e = event(c);
        switch (e->response_type & 0x7fu) {
        case XCB_SELECTION_REQUEST: {
            const xcb_selection_request_event_t *req = (const void *)e;
            require(req->target == c->utf8 && req->selection == c->selection, "unexpected conversion");
            requestor = req->requestor;
            property = req->property;
            if (partial) {
                uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
                checked(c, xcb_change_window_attributes_checked(c->conn, requestor, XCB_CW_EVENT_MASK, &mask));
                uint32_t size = sizeof bytes;
                checked(c, xcb_change_property_checked(c->conn, XCB_PROP_MODE_REPLACE, requestor,
                    property, c->incr, 32, 1, &size));
            } else {
                checked(c, xcb_change_property_checked(c->conn, XCB_PROP_MODE_REPLACE, requestor,
                    property, c->utf8, 8, sizeof bytes, bytes));
            }
            xcb_selection_notify_event_t notify = {
                .response_type = XCB_SELECTION_NOTIFY, .time = req->time, .requestor = requestor,
                .selection = req->selection, .target = req->target, .property = property,
            };
            char wire[32] = {0};
            memcpy(wire, &notify, sizeof notify);
            checked(c, xcb_send_event_checked(c->conn, 0, requestor, 0, wire));
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            const xcb_property_notify_event_t *p = (const void *)e;
            if (partial && p->window == requestor && p->atom == property && p->state == XCB_PROPERTY_DELETE) {
                checked(c, xcb_change_property_checked(c->conn, XCB_PROP_MODE_REPLACE, requestor,
                    property, c->utf8, 8, sizeof bytes, bytes));
                partial = false;
                puts("sent-partial"); /* Full lower bound, but no zero-length terminator. */
            }
            break;
        }
        }
        free(e);
    }
}

static void read_selection(client *c, size_t expected)
{
    checked(c, xcb_convert_selection_checked(c->conn, c->window, c->selection, c->utf8,
                                               c->property, XCB_CURRENT_TIME));
    size_t total = 0, chunks = 0;
    bool incr = false;
    for (;;) {
        xcb_generic_event_t *e = event(c);
        bool take = false;
        switch (e->response_type & 0x7fu) {
        case XCB_SELECTION_NOTIFY: {
            const xcb_selection_notify_event_t *n = (const void *)e;
            require(n->requestor == c->window && n->selection == c->selection &&
                    n->target == c->utf8 && n->property == c->property, "conversion refused");
            take = true;
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            const xcb_property_notify_event_t *p = (const void *)e;
            take = incr && p->window == c->window && p->atom == c->property && p->state == XCB_PROPERTY_NEW_VALUE;
            break;
        }
        }
        free(e);
        if (!take) continue;
        xcb_get_property_reply_t *r = xcb_get_property_reply(c->conn, xcb_get_property(c->conn, 1,
            c->window, c->property, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT32_MAX / 4), NULL);
        require(r && !r->bytes_after, "selection property missing or truncated");
        if (!incr && r->type == c->incr) {
            require(r->format == 32 && r->value_len == 1 &&
                    *(const uint32_t *)xcb_get_property_value(r) == expected, "wrong INCR header");
            incr = true;
            free(r);
            continue;
        }
        require(r->type == c->utf8 && r->format == 8, "wrong selection encoding");
        const uint8_t *data = xcb_get_property_value(r);
        size_t n = r->value_len;
        require(n <= expected - total, "selection longer than expected");
        for (size_t i = 0; i < n; ++i)
            require(data[i] == (uint8_t)(total + i), "selection byte differs");
        free(r);
        total += n;
        if (!incr || !n) break;
        ++chunks;
    }
    require(total == expected, "selection shorter than expected");
    printf("READY %zu %zu\n", total, chunks);
}

int main(int argc, char **argv)
{
    require(argc >= 3, "expected display and mode");
    require(setvbuf(stdout, NULL, _IOLBF, 0) == 0, "setvbuf failed");
    if (!strcmp(argv[1], "--display")) {
        /* Stand in for the driver's pipe only. Report the core protocol limit. */
        const char *reply = getenv("XDRIVER_TEST_REPLY");
        require(reply != NULL, "missing protocol fixture reply");
        printf("READY %u\n", UINT16_MAX * 4u);
        char line[BUFSIZ];
        require(fgets(line, sizeof line, stdin) != NULL, "missing protocol fixture request");
        size_t size = strlen(reply);
        require(write(STDOUT_FILENO, reply, size) == (ssize_t)size, "protocol fixture write failed");
        return 0;
    }
    client c = {.conn = xcb_connect(argv[1], NULL)};
    require(c.conn && !xcb_connection_has_error(c.conn), "cannot connect");
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c.conn)).data;
    c.window = xcb_generate_id(c.conn);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE;
    bool window = !strcmp(argv[2], "window");
    checked(&c, xcb_create_window_checked(c.conn, XCB_COPY_FROM_PARENT, c.window, screen->root,
        10, 20, 180, 160, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, &mask));
    if (window) {
        c.marker = atom(&c, "_SUBLIMITE_CONTRACT");
        uint32_t pid = (uint32_t)getpid();
        checked(&c, xcb_change_property_checked(c.conn, XCB_PROP_MODE_REPLACE, c.window,
            atom(&c, "_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1, &pid));
        checked(&c, xcb_map_window_checked(c.conn, c.window));
        checked(&c, xcb_set_input_focus_checked(c.conn, XCB_INPUT_FOCUS_PARENT, c.window, XCB_CURRENT_TIME));
        observe_window(&c);
    }
    require(argc == 5, "expected a selection and mode argument");
    c.selection = atom(&c, argv[3]);
    c.utf8 = atom(&c, "UTF8_STRING");
    c.incr = atom(&c, "INCR");
    c.property = atom(&c, "_XDRIVER_TEST_DATA");
    if (!strcmp(argv[2], "owner")) serve(&c, !strcmp(argv[4], "partial"));
    require(!strcmp(argv[2], "read"), "wrong mode");
    read_selection(&c, strtoul(argv[4], NULL, 10));
    xcb_disconnect(c.conn);
    return 0;
}
