/* replay - re-injects the input log of a trace dump with its original timing.
 * Usage: replay [--fast] [--speed=x] dump
 * Default sink prints one line per event. Exit 1 on a malformed dump, 2 on usage. */
#include "../src/trace/trace_fmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const kind_names[TRACE_IN_KIND_COUNT] = {
    "invalid", "key_down", "key_up", "pointer_move", "button_down", "button_up",
    "wheel", "resize", "focus", "clipboard", "filechange"
};

static void print_sink(const trace_input_rec *e, void *user) {
    uint64_t base = *(const uint64_t *)user;
    const char *k = e->kind < TRACE_IN_KIND_COUNT ? kind_names[e->kind] : "?";
    printf("seq=%llu t=+%llu us %s", (unsigned long long)e->seq,
           (unsigned long long)((e->t0_ns - base) / 1000u), k);
    switch (e->kind) {
    case TRACE_IN_KEY_DOWN:
    case TRACE_IN_KEY_UP:
        printf(" keysym=0x%x state=0x%x repeat=%u utf8_len=%u", e->p.key.keysym, e->p.key.state,
               (unsigned)e->p.key.repeat, (unsigned)e->p.key.utf8_len);
        break;
    case TRACE_IN_POINTER_MOVE:
    case TRACE_IN_BUTTON_DOWN:
    case TRACE_IN_BUTTON_UP:
        printf(" x=%d y=%d button=%u mods=0x%x", e->p.pointer.x, e->p.pointer.y,
               e->p.pointer.button, e->p.pointer.mods);
        break;
    case TRACE_IN_WHEEL:
        printf(" dx=%d dy=%d mods=0x%x", e->p.wheel.dx, e->p.wheel.dy, e->p.wheel.mods);
        break;
    case TRACE_IN_RESIZE:
        printf(" w=%u h=%u", e->p.resize.w, e->p.resize.h);
        break;
    case TRACE_IN_FOCUS:
        printf(" focused=%u", e->p.focus.focused);
        break;
    case TRACE_IN_CLIPBOARD:
        printf(" selection=%u length=%u", e->p.clipboard.selection, e->p.clipboard.length);
        break;
    case TRACE_IN_FILECHANGE:
        printf(" watch_id=%u flags=0x%x", e->p.filechange.watch_id, e->p.filechange.flags);
        break;
    default:
        break;
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    trace_replay_opts o = { 0, 1.0 };
    const char *path = NULL;
    trace_loaded d;
    FILE *f;
    uint64_t base;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fast") == 0) {
            o.fast = 1;
        } else if (strncmp(argv[i], "--speed=", 8) == 0) {
            char *end = NULL;
            o.speed = strtod(argv[i] + 8, &end);
            if (end == argv[i] + 8 || *end != '\0' || !isfinite(o.speed) || !(o.speed > 0.0)) {
                fprintf(stderr, "replay: --speed must be a finite number > 0\n");
                return 2;
            }
        } else if (argv[i][0] == '-' || path != NULL) {
            path = NULL;
            break;
        } else {
            path = argv[i];
        }
    }
    if (path == NULL) {
        fprintf(stderr, "usage: replay [--fast] [--speed=x] dump\n");
        return 2;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    if (trace_fmt_load_dump(f, &d) != 0) {
        fprintf(stderr, "%s: not a valid trace dump\n", path);
        fclose(f);
        return 1;
    }
    fclose(f);
    if (!d.has_input) {
        fprintf(stderr, "%s: valid dump, no input section\n", path);
        trace_fmt_dump_free(&d);
        return 0;
    }
    base = d.nin > 0 ? d.in[0].t0_ns : 0;
    if (trace_replay(d.in, d.nin, &o, print_sink, &base) != 0) {
        trace_fmt_dump_free(&d);
        return 2;
    }
    printf("replayed %zu events (%llu dropped before dump)\n", d.nin, (unsigned long long)d.in_dropped);
    trace_fmt_dump_free(&d);
    return 0;
}
