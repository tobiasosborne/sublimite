/* src/font/fallback.c - fallback font discovery (P2.3b, edit-e6x.8).
 * Runs on a work-pool worker, never the UI thread. libfontconfig.so.1 is
 * dlopen'd with local prototypes (static-link decision: heavy system libs are
 * loaded at runtime on a worker). Missing library -> no fallback, no error.
 * Results are cached in the caller's font_fallback (fixed storage). FcFini releases fontconfig's state
 * after the paths are copied; the library handle is never dlclose'd. Not a typing-path module: libc
 * allocation inside fontconfig is acceptable here. */
#include "font/font.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Local fontconfig declarations (checked against fontconfig/fontconfig.h). */
typedef struct FcPattern FcPattern;
typedef struct FcCharSet FcCharSet;
typedef struct FcConfig  FcConfig;
typedef unsigned char    FcChar8;
typedef unsigned int     FcChar32;
typedef int              FcBool;
typedef int              FcResult;      /* FcResultMatch == 0 */
enum { FC_MATCH_PATTERN = 0, FC_RESULT_MATCH = 0 };

typedef struct fc_api {
    FcConfig  *(*InitLoadConfigAndFonts)(void);
    FcPattern *(*PatternCreate)(void);
    void       (*PatternDestroy)(FcPattern *);
    FcCharSet *(*CharSetCreate)(void);
    void       (*CharSetDestroy)(FcCharSet *);
    FcBool     (*CharSetAddChar)(FcCharSet *, FcChar32);
    FcBool     (*CharSetHasChar)(const FcCharSet *, FcChar32);
    FcBool     (*PatternAddCharSet)(FcPattern *, const char *, const FcCharSet *);
    FcBool     (*PatternAddString)(FcPattern *, const char *, const FcChar8 *);
    FcBool     (*PatternAddBool)(FcPattern *, const char *, FcBool);
    FcBool     (*ConfigSubstitute)(FcConfig *, FcPattern *, int);
    void       (*DefaultSubstitute)(FcPattern *);
    FcPattern *(*FontMatch)(FcConfig *, FcPattern *, FcResult *);
    void       (*ConfigDestroy)(FcConfig *);
    void       (*Fini)(void);
    FcResult   (*PatternGetString)(const FcPattern *, const char *, int, FcChar8 **);
    FcResult   (*PatternGetBool)(const FcPattern *, const char *, int, FcBool *);
    FcResult   (*PatternGetCharSet)(const FcPattern *, const char *, int, FcCharSet **);
} fc_api;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int fc_bind(fc_api *a, void *h)
{
#define BIND(field, sym) do { *(void **)(&a->field) = dlsym(h, sym); if (!a->field) return 0; } while (0)
    BIND(InitLoadConfigAndFonts, "FcInitLoadConfigAndFonts");
    BIND(PatternCreate, "FcPatternCreate");
    BIND(PatternDestroy, "FcPatternDestroy");
    BIND(CharSetCreate, "FcCharSetCreate");
    BIND(CharSetDestroy, "FcCharSetDestroy");
    BIND(CharSetAddChar, "FcCharSetAddChar");
    BIND(CharSetHasChar, "FcCharSetHasChar");
    BIND(PatternAddCharSet, "FcPatternAddCharSet");
    BIND(PatternAddString, "FcPatternAddString");
    BIND(PatternAddBool, "FcPatternAddBool");
    BIND(ConfigSubstitute, "FcConfigSubstitute");
    BIND(DefaultSubstitute, "FcDefaultSubstitute");
    BIND(FontMatch, "FcFontMatch");
    BIND(ConfigDestroy, "FcConfigDestroy");
    BIND(Fini, "FcFini");
    BIND(PatternGetString, "FcPatternGetString");
    BIND(PatternGetBool, "FcPatternGetBool");
    BIND(PatternGetCharSet, "FcPatternGetCharSet");
#undef BIND
    return 1;
}

