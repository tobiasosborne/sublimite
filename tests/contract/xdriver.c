/* Persistent X11 input and selection peer for the behavioral contract suite. */
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-x11.h>

typedef const char *error;
typedef xcb_void_cookie_t (*fake_input)(xcb_connection_t *, uint8_t, uint8_t,
    uint32_t, xcb_window_t, int16_t, int16_t, uint8_t);

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} buffer;

typedef struct {
    xcb_atom_t atom;
    xcb_timestamp_t time;
    buffer bytes;
} selection;

typedef struct transfer {
    struct transfer *next;
    xcb_window_t window;
    xcb_atom_t property;
    buffer bytes;
    size_t offset;
} transfer;

typedef struct {
    xcb_connection_t *conn;
    void *xtest;
    fake_input fake;
    struct xkb_context *xkb;
    int32_t keyboard;
    xcb_screen_t *screen;
    xcb_window_t window, read_window, target;
    xcb_atom_t pid, marker, utf8, targets, timestamp, incr, property, clock;
    size_t max_request, chunk;
    bool focused;
    bool buttons[UINT8_MAX + 1u];
    selection selections[2]; /* The two public selections, PRIMARY and CLIPBOARD. */
    transfer *transfers;
} driver;

static bool append(buffer *b, const void *data, size_t size)
{
    if (!size) return true;
    if (size > SIZE_MAX - b->size) return false;
    size_t end = b->size + size;
    if (end > b->capacity) {
        size_t capacity = b->capacity <= SIZE_MAX / 2 ? b->capacity * 2 : SIZE_MAX;
        if (capacity < end) capacity = end;
        uint8_t *p = realloc(b->data, capacity);
        if (!p) return false;
        b->data = p;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, data, size);
    b->size = end;
    return true;
}

static error request(driver *d, xcb_void_cookie_t cookie, const char *what)
{
    xcb_generic_error_t *e = xcb_request_check(d->conn, cookie);
    if (!e) return xcb_connection_has_error(d->conn) ? "X_CONNECTION" : NULL;
    bool gone = e->error_code == XCB_WINDOW;
    if (!gone) fprintf(stderr, "xdriver: %s: X error %u\n", what, e->error_code);
    free(e);
    return gone ? "BAD_WINDOW" : "X_REQUEST";
}

static bool bad_window(error err)
{
    return err && !strcmp(err, "BAD_WINDOW");
}

static error reply_error(driver *d, xcb_generic_error_t *e, const char *operation)
{
    error err = "X_REPLY";
    if (xcb_connection_has_error(d->conn)) err = "X_CONNECTION";
    else if (e && e->error_code == XCB_WINDOW) err = "BAD_WINDOW";
    else fprintf(stderr, "xdriver: %s: X error %u\n", operation, e ? e->error_code : 0u);
    free(e);
    return err;
}

static error watch(driver *d, xcb_window_t window, uint32_t mask)
{
    xcb_generic_error_t *e = NULL;
    xcb_get_window_attributes_reply_t *r = xcb_get_window_attributes_reply(d->conn,
        xcb_get_window_attributes(d->conn, window), &e);
    if (!r) return reply_error(d, e, "GetWindowAttributes for subscription");
    uint32_t previous = r->your_event_mask;
    free(r);
    if ((previous & mask) == mask) return NULL;
    mask |= previous;
    return request(d, xcb_change_window_attributes_checked(d->conn, window,
        XCB_CW_EVENT_MASK, &mask), "Select window events");
}

static int64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) {
        perror("xdriver: clock_gettime");
        exit(1);
    }
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Transfer and window waits share the same queue and absolute deadline. */
static error next_event(driver *d, int64_t deadline, xcb_generic_event_t **out)
{
    struct pollfd p = {xcb_get_file_descriptor(d->conn), POLLIN, 0};
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) return "TIMEOUT";
        *out = xcb_poll_for_event(d->conn);
        if (*out) return NULL;
        if (xcb_connection_has_error(d->conn)) return "X_CONNECTION";
        int n = poll(&p, 1, left > INT_MAX ? INT_MAX : (int)left);
        if (n < 0 && errno != EINTR) return "POLL";
    }
}

