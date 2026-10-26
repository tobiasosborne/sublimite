/* Headless round-2 regressions. Native shims are confined to this TU. */
#define INITGUID
#include "font/font.h"
#include "font/dwrite.h"
#include "font/dwrite_internal.h"
#include "font_fixture.h"
#include "../../vendor/stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font_dwrite_oracle.h"
#define T(c) do { if (!(c)) { fprintf(stderr, "font_dwrite_r2_test:%d: %s\n", __LINE__, #c); return 1; } } while (0)
static DWORD load_flags;
static int fail_load;
static _Atomic int library_balance, apartment_balance, factory_releases, bad_refs, native_calls, forbidden_calls;
static _Atomic uint64_t forbidden_thread;
static const char *failure_point;
static int reached_failure, stage_mask;
static const char *const failure_points[] = {"loader", "file", "face", "analysis", "bounds", "pixels", "texture"};
static int fail_export, fail_factory;
static void native_thread(void)
{
    atomic_fetch_add(&native_calls, 1);
    if (os_thread_self() == atomic_load(&forbidden_thread)) atomic_fetch_add(&forbidden_calls, 1);
}
static HMODULE WINAPI test_load(LPCWSTR name)
{
    load_flags = 0;
    HMODULE m = fail_load ? NULL : LoadLibraryW(name);
    if (m) atomic_fetch_add(&library_balance, 1);
    return m;
}
static HMODULE WINAPI test_load_ex(LPCWSTR name, HANDLE file, DWORD flags)
{
    load_flags = flags;
    HMODULE m = fail_load ? NULL : LoadLibraryExW(name, file, flags);
    if (m) atomic_fetch_add(&library_balance, 1);
    return m;
}
static BOOL WINAPI test_free(HMODULE library)
{
    BOOL ok = FreeLibrary(library);
    if (ok) atomic_fetch_sub(&library_balance, 1);
    return ok;
}
static HRESULT WINAPI test_initialize(LPVOID reserved, DWORD flags)
{
    native_thread();
    HRESULT hr = CoInitializeEx(reserved, flags);
    if (SUCCEEDED(hr)) atomic_fetch_add(&apartment_balance, 1);
    return hr;
}
static void WINAPI test_uninitialize(void)
{ CoUninitialize(); atomic_fetch_sub(&apartment_balance, 1); }
static HRESULT WINAPI unavailable_factory(DWRITE_FACTORY_TYPE type, REFIID iid, IUnknown **out)
{ (void)type; (void)iid; *out = NULL; return E_NOINTERFACE; }
static FARPROC WINAPI test_proc(HMODULE library, LPCSTR name)
{
    if (!strcmp(name, "DWriteCreateFactory")) {
        if (fail_export) return NULL;
        if (fail_factory) {
            HRESULT (WINAPI *fn)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **) = unavailable_factory;
            FARPROC proc; memcpy(&proc, &fn, sizeof proc); return proc;
        }
    }
    return GetProcAddress(library, name);
}
static int inject_failure(const char *point)
{
    for (size_t i = 0; i < sizeof failure_points / sizeof *failure_points; i++)
        if (!strcmp(point, failure_points[i])) stage_mask |= 1 << i;
    if (failure_point && !strcmp(failure_point, point)) { reached_failure++; return 1; }
    return 0;
}
static void check_refs(const char *kind, ULONG refs)
{
    if (!strcmp(kind, "factory")) {
        atomic_fetch_add(&factory_releases, 1);
        if (refs) atomic_fetch_add(&bad_refs, 1);
    } else if ((!strcmp(kind, "loader") || !strcmp(kind, "stream")) && refs != 1u)
        atomic_fetch_add(&bad_refs, 1);
    else if (!strcmp(kind, "unregister") && FAILED((HRESULT)refs)) atomic_fetch_add(&bad_refs, 1);
}
#define LoadLibraryW test_load
#define LoadLibraryExW test_load_ex
#define FreeLibrary test_free
#define CoInitializeEx test_initialize
#define CoUninitialize test_uninitialize
#define GetProcAddress test_proc
#define FONT_DWRITE_TEST_FAIL(point) inject_failure(point)
#define FONT_DWRITE_TEST_REFS(kind, refs) check_refs(kind, (ULONG)(refs))
#ifndef FONT_DWRITE_TEST_SOURCE
#define FONT_DWRITE_TEST_SOURCE "../../src/font/dwrite_win.c"
#endif
#include FONT_DWRITE_TEST_SOURCE
#undef LoadLibraryW
#undef LoadLibraryExW
#undef FreeLibrary
#undef CoInitializeEx
#undef CoUninitialize
#undef GetProcAddress
static const char *candidate_override;
static int candidate_path(IDWriteFont *font, char *path, size_t cap, uint32_t *index)
{
    if (candidate_override) {
        const char *p = candidate_override; candidate_override = NULL;
        if (strlen(p) >= cap) return 0;
        strcpy(path, p); *index = 0; return 1;
    }
    return font_dwrite_path(font, path, cap, index);
}
#define font_dwrite_path candidate_path
static const WCHAR *user_locale = L"de-DE";
static WCHAR observed_locale[LOCALE_NAME_MAX_LENGTH];
static int WINAPI user_locale_name(LPWSTR out, int cap)
{
    size_t n = wcslen(user_locale) + 1u;
    if (n > (size_t)cap) return 0;
    memcpy(out, user_locale, n * sizeof *out); return (int)n;
}
static void observe_source(IDWriteTextAnalysisSource *source)
{
    const WCHAR *locale = NULL; UINT32 length = 0;
    if (SUCCEEDED(source->lpVtbl->GetLocaleName(source, 0, &length, &locale)) && locale)
        (void)wcscpy(observed_locale, locale);
}
#define GetUserDefaultLocaleName user_locale_name
#define FONT_DWRITE_SOURCE_TEST(source) observe_source(source)
#ifndef FONT_DISCOVERY_TEST_SOURCE
#define FONT_DISCOVERY_TEST_SOURCE "../../src/font/discovery_win.c"
#endif
#include FONT_DISCOVERY_TEST_SOURCE
#undef font_dwrite_path
#undef GetUserDefaultLocaleName