/* Best font covering cp. lang may be NULL. mono_only rejects colour fonts.
 * Copies the file path into out (empty when nothing suitable). */
static void fc_query(const fc_api *a, FcConfig *cfg, uint32_t cp, const char *lang,
                     int mono_only, char *out)
{
    out[0] = 0;
    FcPattern *pat = a->PatternCreate();
    FcCharSet *cs = a->CharSetCreate();
    if (!pat || !cs) goto done;
    a->CharSetAddChar(cs, cp);
    a->PatternAddCharSet(pat, "charset", cs);
    if (lang) a->PatternAddString(pat, "lang", (const FcChar8 *)lang);
    if (mono_only) a->PatternAddBool(pat, "color", 0);
    a->ConfigSubstitute(cfg, pat, FC_MATCH_PATTERN);
    a->DefaultSubstitute(pat);
    FcResult res = 0;
    FcPattern *m = a->FontMatch(cfg, pat, &res);
    if (m) {
        FcChar8 *file = NULL;
        FcCharSet *mcs = NULL;
        FcBool color = 0;
        int ok = a->PatternGetString(m, "file", 0, &file) == FC_RESULT_MATCH && file;
        ok = ok && a->PatternGetCharSet(m, "charset", 0, &mcs) == FC_RESULT_MATCH &&
             mcs && a->CharSetHasChar(mcs, cp);
        if (ok && mono_only && a->PatternGetBool(m, "color", 0, &color) == FC_RESULT_MATCH && color)
            ok = 0;
        if (ok && strlen((const char *)file) < FONT_FALLBACK_PATH_MAX)
            memcpy(out, file, strlen((const char *)file) + 1);
        a->PatternDestroy(m);
    }
done:
    if (cs) a->CharSetDestroy(cs);
    if (pat) a->PatternDestroy(pat);
}

void font_fallback_discover(font_fallback *fb, const char *soname)
{
    uint64_t t0 = now_ns();
    fb->cjk[0] = 0;
    fb->emoji[0] = 0;
    fb->have_fontconfig = 0;
    fb->worker = pthread_self();
    void *h = soname ? dlopen(soname, RTLD_NOW | RTLD_LOCAL) : NULL;
    fc_api api;
    if (h && fc_bind(&api, h)) {
        FcConfig *cfg = api.InitLoadConfigAndFonts();
        if (cfg) {
            fb->have_fontconfig = 1;
            fc_query(&api, cfg, 0x4E2Du, "zh", 0, fb->cjk);
            fc_query(&api, cfg, 0x1F600u, NULL, 1, fb->emoji);
            api.ConfigDestroy(cfg);   /* paths are copied out */
            api.Fini();
        }
    }
    /* handle intentionally kept open: nothing else uses fontconfig, and
     * dlclose of a library with live TLS/atexit hooks buys nothing. */
    fb->elapsed_ns = now_ns() - t0;
    atomic_store_explicit(&fb->done, 1u, memory_order_release);
}

void font_fallback_job(work_ctx *c)
{
    font_fallback *fb = (font_fallback *)c->arg;
    font_fallback_discover(fb, "libfontconfig.so.1");
    work_msg m;
    memset(&m, 0, sizeof m);
    m.kind = FONT_FALLBACK_MSG_KIND;
    m.generation = c->generation;
    (void)work_publish(c, &m);
}

unsigned char *font_load_file(const char *path, edit_arena *arena, size_t *len)
{
    FILE *fp = path && arena ? fopen(path, "rb") : NULL;
    if (!fp) return NULL;
    unsigned char *buf = NULL;
    long n = 0;
    if (fseek(fp, 0, SEEK_END) == 0) n = ftell(fp);
    if (n > 0 && fseek(fp, 0, SEEK_SET) == 0) {
        buf = edit_arena_alloc(arena, (size_t)n, 16);
        if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) buf = NULL;
    }
    fclose(fp);
    if (buf) *len = (size_t)n;
    return buf;
}