static xcb_atom_t atom(driver *d, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(d->conn,
        xcb_intern_atom(d->conn, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t value = r ? r->atom : XCB_NONE;
    free(r);
    return value;
}

static error new_window(driver *d, xcb_window_t *window)
{
    xcb_window_t id = xcb_generate_id(d->conn);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    error err = request(d, xcb_create_window_checked(d->conn, XCB_COPY_FROM_PARENT, id,
        d->screen->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, d->screen->root_visual,
        XCB_CW_EVENT_MASK, &mask), "CreateWindow");
    if (!err) *window = id;
    return err;
}

static error connect_display(driver *d, const char *display)
{
    d->conn = xcb_connect(display, NULL);
    if (xcb_connection_has_error(d->conn)) return "X_CONNECT";
    d->screen = xcb_setup_roots_iterator(xcb_get_setup(d->conn)).data;
    if (!d->screen) return "X_SCREEN";
    xcb_query_extension_reply_t *xtest = xcb_query_extension_reply(d->conn,
        xcb_query_extension(d->conn, 5, "XTEST"), NULL);
    bool has_xtest = xtest && xtest->present;
    free(xtest);
    if (!has_xtest) return "NO_XTEST";
    d->xtest = dlopen("libxcb-xtest.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!d->xtest) return "NO_XTEST_LIBRARY";
    void *symbol = dlsym(d->xtest, "xcb_test_fake_input_checked");
    _Static_assert(sizeof d->fake == sizeof symbol, "POSIX function pointer representation");
    memcpy(&d->fake, &symbol, sizeof symbol);
    if (!d->fake) return "NO_XTEST_SYMBOL";
    if (!xkb_x11_setup_xkb_extension(d->conn, XKB_X11_MIN_MAJOR_XKB_VERSION,
            XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
            NULL, NULL, NULL, NULL)) return "NO_XKB";
    d->keyboard = xkb_x11_get_core_keyboard_device_id(d->conn);
    d->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (d->keyboard < 0 || !d->xkb) return "NO_KEYBOARD";

    d->pid = atom(d, "_NET_WM_PID");
    d->marker = atom(d, "_SUBLIMITE_CONTRACT");
    d->utf8 = atom(d, "UTF8_STRING");
    d->targets = atom(d, "TARGETS");
    d->timestamp = atom(d, "TIMESTAMP");
    d->incr = atom(d, "INCR");
    d->property = atom(d, "_SUBLIMITE_XDRIVER_SELECTION");
    d->clock = atom(d, "_SUBLIMITE_XDRIVER_CLOCK");
    d->selections[0].atom = XCB_ATOM_PRIMARY;
    d->selections[1].atom = atom(d, "CLIPBOARD");
    if (!(d->pid && d->marker && d->utf8 && d->targets && d->timestamp && d->incr &&
          d->property && d->clock && d->selections[1].atom)) return "X_ATOM";

    d->max_request = (size_t)xcb_get_maximum_request_length(d->conn) * 4;
    /* BIG-REQUESTS can add one 32-bit length word to the normal request. */
    size_t header = sizeof(xcb_change_property_request_t) + sizeof(uint32_t);
    if (d->max_request <= header) return "X_REQUEST_LIMIT";
    d->chunk = (d->max_request - header) & ~(size_t)3;
    if (d->chunk > (size_t)(INT_MAX & ~3)) d->chunk = INT_MAX & ~3;
    uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    error err = request(d, xcb_change_window_attributes_checked(d->conn, d->screen->root,
        XCB_CW_EVENT_MASK, &mask), "Select root events");
    return err ? err : new_window(d, &d->window);
}

static selection *get_selection(driver *d, xcb_atom_t atom_id)
{
    for (size_t i = 0; i < sizeof d->selections / sizeof d->selections[0]; ++i)
        if (d->selections[i].atom == atom_id) return &d->selections[i];
    return NULL;
}

static error owner(driver *d, xcb_atom_t selection_atom, xcb_window_t *result)
{
    xcb_get_selection_owner_reply_t *r = xcb_get_selection_owner_reply(d->conn,
        xcb_get_selection_owner(d->conn, selection_atom), NULL);
    if (!r) return "X_SELECTION_OWNER";
    *result = r->owner;
    free(r);
    return NULL;
}

static error expect_owner(driver *d, xcb_atom_t selection_atom, xcb_window_t expected)
{
    xcb_window_t actual;
    error err = owner(d, selection_atom, &actual);
    if (err) return err;
    return actual != expected ? "STALE_OWNER" : NULL;
}

static void remove_transfer(transfer **slot)
{
    transfer *done = *slot;
    *slot = done->next;
    free(done->bytes.data);
    free(done);
}

static error send_selection(driver *d, const xcb_selection_request_event_t *req)
{
    selection *s = get_selection(d, req->selection);
    xcb_atom_t property = req->property ? req->property : req->target;
    xcb_selection_notify_event_t notify = {
        .response_type = XCB_SELECTION_NOTIFY, .time = req->time, .requestor = req->requestor,
        .selection = req->selection, .target = req->target,
    };
    bool busy = false;
    for (transfer *t = d->transfers; t; t = t->next)
        if (t->window == req->requestor && t->property == property) busy = true;

    error err = NULL;
    if (!busy && req->owner == d->window && s && s->time) {
        const void *data = NULL;
        uint32_t count = 0;
        uint8_t format = 32;
        xcb_atom_t type = XCB_NONE;
        xcb_atom_t supported[] = {d->targets, d->timestamp, d->utf8};
        uint32_t length = (uint32_t)s->bytes.size;
        if (req->target == d->targets) {
            type = XCB_ATOM_ATOM;
            count = sizeof supported / sizeof supported[0];
            data = supported;
        } else if (req->target == d->timestamp) {
            type = XCB_ATOM_INTEGER;
            count = 1;
            data = &s->time;
        } else if (req->target == d->utf8 && s->bytes.size <= d->chunk) {
            type = d->utf8;
            format = 8;
            count = length;
            data = s->bytes.data;
        } else if (req->target == d->utf8) {
            transfer *t = calloc(1, sizeof *t);
            if (!t || !append(&t->bytes, s->bytes.data, s->bytes.size)) {
                free(t);
                return "MEMORY";
            }
            t->window = req->requestor;
            t->property = property;
            err = watch(d, t->window, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
            t->next = d->transfers;
            d->transfers = t;
            if (err) remove_transfer(&d->transfers);
            type = d->incr;
            count = 1;
            data = &length;
        }
        if (!err && type) {
            err = request(d, xcb_change_property_checked(d->conn, XCB_PROP_MODE_REPLACE,
                req->requestor, property, type, format, count, data), "Set selection property");
            if (!err) notify.property = property;
        }
    }
    if (!err) {
        char wire[32] = {0}; /* XSendEvent always copies one 32-byte event. */
        memcpy(wire, &notify, sizeof notify);
        err = request(d, xcb_send_event_checked(d->conn, 0, req->requestor, 0, wire),
                      "Send SelectionNotify");
    }
    return bad_window(err) ? NULL : err;
}

static error advance_transfer(driver *d, const xcb_property_notify_event_t *p)
{
    if (p->state != XCB_PROPERTY_DELETE) return NULL;
    for (transfer **slot = &d->transfers; *slot; slot = &(*slot)->next) {
        transfer *t = *slot;
        if (t->window != p->window || t->property != p->atom) continue;
        size_t length = t->bytes.size - t->offset;
        if (length > d->chunk) length = d->chunk;
        error err = request(d, xcb_change_property_checked(d->conn, XCB_PROP_MODE_REPLACE,
            t->window, t->property, d->utf8, 8, (uint32_t)length, t->bytes.data + t->offset),
            "Send INCR chunk");
        t->offset += length;
        if (!length || err) remove_transfer(slot);
        return bad_window(err) ? NULL : err;
    }
    return NULL;
}

static error service(driver *d, const xcb_generic_event_t *event)
{
    uint8_t type = event->response_type;
    if (!type) {
        const xcb_generic_error_t *e = (const void *)event;
        fprintf(stderr, "xdriver: asynchronous X error %u\n", e->error_code);
        return "X_ASYNC";
    }
    if (type == XCB_SELECTION_REQUEST)
        return send_selection(d, (const void *)event);
    if (type == XCB_PROPERTY_NOTIFY)
        return advance_transfer(d, (const void *)event);
    if (type == XCB_SELECTION_CLEAR) {
        const xcb_selection_clear_event_t *clear = (const void *)event;
        selection *s = get_selection(d, clear->selection);
        if (!s || clear->owner != d->window) return NULL;
        xcb_window_t actual;
        error err = owner(d, s->atom, &actual);
        if (err) return err;
        if (actual != d->window) {
            free(s->bytes.data);
            s->bytes = (buffer){0};
            s->time = XCB_CURRENT_TIME;
        }
        /* Already accepted INCR transfers own their byte snapshots. */
    } else if (type == XCB_DESTROY_NOTIFY) {
        const xcb_destroy_notify_event_t *x = (const void *)event;
        for (transfer **slot = &d->transfers; *slot;) {
            if ((*slot)->window == x->window) remove_transfer(slot);
            else slot = &(*slot)->next;
        }
    }
    return NULL;
}

static error server_time(driver *d, xcb_window_t window, int64_t deadline, xcb_timestamp_t *time)
{
    uint8_t value = 0;
    xcb_void_cookie_t cookie = xcb_change_property_checked(d->conn, XCB_PROP_MODE_REPLACE,
        window, d->clock, XCB_ATOM_INTEGER, 8, 1, &value);
    error err = request(d, cookie, "Request timestamp");
    if (err) return err;
    xcb_generic_event_t *e;
    while (!(err = next_event(d, deadline, &e))) {
        if (e->response_type == XCB_PROPERTY_NOTIFY) {
            const xcb_property_notify_event_t *p = (const void *)e;
            if (p->window == window && p->atom == d->clock && p->state == XCB_PROPERTY_NEW_VALUE &&
                p->sequence == (uint16_t)cookie.sequence) {
                *time = p->time;
                free(e);
                return NULL;
            }
        }
        err = service(d, e);
        free(e);
        if (err) break;
    }
    return err;
}

static error own_selection(driver *d, selection *s, buffer *bytes, int64_t deadline)
{
    xcb_timestamp_t time;
    error err = server_time(d, d->window, deadline, &time);
    if (!err) err = request(d, xcb_set_selection_owner_checked(d->conn, d->window, s->atom,
        time), "SetSelectionOwner");
    if (!err) err = expect_owner(d, s->atom, d->window);
    if (err) return err;
    free(s->bytes.data);
    s->bytes = *bytes;
    s->time = time;
    *bytes = (buffer){0};
    return NULL;
}

enum receive_state { WAIT_NOTIFY, WAIT_CHUNK, RECEIVED };

typedef struct {
    enum receive_state state;
    xcb_timestamp_t time;
    uint32_t minimum;
    buffer bytes;
} receiver;

static error receive_property(driver *d, receiver *r)
{
    /* GetProperty lengths are in four-byte units. Keep the reply length
       representable by libxcb's signed value-length API. */
    xcb_get_property_reply_t *p = xcb_get_property_reply(d->conn, xcb_get_property(d->conn, 1,
        d->read_window, d->property, XCB_GET_PROPERTY_TYPE_ANY, 0, INT_MAX / 4), NULL);
    if (!p) return "X_SELECTION_PROPERTY";
    error err = NULL;
    if (p->bytes_after) err = "SELECTION_TRUNCATED";
    else if (r->state == WAIT_NOTIFY && p->type == d->incr && p->format == 32 && p->value_len == 1) {
        memcpy(&r->minimum, xcb_get_property_value(p), sizeof r->minimum);
        r->state = WAIT_CHUNK;
    } else if (p->type != d->utf8 || p->format != 8) {
        err = "SELECTION_FORMAT";
    } else if (!append(&r->bytes, xcb_get_property_value(p), p->value_len)) {
        err = "MEMORY";
    } else if (r->state == WAIT_NOTIFY || !p->value_len) {
        if (r->bytes.size < r->minimum) err = "INCOMPLETE_INCR";
        else r->state = RECEIVED;
    }
    free(p);
    return err;
}

static error receive_event(driver *d, receiver *r, xcb_atom_t selection_atom,
                           const xcb_generic_event_t *e)
{
    switch (r->state) {
    case WAIT_NOTIFY:
        if ((e->response_type & 0x7fu) == XCB_SELECTION_NOTIFY) {
            const xcb_selection_notify_event_t *n = (const void *)e;
            if (n->requestor != d->read_window) break;
            if (n->selection != selection_atom || n->target != d->utf8 || n->time != r->time)
                return "SELECTION_NOTIFY";
            if (!n->property) return "SELECTION_REFUSED";
            if (n->property != d->property) return "SELECTION_NOTIFY";
            return receive_property(d, r);
        }
        break;
    case WAIT_CHUNK:
        if (e->response_type == XCB_PROPERTY_NOTIFY) {
            const xcb_property_notify_event_t *p = (const void *)e;
            if (p->window == d->read_window && p->atom == d->property && p->state == XCB_PROPERTY_NEW_VALUE)
                return receive_property(d, r);
        }
        break;
    case RECEIVED:
        break;
    }
    return NULL;
}

static error convert_selection(driver *d, xcb_atom_t selection_atom, xcb_window_t expected,
                               xcb_timestamp_t time)
{
    error err = request(d, xcb_grab_server_checked(d->conn), "GrabServer for conversion");
    if (err) return err;
    err = expect_owner(d, selection_atom, expected);
    if (!err) err = request(d, xcb_convert_selection_checked(d->conn, d->read_window,
        selection_atom, d->utf8, d->property, time), "ConvertSelection");
    error release = request(d, xcb_ungrab_server_checked(d->conn), "UngrabServer after conversion");
    return release ? release : err;
}

static error read_selection(driver *d, xcb_atom_t selection_atom, xcb_window_t expected,
                            int64_t deadline, buffer *bytes)
{
    error err = expect_owner(d, selection_atom, expected);
    if (!err) err = new_window(d, &d->read_window);
    if (err) return err;
    receiver r = {0};
    err = server_time(d, d->read_window, deadline, &r.time);
    if (!err) err = convert_selection(d, selection_atom, expected, r.time);
    while (!err && r.state != RECEIVED) {
        xcb_generic_event_t *event;
        err = next_event(d, deadline, &event);
        if (err) break;
        err = service(d, event);
        if (!err) err = receive_event(d, &r, selection_atom, event);
        free(event);
    }
    if (err && !strcmp(err, "TIMEOUT") && r.state == WAIT_CHUNK) err = "INCOMPLETE_INCR";
    error provenance = expect_owner(d, selection_atom, expected);
    if (provenance) err = provenance;
    error destroy = request(d, xcb_destroy_window_checked(d->conn, d->read_window), "Destroy requestor");
    d->read_window = XCB_NONE;
    if (!err) err = destroy;
    if (err) free(r.bytes.data);
    else *bytes = r.bytes;
    return err;
}

static error window_matches(driver *d, xcb_window_t window, uint32_t pid, bool *matches)
{
    xcb_generic_error_t *e = NULL;
    *matches = false;
    xcb_get_window_attributes_reply_t *attrs = xcb_get_window_attributes_reply(d->conn,
        xcb_get_window_attributes(d->conn, window), &e);
    if (!attrs) return reply_error(d, e, "GetWindowAttributes");
    bool eligible = attrs->map_state == XCB_MAP_STATE_VIEWABLE &&
        attrs->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT;
    uint32_t mask = attrs->your_event_mask;
    free(attrs);
    if (!eligible) return NULL;

    /* Arm property events before reading PID, including for a late publisher. */
    if (!(mask & XCB_EVENT_MASK_PROPERTY_CHANGE)) {
        mask |= XCB_EVENT_MASK_PROPERTY_CHANGE;
        error err = request(d, xcb_change_window_attributes_checked(d->conn, window,
            XCB_CW_EVENT_MASK, &mask), "Watch window PID");
        if (err) return err;
    }
    xcb_get_property_reply_t *p = xcb_get_property_reply(d->conn,
        xcb_get_property(d->conn, 0, window, d->pid, XCB_ATOM_CARDINAL, 0, 1), &e);
    if (!p) return reply_error(d, e, "Get _NET_WM_PID");
    if (p->type == XCB_ATOM_CARDINAL && p->format == 32 && p->value_len == 1 && !p->bytes_after) {
        uint32_t actual;
        memcpy(&actual, xcb_get_property_value(p), sizeof actual);
        *matches = actual == pid;
    }
    free(p);
    return NULL;
}

static error find_mapped(driver *d, uint32_t pid, xcb_window_t *found)
{
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(d->conn,
        xcb_query_tree(d->conn, d->screen->root), NULL);
    if (!tree) return "X_TREE";
    xcb_window_t *children = xcb_query_tree_children(tree);
    int count = xcb_query_tree_children_length(tree);
    error err = NULL;
    *found = XCB_NONE;
    for (int i = 0; i < count; ++i) {
        bool matches;
        err = window_matches(d, children[i], pid, &matches);
        if (bad_window(err)) {
            err = NULL; /* QueryTree raced a child being destroyed. */
            continue;
        }
        if (err) break;
        if (matches) {
            if (*found) {
                err = "AMBIGUOUS_PID";
                break;
            }
            *found = children[i];
        }
    }
    free(tree);
    return err;
}

static bool any_button(driver *d)
{
    for (size_t i = 1; i < sizeof d->buttons / sizeof d->buttons[0]; ++i)
        if (d->buttons[i]) return true;
    return false;
}

static error find_target(driver *d, uint32_t pid, int64_t deadline)
{
    if (any_button(d)) return "BUTTON_HELD";
    d->focused = false;
    d->target = XCB_NONE;
    error err = find_mapped(d, pid, &d->target);
    while (!err && !d->target) {
        xcb_generic_event_t *e;
        err = next_event(d, deadline, &e);
        if (err) break;
        err = service(d, e);
        bool scan = e->response_type == XCB_MAP_NOTIFY || e->response_type == XCB_REPARENT_NOTIFY;
        if (e->response_type == XCB_PROPERTY_NOTIFY) {
            const xcb_property_notify_event_t *p = (const void *)e;
            if (p->atom == d->pid) scan = true;
        }
        free(e);
        if (!err && scan) err = find_mapped(d, pid, &d->target);
    }
    if (err) d->target = XCB_NONE;
    return err;
}

static error verify_focus(driver *d)
{
    xcb_get_input_focus_reply_t *r = xcb_get_input_focus_reply(d->conn,
        xcb_get_input_focus(d->conn), NULL);
    if (!r) return "X_FOCUS";
    bool matches = r->focus == d->target;
    free(r);
    if (!matches) {
        d->focused = false;
        return "FOCUS_LOST";
    }
    return NULL;
}

static error set_focus(driver *d)
{
    d->focused = false;
    if (!d->target) return "NO_TARGET";
    error err = request(d, xcb_set_input_focus_checked(d->conn, XCB_INPUT_FOCUS_PARENT,
        d->target, XCB_CURRENT_TIME), "SetInputFocus");
    if (!err) err = verify_focus(d);
    d->focused = !err;
    return err;
}

static error fake(driver *d, uint8_t type, uint8_t detail, int16_t x, int16_t y)
{
    return request(d, d->fake(d->conn, type, detail, XCB_CURRENT_TIME,
        d->screen->root, x, y, 0), "XTest FakeInput");
}

static bool modifier(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
    case XKB_KEY_Super_L: case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
    case XKB_KEY_ISO_Level3_Shift: case XKB_KEY_ISO_Level5_Shift:
    case XKB_KEY_Mode_switch:
        return true;
    default:
        return false;
    }
}

static uint8_t mapped_key(driver *d, struct xkb_state *state, xkb_keysym_t sym)
{
    const xcb_setup_t *setup = xcb_get_setup(d->conn);
    for (xkb_keycode_t code = setup->min_keycode; code <= setup->max_keycode; ++code)
        if (xkb_state_key_get_one_sym(state, code) == sym) return (uint8_t)code;
    return 0;
}

static error keyboard_idle(driver *d)
{
    xcb_query_keymap_reply_t *r = xcb_query_keymap_reply(d->conn, xcb_query_keymap(d->conn), NULL);
    if (!r) return "X_KEYMAP";
    bool idle = true;
    for (size_t i = 0; i < sizeof r->keys; ++i)
        if (r->keys[i]) idle = false;
    free(r);
    return idle ? NULL : "KEYBOARD_BUSY";
}

static struct xkb_state *keyboard_model(driver *d)
{
    struct xkb_keymap *map = xkb_x11_keymap_new_from_device(d->xkb, d->conn, d->keyboard,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!map) return NULL;
    struct xkb_state *server = xkb_x11_state_new_from_device(map, d->conn, d->keyboard);
    struct xkb_state *model = server ? xkb_state_new(map) : NULL;
    if (model) {
        /* QueryKeymap already confirmed no depressed physical keys. Seed a
           separate simulation with the active locks, latches and layout. */
        xkb_state_update_mask(model, 0,
            xkb_state_serialize_mods(server, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(server, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(server, XKB_STATE_LAYOUT_DEPRESSED),
            xkb_state_serialize_layout(server, XKB_STATE_LAYOUT_LATCHED),
            xkb_state_serialize_layout(server, XKB_STATE_LAYOUT_LOCKED));
    }
    xkb_state_unref(server);
    xkb_keymap_unref(map);
    return model;
}

static bool already_key(const uint8_t *keys, size_t count, uint8_t code)
{
    for (size_t i = 0; i < count; ++i)
        if (keys[i] == code) return true;
    return false;
}

static uint8_t implicit_key(driver *d, struct xkb_state *state, xkb_keysym_t sym,
                            uint8_t *keys, size_t *count)
{
    /* Momentary level selectors exposed by XKB. Try combinations without
       remapping the server or switching its layout group. */
    const xkb_keysym_t levels[] = {XKB_KEY_Shift_L, XKB_KEY_ISO_Level3_Shift, XKB_KEY_ISO_Level5_Shift};
    uint8_t modifiers[sizeof levels / sizeof levels[0]];
    size_t total = sizeof modifiers / sizeof modifiers[0];
    for (size_t i = 0; i < total; ++i) {
        modifiers[i] = mapped_key(d, state, levels[i]);
        if (already_key(keys, *count, modifiers[i])) modifiers[i] = 0;
    }
    for (unsigned mask = 1; mask < (1u << total); ++mask) {
        size_t n = *count;
        bool possible = true;
        for (size_t i = 0; i < total; ++i) {
            if (!(mask & (1u << i))) continue;
            if (!modifiers[i] || already_key(keys, n, modifiers[i])) {
                possible = false;
                break;
            }
            keys[n++] = modifiers[i];
            xkb_state_update_key(state, modifiers[i], XKB_KEY_DOWN);
        }
        uint8_t code = possible ? mapped_key(d, state, sym) : 0;
        if (code && !already_key(keys, n, code)) {
            *count = n;
            return code;
        }
        while (n > *count) xkb_state_update_key(state, keys[--n], XKB_KEY_UP);
    }
    return 0;
}

static error send_keys(driver *d, const xkb_keysym_t *syms, size_t count)
{
    error err = keyboard_idle(d);
    if (err) return err;
    struct xkb_state *state = keyboard_model(d);
    if (!state) return "X_KEYMAP";
    uint8_t keys[UINT8_MAX + 1u];
    size_t n = 0;
    xkb_keysym_t sym = syms[count - 1];
    uint8_t base = mapped_key(d, state, sym);
    for (size_t i = 0; i + 1 < count; ++i) {
        uint8_t code = mapped_key(d, state, syms[i]);
        if (!modifier(syms[i]) || !code || already_key(keys, n, code)) {
            xkb_state_unref(state);
            return "CHORD_MODIFIER";
        }
        keys[n++] = code;
        xkb_state_update_key(state, code, XKB_KEY_DOWN);
    }
    uint8_t code = mapped_key(d, state, sym);
    /* Chords can name the unmodified key, e.g. Control_L Shift_L a. */
    if (!code && n) code = base;
    if (!code) code = implicit_key(d, state, sym, keys, &n);
    xkb_state_unref(state);
    if (!code || already_key(keys, n, code)) return "UNMAPPED_KEY";
    keys[n++] = code;

    size_t sent = 0;
    while (sent < n) {
        err = fake(d, XCB_KEY_PRESS, keys[sent], 0, 0);
        if (err) break;
        ++sent;
    }
    while (sent) {
        error released = fake(d, XCB_KEY_RELEASE, keys[--sent], 0, 0);
        if (!err) err = released;
    }
    return err;
}

static error move_pointer(driver *d, int16_t x, int16_t y)
{
    xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(d->conn,
        xcb_get_geometry(d->conn, d->target), NULL);
    if (!g) return "X_GEOMETRY";
    bool inside = x >= 0 && y >= 0 && x < g->width && y < g->height;
    free(g);
    if (!inside) return "POINTER_OUTSIDE";
    xcb_translate_coordinates_reply_t *r = xcb_translate_coordinates_reply(d->conn,
        xcb_translate_coordinates(d->conn, d->target, d->screen->root, x, y), NULL);
    if (!r) return "X_COORDINATES";
    int16_t root_x = r->dst_x, root_y = r->dst_y;
    bool on_target = r->same_screen && r->child == d->target;
    free(r);
    if (!on_target) return "POINTER_OUTSIDE";
    return fake(d, XCB_MOTION_NOTIFY, 0, root_x, root_y);
}

enum button_action { CLICK, DOWN, UP };

static error send_button(driver *d, uint8_t button, enum button_action action)
{
    if (d->buttons[button] != (action == UP)) return "BUTTON_STATE";
    if (action != UP) {
        xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(d->conn,
            xcb_query_pointer(d->conn, d->screen->root), NULL);
        if (!p) return "X_POINTER";
        bool on_target = p->same_screen && p->child == d->target;
        free(p);
        if (!on_target) return "POINTER_OUTSIDE";
        error err = fake(d, XCB_BUTTON_PRESS, button, 0, 0);
        if (err) return err;
        d->buttons[button] = true;
    }
    if (action == DOWN) return NULL;
    error err = fake(d, XCB_BUTTON_RELEASE, button, 0, 0);
    if (!err) d->buttons[button] = false;
    return err;
}

static error send_marker(driver *d, const uint32_t words[static 5])
{
    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = d->target, .type = d->marker,
    };
    memcpy(event.data.data32, words, sizeof event.data.data32);
    return request(d, xcb_send_event_checked(d->conn, 0, d->target, XCB_EVENT_MASK_NO_EVENT,
        (const char *)&event), "Send contract marker");
}

static bool number(const char *text, uint32_t *result)
{
    if (text[0] < '0' || text[0] > '9') return false;
    int base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, base);
    if (errno || *end || value > UINT32_MAX) return false;
    *result = (uint32_t)value;
    return true;
}

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The input line can be reused after decoding. Transfer ownership of a
   separate allocation, so replacements and pipelined commands cannot alias. */
static error decode(const char *hex, buffer *bytes)
{
    if (!strcmp(hex, "-")) return NULL;
    size_t length = strlen(hex);
    if (!length || length % 2 || length / 2 > UINT32_MAX) return "BAD_BYTES";
    bytes->data = malloc(length / 2);
    if (!bytes->data) return "MEMORY";
    bytes->size = bytes->capacity = length / 2;
    for (size_t i = 0; i < bytes->size; ++i) {
        int high = hex_digit((unsigned char)hex[2 * i]);
        int low = hex_digit((unsigned char)hex[2 * i + 1]);
        if (high < 0 || low < 0) return "BAD_BYTES";
        bytes->data[i] = (uint8_t)(16 * high + low);
    }
    return NULL;
}

static void print_bytes(const buffer *bytes)
{
    if (!bytes->size) {
        puts("OK -");
        return;
    }
    static const char digits[] = "0123456789abcdef";
    char block[BUFSIZ];
    size_t used = 0;
    fputs("OK ", stdout);
    for (size_t i = 0; i < bytes->size; ++i) {
        uint8_t b = bytes->data[i];
        block[used++] = digits[b >> 4];
        block[used++] = digits[b & 15u];
        if (used + 2 > sizeof block) {
            (void)fwrite(block, 1, used, stdout);
            used = 0;
        }
    }
    (void)fwrite(block, 1, used, stdout);
    putchar('\n');
}

static void response(error err)
{
    if (err) printf("ERR %s\n", err);
    else puts("OK");
}

static void xid_response(error err, xcb_window_t id)
{
    if (err) response(err);
    else printf("OK %" PRIu32 "\n", id);
}

static error input_action(driver *d, size_t count, char **argv)
{
    if (!strcmp(argv[0], "motion")) {
        uint32_t x, y;
        if (count != 3 || !number(argv[1], &x) || !number(argv[2], &y) ||
            x > INT16_MAX || y > INT16_MAX) return "BAD_ARGS";
        return move_pointer(d, (int16_t)x, (int16_t)y);
    }
    if (!strcmp(argv[0], "button")) {
        uint32_t detail;
        if (count != 3 || !number(argv[1], &detail) || !detail || detail > UINT8_MAX)
            return "BAD_ARGS";
        enum button_action action;
        if (!strcmp(argv[2], "click")) action = CLICK;
        else if (!strcmp(argv[2], "down")) action = DOWN;
        else if (!strcmp(argv[2], "up")) action = UP;
        else return "BAD_ARGS";
        return send_button(d, (uint8_t)detail, action);
    }
    if ((!strcmp(argv[0], "key") && count != 2) ||
        (!strcmp(argv[0], "chord") && count < 3)) return "BAD_ARGS";
    xkb_keysym_t syms[UINT8_MAX];
    for (size_t i = 1; i < count; ++i) {
        xkb_keysym_t sym;
        const char *key = argv[i];
        if (key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
            if (!number(key, &sym)) return "BAD_KEYSYM";
        } else {
            sym = xkb_keysym_from_name(key, XKB_KEYSYM_NO_FLAGS);
        }
        if (sym == XKB_KEY_NoSymbol) return "BAD_KEYSYM";
        syms[i - 1] = sym;
    }
    return send_keys(d, syms, count - 1);
}

static error guarded_input(driver *d, size_t count, char **argv)
{
    if (!d->focused) return "FOCUS_REQUIRED";
    /* A different client cannot steal focus between verification and XTest.
       The grab covers one command only, with no waits for another client. */
    error err = request(d, xcb_grab_server_checked(d->conn), "GrabServer");
    if (err) return err;
    err = verify_focus(d);
    if (!err) err = input_action(d, count, argv);
    error release = request(d, xcb_ungrab_server_checked(d->conn), "UngrabServer");
    return release ? release : err;
}

static void command(driver *d, size_t count, char **argv)
{
    const char *name = argv[0];
    if (!strcmp(name, "find")) {
        uint32_t pid, ms;
        if (count != 3 || !number(argv[1], &pid) || !pid || !number(argv[2], &ms) || !ms) {
            response("BAD_ARGS");
            return;
        }
        error err = find_target(d, pid, now_ms() + ms);
        xid_response(err, d->target);
    } else if (!strcmp(name, "focus")) {
        response(count == 1 ? set_focus(d) : "BAD_ARGS");
    } else if (!strcmp(name, "key") || !strcmp(name, "chord") || !strcmp(name, "motion") ||
               !strcmp(name, "button")) {
        response(guarded_input(d, count, argv));
    } else if (!strcmp(name, "marker")) {
        uint32_t words[5]; /* X11 ClientMessage format 32 has five words. */
        size_t n = sizeof words / sizeof words[0];
        if (count != n + 1) {
            response("BAD_ARGS");
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            if (!number(argv[i + 1], &words[i])) {
                response("BAD_ARGS");
                return;
            }
        }
        response(d->target ? send_marker(d, words) : "NO_TARGET");
    } else if (!strcmp(name, "owner") || !strcmp(name, "own") || !strcmp(name, "read")) {
        size_t expected_count = !strcmp(name, "owner") ? 2u : 4u;
        if (count != expected_count) {
            response("BAD_ARGS");
            return;
        }
        selection *s = NULL;
        if (!strcmp(argv[1], "PRIMARY")) s = &d->selections[0];
        if (!strcmp(argv[1], "CLIPBOARD")) s = &d->selections[1];
        if (!s) {
            response("BAD_SELECTION");
            return;
        }
        if (expected_count == 2u) {
            xcb_window_t id = XCB_NONE;
            error err = owner(d, s->atom, &id);
            xid_response(err, id);
            return;
        }
        uint32_t ms;
        if (!number(argv[3], &ms) || !ms) {
            response("BAD_ARGS");
            return;
        }
        buffer bytes = {0};
        if (!strcmp(name, "own")) {
            error err = decode(argv[2], &bytes);
            if (!err) err = own_selection(d, s, &bytes, now_ms() + ms);
            xid_response(err, d->window);
        } else {
            xcb_window_t expected;
            error err = number(argv[2], &expected) && expected
                ? read_selection(d, s->atom, expected, now_ms() + ms, &bytes) : "BAD_ARGS";
            if (err) response(err);
            else print_bytes(&bytes);
        }
        free(bytes.data);
    } else if (!strcmp(name, "quit")) {
        response("BAD_ARGS");
    } else {
        response("UNKNOWN_COMMAND");
    }
}

static bool command_line(driver *d, buffer *line)
{
    bool quit = false;
    if (line->size && memchr(line->data, 0, line->size)) {
        response("BAD_LINE");
    } else if (!append(line, "", 1)) {
        response("MEMORY");
    } else {
        /* No chord can contain more keys than the 8-bit X11 keycode space. */
        char *argv[UINT8_MAX + 1u], *save;
        size_t count = 0;
        char *word = strtok_r((char *)line->data, " \t", &save);
        while (word && count < sizeof argv / sizeof argv[0]) {
            argv[count++] = word;
            word = strtok_r(NULL, " \t", &save);
        }
        if (word || !count) response("BAD_LINE");
        else if (count == 1 && !strcmp(argv[0], "quit")) {
            response(NULL);
            quit = true;
        } else command(d, count, argv);
    }
    line->size = 0;
    return quit;
}

static error run(driver *d)
{
    struct pollfd fds[] = {{STDIN_FILENO, POLLIN, 0}, {xcb_get_file_descriptor(d->conn), POLLIN, 0}};
    buffer line = {0};
    error err = NULL;
    bool quit = false;
    while (!quit && !err) {
        xcb_generic_event_t *e;
        /* Limit each drain to queued events, then check stdin even under load. */
        while ((e = xcb_poll_for_queued_event(d->conn))) {
            err = service(d, e);
            free(e);
            if (err) break;
        }
        if (err) break;
        if (xcb_connection_has_error(d->conn)) {
            err = "X_CONNECTION";
            break;
        }
        if (poll(fds, sizeof fds / sizeof fds[0], -1) < 0) {
            if (errno == EINTR) continue;
            err = "POLL";
            break;
        }
        if (fds[1].revents) {
            e = xcb_poll_for_event(d->conn);
            if (e) {
                err = service(d, e);
                free(e);
                if (err) break;
            }
        }
        if (!fds[0].revents) continue;
        uint8_t block[BUFSIZ];
        ssize_t n = read(STDIN_FILENO, block, sizeof block);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = "STDIN";
            break;
        }
        if (!n) {
            err = line.size ? "TRUNCATED_COMMAND" : NULL;
            break;
        }
        const uint8_t *p = block, *end = block + n;
        while (p < end && !quit) {
            const uint8_t *newline = memchr(p, '\n', (size_t)(end - p));
            const uint8_t *stop = newline ? newline : end;
            if (!append(&line, p, (size_t)(stop - p))) {
                err = "MEMORY";
                break;
            }
            if (!newline) break;
            quit = command_line(d, &line);
            if (fflush(stdout) || ferror(stdout)) {
                err = "STDOUT";
                break;
            }
            p = newline + 1;
        }
    }
    free(line.data);
    return err;
}

int main(int argc, char **argv)
{
    uint32_t display;
    if (argc != 3 || strcmp(argv[1], "--display") || argv[2][0] != ':' ||
        argv[2][1] < '1' || argv[2][1] > '9' || strspn(argv[2] + 1, "0123456789") != strlen(argv[2] + 1) ||
        !number(argv[2] + 1, &display)) {
        fprintf(stderr, "usage: xdriver --display :NUMBER (explicit local display > 0)\n");
        return 2;
    }
    driver d = {0};
    error err = connect_display(&d, argv[2]);
    if (!err) {
        printf("READY %zu\n", d.max_request);
        err = fflush(stdout) ? "STDOUT" : run(&d);
    }
    if (d.conn && !xcb_connection_has_error(d.conn)) {
        for (size_t i = 1; i < sizeof d.buttons / sizeof d.buttons[0]; ++i) {
            if (!d.buttons[i]) continue;
            error release = fake(&d, XCB_BUTTON_RELEASE, (uint8_t)i, 0, 0);
            if (!err) err = release;
        }
    }
    while (d.transfers) remove_transfer(&d.transfers);
    for (size_t i = 0; i < sizeof d.selections / sizeof d.selections[0]; ++i)
        free(d.selections[i].bytes.data);
    if (d.xkb) xkb_context_unref(d.xkb);
    if (d.xtest) dlclose(d.xtest);
    if (d.conn) xcb_disconnect(d.conn);
    if (err) fprintf(stderr, "xdriver: %s\n", err);
    return err ? 1 : 0;
}