static uint16_t be16(const unsigned char *p)
{ return (uint16_t)((uint16_t)p[0] * 256u + p[1]); }
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)be16(p) * 65536u + be16(p + 2); }
static void put16(unsigned char *p, int v)
{ p[0] = (unsigned char)((unsigned)v >> 8); p[1] = (unsigned char)v; }
static size_t table(const unsigned char *p, const char *tag)
{
    for (size_t i = 0; i < be16(p + 4); i++) {
        const unsigned char *r = p + 12 + 16 * i;
        if (!memcmp(r, tag, 4)) return be32(r + 8);
    }
    return 0;
}
static int same_bitmap(const font_bitmap *a, const font_bitmap *b)
{
    return a->w == b->w && a->h == b->h && !memcmp(&a->m, &b->m, sizeof a->m) &&
           (!a->w || !a->h || !memcmp(a->pixels, b->pixels, (size_t)a->w * a->h));
}
static void ink_box(const uint8_t *cell, int box[4])
{
    box[0] = box[1] = 64; box[2] = box[3] = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        /* (P) Half of the R8 full-coverage value, rounded down. */
        if (cell[(size_t)y * 64u + (size_t)x] <= 255 / 2) continue;
        if (x < box[0]) box[0] = x;
        if (y < box[1]) box[1] = y;
        if (x + 1 > box[2]) box[2] = x + 1;
        if (y + 1 > box[3]) box[3] = y + 1;
    }
}
static int origin_case(void)
{
    size_t len;
    const unsigned char *data = font_embedded_data(&len);
    unsigned char *copy = malloc(len); T(copy);
    font_t f; T(font_init(&f, data, len) == FONT_OK && font_set_px(&f, 30) == FONT_OK);
    const stbtt_fontinfo *info = (const stbtt_fontinfo *)(const void *)f.info;
    uint32_t chosen[2] = {0, 0};
    for (uint32_t cp = 0x21; cp < 0x10000 && (!chosen[0] || !chosen[1]); cp++) {
        uint32_t gid = font_glyph_id(&f, cp);
        int adv, lsb, x0, y0, x1, y1;
        if (!gid || !stbtt_GetGlyphBox(info, (int)gid, &x0, &y0, &x1, &y1)) continue;
        stbtt_GetGlyphHMetrics(info, (int)gid, &adv, &lsb);
        if (lsb < 0 && !chosen[0]) chosen[0] = cp;
        if (lsb > 0 && !chosen[1]) chosen[1] = cp;
    }
    T(chosen[0] && chosen[1]);
    edit_arena a; T(edit_arena_init(&a, 1u << 20) == 0);
    for (size_t i = 0; i < sizeof chosen / sizeof *chosen; i++) {
        memcpy(copy, data, len);
        size_t hmtx = table(copy, "hmtx"), hhea = table(copy, "hhea"), head = table(copy, "head");
        T(hmtx && hhea && head);
        uint32_t gid = font_glyph_id(&f, chosen[i]);
        uint32_t nh = be16(copy + hhea + 34);
        size_t at = hmtx + (gid < nh ? 4u * gid + 2u : 4u * nh + 2u * (gid - nh));
        int lsb = (int16_t)be16(copy + at);
        /* (P) A quarter em separates an origin error from AA edge differences. */
        put16(copy + at, lsb + (int)be16(copy + head + 18) / 4);
        put16(copy + head + 16, be16(copy + head + 16) & ~2u);
        font_t changed; T(font_init(&changed, copy, len) == FONT_OK && font_set_px(&changed, 30) == FONT_OK);
        font_bitmap native, shifted, stb, changed_stb;
        T(font_dwrite_raster(&f, gid, &a, &native, 0) == FONT_OK);
        T(font_dwrite_raster(&changed, gid, &a, &shifted, 0) == FONT_OK);
        T(font_raster_glyph_stb(&f, chosen[i], &a, &stb) == FONT_OK);
        T(font_raster_glyph_stb(&changed, chosen[i], &a, &changed_stb) == FONT_OK);
        printf("origin U+%04x lsb=%d native/stb left=%d/%d changed-native=%d\n",
               chosen[i], lsb, native.m.bearing_x, stb.m.bearing_x, shifted.m.bearing_x);
        T(same_bitmap(&stb, &changed_stb));
        T(same_bitmap(&native, &shifted));
        uint8_t original_cell[64 * 64] = {0}, changed_cell[64 * 64] = {0};
        T(font_place_in_cell(&f, &native, original_cell, 64, 64) == FONT_OK);
        T(font_place_in_cell(&changed, &shifted, changed_cell, 64, 64) == FONT_OK);
        T(!memcmp(original_cell, changed_cell, sizeof original_cell));
        uint8_t stb_cell[sizeof original_cell] = {0};
        T(font_place_in_cell(&f, &stb, stb_cell, 64, 64) == FONT_OK);
        int native_box[4], stb_box[4]; ink_box(original_cell, native_box); ink_box(stb_cell, stb_box);
        printf("origin placed half-coverage box native/stb=%d,%d,%d,%d/%d,%d,%d,%d\n",
               native_box[0], native_box[1], native_box[2], native_box[3], stb_box[0], stb_box[1], stb_box[2], stb_box[3]);
        /* (M) Exact placement regression for the immutable embedded face at
         * the inherited bake size, not a universal cross-filter error bound. */
        /* (M) U+0132 right edge: native 16 minus stb 15 = 1 on Spiral;
         * their left/top/bottom coincide. The ! glyph coincides on all edges.
         * These exact deltas pin this font/size/filter pair only. */
        for (size_t edge = 0; edge < 4; edge++)
            T(native_box[edge] - stb_box[edge] == (i == 0 && edge == 2 ? 1 : 0));
        edit_arena_reset(&a);
    }
    edit_arena_free(&a); free(copy);
    puts("origin: negative/positive embedded bearings retain stb origin and native cell pixels");
    return 0;
}
static int candidate_case(int unsupported)
{
    WCHAR old_cwd[OS_PATH_MAX]; T(GetCurrentDirectoryW(OS_PATH_MAX, old_cwd));
    T(font_test_root());
    font_dwrite dw; T(font_dwrite_open(&dw, "dwrite.dll"));
    const char *fixture = "build/font-dwrite-r2-candidate.ttf";
    size_t len; const unsigned char *data = font_embedded_data(&len);
    unsigned char *copy = malloc(len); T(copy); memcpy(copy, data, len);
    font_t f; T(font_init(&f, data, len) == FONT_OK);
    uint32_t gid = font_glyph_id(&f, 'A'); T(gid);
    size_t head = table(copy, "head"), loca = table(copy, "loca"), glyf = table(copy, "glyf");
    T(head && loca && glyf);
    size_t off = be16(copy + head + 50) ? be32(copy + loca + 4u * gid) : 2u * be16(copy + loca + 2u * gid);
    put16(copy + glyf + off, 0); /* covering cmap, but no monochrome contours */
    FILE *fp = fopen(fixture, "wb"); T(fp);
    if (unsupported) T(fwrite("unsupported font", 1, sizeof "unsupported font", fp) == sizeof "unsupported font");
    else T(fwrite(copy, 1, len, fp) == len);
    T(fclose(fp) == 0);
    char path[FONT_FALLBACK_PATH_MAX]; uint32_t index;
    candidate_override = fixture;
    T(map_scalar(&dw, 'A', path, &index));
    printf("candidate: %s first face -> '%s'\n", unsupported ? "unsupported" : "blank", path);
    T(strcmp(path, fixture));
    /* Legitimate blanks must retain a covering face. */
    T(map_scalar(&dw, ' ', path, &index) && path[0]);
    T(map_scalar(&dw, 0x1f600u, path, &index));
    printf("candidate: U+1F600 accepted '%s' index=%u\n", path, index);
    T(remove(fixture) == 0); free(copy); font_dwrite_close(&dw);
    T(SetCurrentDirectoryW(old_cwd));
    puts("candidate: blank and unsupported first faces rejected; retry and space passed");
    return 0;
}
static int locale_case(void)
{
    (void)user_locale_name; (void)observe_source;
    /* Same Han scalar; inspect the actual source passed to MapCharacters. */
    font_dwrite dw; T(font_dwrite_open(&dw, "dwrite.dll"));
    const WCHAR *locales[] = {L"de-DE", L"ja-JP"};
    for (size_t i = 0; i < sizeof locales / sizeof *locales; i++) {
        char path[FONT_FALLBACK_PATH_MAX]; uint32_t index;
        user_locale = locales[i]; observed_locale[0] = 0;
        T(map_scalar(&dw, 0x4e2du, path, &index));
        printf("locale: requested=%ls delivered=%ls\n", user_locale, observed_locale);
        T(!wcscmp(user_locale, observed_locale));
    }
    font_dwrite_close(&dw);
    puts("locale: general Han lookup follows user locale");
    return 0;
}
static int dll_case(void)
{
    (void)test_load; (void)test_load_ex;
    font_dwrite dw; T(font_dwrite_open(&dw, "dwrite.dll"));
    printf("dll: requested flags=0x%lx\n", load_flags);
    T(load_flags == LOAD_LIBRARY_SEARCH_SYSTEM32);
    font_dwrite_close(&dw);
    fail_load = 1;
    size_t len; const unsigned char *data = font_embedded_data(&len);
    font_t f; T(font_init(&f, data, len) == FONT_OK && font_set_px(&f, 30) == FONT_OK);
    edit_arena a; T(edit_arena_init(&a, 1u << 20) == 0);
    font_bitmap ref, actual;
    T(font_raster_glyph_stb(&f, 0xe9u, &a, &ref) == FONT_OK);
    T(font_raster_glyph(&f, 0xe9u, &a, &actual) == FONT_OK);
    T(same_bitmap(&ref, &actual));
    T(!font_dwrite_open(&dw, "dwrite.dll") && !dw.library && !dw.factory && !dw.uninitialize);
    fail_load = 0; edit_arena_free(&a);
    puts("dll: System32-only load and exact stb fallback passed");
    return 0;
}
static int oracle_case(void)
{
    size_t len; const unsigned char *data = font_embedded_data(&len);
    unsigned char *copy = malloc(len); T(copy); memcpy(copy, data, len);
    font_t f; T(font_init(&f, data, len) == FONT_OK);
    uint32_t gid = font_glyph_id(&f, 'B'); T(gid);
    size_t head = table(copy, "head"), loca = table(copy, "loca"), glyf = table(copy, "glyf");
    size_t hmtx = table(copy, "hmtx"), hhea = table(copy, "hhea");
    T(head && loca && glyf && hmtx && hhea && be16(copy + head + 18) == 2048);
    size_t off = be16(copy + head + 50) ? be32(copy + loca + 4u * gid) : 2u * be16(copy + loca + 2u * gid);
    size_t next = be16(copy + head + 50) ? be32(copy + loca + 4u * (gid + 1u)) : 2u * be16(copy + loca + 2u * (gid + 1u));
    /* (P) Header 10 + endpoint 2 + instruction length 2 + flags 6 + XY 24. */
    T(next - off >= 44u);
    unsigned char *g = copy + glyf + off; memset(g, 0, next - off);
    put16(g, 1); put16(g + 2, 128); put16(g + 4, -128); put16(g + 6, 640); put16(g + 8, 640);
    put16(g + 10, 5);
    const int x[] = {128, 640, 640, 320, 320, 128};
    const int y[] = {-128, -128, 0, 0, 640, 640};
    for (size_t i = 0; i < sizeof x / sizeof *x; i++) {
        g[14 + i] = 1;
        put16(g + 20 + 2 * i, x[i] - (i ? x[i - 1] : 0));
        put16(g + 32 + 2 * i, y[i] - (i ? y[i - 1] : 0));
    }
    uint32_t nh = be16(copy + hhea + 34); T(nh);
    put16(copy + hmtx + 4u * (gid < nh ? gid : nh - 1u), 1024);
    put16(copy + hmtx + (gid < nh ? 4u * gid + 2u : 4u * nh + 2u * (gid - nh)), 128);
    T(font_init(&f, copy, len) == FONT_OK && font_set_em_px(&f, 32) == FONT_OK);
    /* (P) Scale 32/2048=1/64. L: x=[2,5), y=[-10,0) plus
     * x=[2,10), y=[0,2). Every contour edge is on a pixel boundary.
     * Area=3*10+8*2=46; exact integral coverage=46*255, no filter budget. */
    uint8_t pixels[8 * 12] = {0}, corrupt[sizeof pixels];
    for (size_t row = 0; row < 12; row++)
        for (size_t col = 0; col < (row < 10 ? 3u : 8u); col++) pixels[row * 8 + col] = 255;
    font_bitmap expected = {pixels, 8, 12, {16, 2, -10, 8, 12}};
    font_bitmap bad = expected; bad.pixels = corrupt;
    for (size_t row = 0; row < 12; row++) memcpy(corrupt + row * 8, pixels + (11 - row) * 8, 8);
    printf("oracle: row-flipped equal-sum artwork accepted=%d\n", font_test_artwork_equal(&f, &expected, &bad));
    T(!font_test_artwork_equal(&f, &expected, &bad));
    for (size_t row = 0; row < 12; row++)
        for (size_t col = 0; col < 8; col++) corrupt[row * 8 + (col + 1u) % 8u] = pixels[row * 8 + col];
    T(!font_test_artwork_equal(&f, &expected, &bad));
    bad = expected; bad.m.bearing_x++;
    T(!font_test_artwork_equal(&f, &expected, &bad));
    edit_arena a; T(edit_arena_init(&a, 1u << 20) == 0);
    font_bitmap native, stb;
    T(font_dwrite_raster(&f, gid, &a, &native, 0) == FONT_OK);
    T(font_raster_glyph_stb(&f, 'B', &a, &stb) == FONT_OK);
    printf("oracle: native box=%d,%d,%u,%u advance=%d; expected=2,-10,8,12 advance=16\n",
           native.m.bearing_x, native.m.bearing_y, native.w, native.h, native.m.advance);
    T(font_test_artwork_equal(&f, &expected, &stb));
    T(font_test_artwork_equal(&f, &expected, &native));
    edit_arena_free(&a); free(copy);
    puts("oracle: analytic L placement/pixels exact; row flip, ink shift and bearing corruption rejected");
    return 0;
}
static int failure_case(const char *point)
{
    (void)inject_failure; (void)check_refs;
    size_t len; const unsigned char *data = font_embedded_data(&len);
    font_t f; T(font_init(&f, data, len) == FONT_OK && font_set_px(&f, 30) == FONT_OK);
    edit_arena a; T(edit_arena_init(&a, 1u << 20) == 0);
    T(edit_arena_alloc(&a, 17, 1)); /* (G) nonzero entry mark, not an allocation budget */
    font_bitmap ref; T(font_raster_glyph_stb(&f, 0xe9u, &a, &ref) == FONT_OK);
    edit_arena_mark_t mark = edit_arena_mark(&a);
    uint32_t gid = font_glyph_id(&f, 0xe9u); T(gid);
    failure_point = point; reached_failure = 0; stage_mask = 0;
    int before = atomic_load(&factory_releases);
    font_bitmap b = {0};
    T(font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT);
    T(reached_failure == 1 && edit_arena_mark(&a) == mark && !b.pixels);
    int expected_mask = 0;
    for (size_t i = 0; i < sizeof failure_points / sizeof *failure_points; i++) {
        expected_mask |= 1 << i;
        if (!strcmp(point, failure_points[i])) break;
    }
    T(stage_mask == expected_mask);
    T(font_raster_glyph(&f, 0xe9u, &a, &b) == FONT_OK && same_bitmap(&ref, &b));
    edit_arena_reset_to_mark(&a, mark);
    T(font_raster_glyph_id(&f, gid, &a, &b) == FONT_OK && same_bitmap(&ref, &b));
    T(reached_failure == 3 && atomic_load(&factory_releases) - before == 3);
    T(!atomic_load(&bad_refs) && !atomic_load(&apartment_balance) && !atomic_load(&library_balance));
    failure_point = NULL; edit_arena_free(&a);
    printf("failure %s: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact\n", point);
    return 0;
}
static int apartment_case(const char *point, int mode)
{
    APTTYPE type; APTTYPEQUALIFIER qualifier;
    T(CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED);
    if (mode) T(CoInitializeEx(NULL, mode == 1 ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED) == S_OK);
    T(failure_case(point) == 0);
    if (mode) {
        T(CoGetApartmentType(&type, &qualifier) == S_OK);
        T(type == (mode == 1 ? APTTYPE_MTA : APTTYPE_MAINSTA));
        CoUninitialize();
    }
    T(CoGetApartmentType(&type, &qualifier) == CO_E_NOTINITIALIZED);
    printf("apartment: %s restored after %s\n", mode == 0 ? "uninitialized" : mode == 1 ? "MTA" : "STA", point);
    return 0;
}
static int service_case(void)
{
    /* Missing export/interface still acquires and releases the DLL/apartment. */
    font_dwrite dw;
    fail_export = 1; T(!font_dwrite_open(&dw, "dwrite.dll")); fail_export = 0;
    fail_factory = 1; T(!font_dwrite_open(&dw, "dwrite.dll")); fail_factory = 0;
    T(!atomic_load(&library_balance) && !atomic_load(&apartment_balance));
    puts("service: absent export and factory interface balance DLL/COM");
    return 0;
}
typedef struct mailbox_test { font_fallback *fb; font_async *async; int adopted; } mailbox_test;
static void mailbox_event(const work_msg *msg, void *arg)
{
    mailbox_test *m = arg;
    if (m->fb) m->adopted += font_fallback_event(m->fb, msg);
    if (m->async && font_async_event(m->async, msg) != RENDER_NO_SLOT) m->adopted++;
}
static int unavailable_case(void)
{
    work_pool pool; T(work_pool_init(&pool, 1, 0) == 0);
    font_fallback fb = {0}; mailbox_test mailbox = {&fb, NULL, 0};
    fail_load = 1; atomic_store(&forbidden_thread, os_thread_self());
    int before = atomic_load(&native_calls);
    work_handle h = work_submit(&pool, (work_job){font_fallback_job, &fb, 1, WORK_BULK}); T(h.epoch);
    /* (G) Same inherited discovery hang guard as font_dwrite_test. */
    uint64_t end = os_now_ns() + UINT64_C(30000000000);
    while (!atomic_load(&fb.done) && os_now_ns() < end) {
        (void)work_mailbox_drain(&pool, mailbox_event, &mailbox); os_sleep_ns(UINT64_C(1000000));
    }
    T(atomic_load(&fb.done) && mailbox.adopted == 1 && !fb.have_fontconfig && !fb.cjk[0] && !fb.emoji[0]);
    T(fb.worker != os_thread_self() && atomic_load(&native_calls) > before);
    font_async *async = NULL; T(font_async_open(&async, 30, &pool) == FONT_OK);
    const font_ascii_atlas *ascii = font_ascii_atlas_for_px(30);
    render_grid grid; render_cell cells[2]; uint64_t dirty = 0;
    T(render_grid_init(&grid, (render_dims){2, 1, ascii->cell.cell_w, ascii->cell.cell_h}, cells, 2, &dirty, 1) == RENDER_OK);
    T(font_async_bind(async, &grid) == FONT_OK);
    mailbox = (mailbox_test){NULL, async, 0};
    uint32_t slots[2];
    T(font_async_glyph(async, (const uint8_t *)"é", sizeof "é" - 1u, 1, &slots[0]) == FONT_OK);
    T(font_async_glyph(async, (const uint8_t *)"中", sizeof "中" - 1u, 2, &slots[1]) == FONT_OK);
    end = os_now_ns() + UINT64_C(30000000000);
    while (!font_async_idle(async) && os_now_ns() < end) {
        font_async_pump(async);
        (void)work_mailbox_drain(&pool, mailbox_event, &mailbox); os_sleep_ns(UINT64_C(1000000));
    }
    T(font_async_idle(async) && !font_async_pending(async) && mailbox.adopted == 2);
    T(font_async_inked(async, slots[0]) && grid.glyphs[slots[0]].page > 0);
    T(!atomic_load(&forbidden_calls) && !atomic_load(&library_balance) && !atomic_load(&apartment_balance));
    atomic_store(&forbidden_thread, 0); fail_load = 0;
    work_pool_shutdown(&pool); font_async_close(async);
    puts("unavailable: live discovery/async mailbox delivery; no native calls on UI lookup/pump/adoption");
    return 0;
}
int main(int argc, char **argv)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 1) {
        T(!origin_case() && !candidate_case(0) && !candidate_case(1) && !locale_case() && !dll_case() && !oracle_case());
        for (int mode = 0; mode < 3; mode++)
            for (size_t i = 0; i < sizeof failure_points / sizeof *failure_points; i++)
                T(!apartment_case(failure_points[i], mode));
        T(!service_case() && !unavailable_case());
        puts("font_dwrite_r2_test: origins, monochrome retry, locale, DLL, exact oracle and resource balance passed");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "origin")) return origin_case();
    if (argc == 2 && !strcmp(argv[1], "candidate")) return candidate_case(0);
    if (argc == 2 && !strcmp(argv[1], "candidate-unsupported")) return candidate_case(1);
    if (argc == 2 && !strcmp(argv[1], "locale")) return locale_case();
    if (argc == 2 && !strcmp(argv[1], "dll")) return dll_case();
    if (argc == 2 && !strcmp(argv[1], "oracle")) return oracle_case();
    if (argc == 2 && !strncmp(argv[1], "failure-", sizeof "failure-" - 1u))
        return apartment_case(argv[1] + sizeof "failure-" - 1u, 0);
    if (argc == 3 && !strncmp(argv[1], "failure-", sizeof "failure-" - 1u))
        return apartment_case(argv[1] + sizeof "failure-" - 1u, !strcmp(argv[2], "mta") ? 1 : 2);
    if (argc == 2 && !strcmp(argv[1], "service")) return service_case();
    if (argc == 2 && !strcmp(argv[1], "unavailable")) return unavailable_case();
    return 2;
}
