/* x11_live_test.c - optional live-X checks (P2.2): keyboard/XI2 setup and a clipboard round trip between two
 * connections in one process. Skips cleanly (exit 0) with no display. */
#include "x11/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail = 1; } } while (0)

typedef struct sink { int arrived, failed, lost; uint8_t which; } sink;
static void on_ev(void *ud, const plat_event *e) {
    sink *s = ud;
    if (e->kind != PLAT_EV_CLIPBOARD) return;
    s->which = e->clip_which;
    if (e->code == 0) s->arrived++; else if (e->code == 1) s->failed++; else s->lost++;
}

static int open_plat(plat *p) {
    plat_config cfg = { "x11 live", 100, 100, getenv("DISPLAY") == NULL, -1, 0 };
    return plat_init(p, &cfg);
}

static void pump(plat *a, sink *sa, plat *b, sink *sb, int rounds) {
    plat_callbacks ca = { sa, on_ev, NULL, NULL, NULL }, cb = { sb, on_ev, NULL, NULL, NULL };
    for (int i = 0; i < rounds; i++) { plat_run_for(a, &ca, 10); plat_run_for(b, &cb, 10); }
}

static void roundtrip(plat *a, sink *sa, plat *b, sink *sb, int which, const char *text) {
    size_t n = strlen(text), got = 0;
    CHECK(plat_clip_set(a, which, text, n) == PLAT_OK, "set %d", which);
    sb->arrived = sb->failed = 0;
    CHECK(plat_clip_request(b, which) == PLAT_OK, "request %d", which);
    for (int i = 0; i < 50 && !sb->arrived && !sb->failed; i++) pump(a, sa, b, sb, 1);
    CHECK(sb->arrived == 1, "arrived (selection %d)", which);
    const uint8_t *d = plat_clip_data(b, &got);
    CHECK(d && got == n && memcmp(d, text, n) == 0, "data matches (selection %d, got %zu bytes)", which, got);
}

int main(void) {
    plat a, b;
    int r = open_plat(&a);
    if (r == PLAT_ERR_NO_DISPLAY) { puts("x11_live_test: no display, skipped"); return 0; }
    if (r != PLAT_OK) { fprintf(stderr, "plat_init failed (%d)\n", r); return 1; }
    CHECK(open_plat(&b) == PLAT_OK, "second connection");
    printf("xi2 %s, core wheel fallback %d\n", a.xi ? "active" : "unavailable", a.core_wheel);
    sink sa, sb;
    memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_CLIPBOARD, "h\xc3\xa9llo clipboard \xe2\x82\xac");
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_PRIMARY, "primary text");
    /* large (1 MiB) transfer within one request */
    size_t big = 1u << 20;
    char *bb = malloc(big);
    memset(bb, 'x', big);
    bb[big - 1] = 0;
    roundtrip(&a, &sa, &b, &sb, PLAT_CLIP_CLIPBOARD, bb);
    free(bb);
    /* ownership moves: b takes CLIPBOARD, a is told */
    sa.lost = 0;
    CHECK(plat_clip_set(&b, PLAT_CLIP_CLIPBOARD, "b owns", 6) == PLAT_OK, "b set");
    for (int i = 0; i < 50 && !sa.lost; i++) pump(&a, &sa, &b, &sb, 1);
    CHECK(sa.lost == 1, "a told it lost the selection");
    /* we own it: request is answered locally */
    sb.arrived = 0;
    plat_clip_request(&b, PLAT_CLIP_CLIPBOARD);
    pump(&a, &sa, &b, &sb, 1);
    size_t got = 0;
    const uint8_t *d = plat_clip_data(&b, &got);
    CHECK(sb.arrived == 1 && d && got == 6 && memcmp(d, "b owns", 6) == 0, "self request");
    plat_shutdown(&a);
    plat_shutdown(&b);
    if (g_fail) { puts("x11_live_test: FAILED"); return 1; }
    puts("x11_live_test: ok");
    return 0;
}
