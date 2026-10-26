#if !defined(_WIN32)
/* Fontconfig has no cancellation API. Keep its opaque work out of WORK_BULK:
 * posix_spawn executes the installed fc-match; the worker streams bounded
 * chunks, polls, and kills/reaps the child on cancellation. No fork-time libc
 * calls in a multithreaded child, shell, or unbounded output buffer. */
#include "font/discovery.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct query_output {
    char path[FONT_FALLBACK_PATH_MAX], index[24], color[16], token[32];
    size_t used;
    unsigned line;
    uint32_t cp;
    int covered, invalid;
} query_output;

static uint64_t discovery_time(void)
{
    return os_now_ns();
}
static int discovery_stop(font_fallback *owner, work_ctx *ctx)
{
    atomic_fetch_add_explicit(&owner->cancellation_polls, 1u, memory_order_relaxed);
    return work_should_stop(ctx);
}
static void charset_token(query_output *out)
{
    if (!out->used) return;
    out->token[out->used] = 0;
    char *end = NULL; errno = 0;
    unsigned long lo = strtoul(out->token, &end, 16), hi = lo;
    if (end == out->token || errno) { out->invalid = 1; return; }
    if (*end == '-') {
        char *last = NULL; errno = 0; hi = strtoul(end + 1, &last, 16);
        if (last == end + 1 || errno) { out->invalid = 1; return; }
        end = last;
    }
    if (*end || hi < lo || hi > 0x10ffffu) out->invalid = 1;
    else if (lo <= out->cp && out->cp <= hi) out->covered = 1;
    out->used = 0;
}
static void query_feed(query_output *out, const char *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = bytes[i];
        if (out->line == 3 && (c == ' ' || c == '\n')) charset_token(out);
        if (c == '\n') {
            out->line++; out->used = 0; continue;
        }
        if (out->line > 3) { out->invalid = 1; continue; }
        if (out->line == 3 && c == ' ') continue;
        char *dst = out->line == 0 ? out->path : out->line == 1 ? out->index : out->line == 2 ? out->color : out->token;
        size_t cap = out->line == 0 ? sizeof out->path : out->line == 1 ? sizeof out->index : out->line == 2 ? sizeof out->color : sizeof out->token;
        if (c == 0 || out->used + 1u >= cap) { out->invalid = 1; continue; }
        dst[out->used++] = c; dst[out->used] = 0;
    }
}
static int above_stdio(int fd)
{
    if (fd >= 3) return fd;
    int copy = fcntl(fd, F_DUPFD_CLOEXEC, 3); close(fd); return copy;
}
static int query_child(font_fallback *owner, work_ctx *ctx, uint32_t cp,
                       const char *pattern, int mono_only, char *path,
                       uint32_t *index, int *available)
{
    if (discovery_stop(owner, ctx)) return 0;
    int fds[2]; if (pipe2(fds, O_CLOEXEC) != 0) return 1;
    fds[0] = above_stdio(fds[0]); fds[1] = above_stdio(fds[1]);
    if (fds[0] < 0 || fds[1] < 0) { if (fds[0] >= 0) close(fds[0]); if (fds[1] >= 0) close(fds[1]); return 1; }
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) < 0) { close(fds[0]); close(fds[1]); return 1; }
    posix_spawn_file_actions_t actions;
    /* edit-r1l / edit-cx8: libc's opaque spawn actions allocate on this
     * worker, concurrently with the process-wide UI typing guard. Exempt
     * only library spawn setup/cleanup, not our query parsing or glyph work. */
    edit_malloc_guard_exempt_begin();
    if (posix_spawn_file_actions_init(&actions) != 0) {
        edit_malloc_guard_exempt_end();
        close(fds[0]); close(fds[1]); return 1;
    }
    int error = posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[0]);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[1]);
    if (!error) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    const char *program = owner->discovery_program ? owner->discovery_program : "/usr/bin/fc-match";
    char *const args[] = {(char *)program, "-f", "%{file}\n%{index}\n%{color}\n%{charset}\n", (char *)pattern, NULL};
    posix_spawnattr_t attrs;
    int have_attrs = posix_spawnattr_init(&attrs) == 0;
    if (!have_attrs && !error) error = EINVAL;
    if (!error) error = posix_spawnattr_setpgroup(&attrs, 0);
    if (!error) error = posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP);
    pid_t pid = 0;
    if (!error) error = posix_spawn(&pid, program, &actions, &attrs, args, environ);
    if (have_attrs) (void)posix_spawnattr_destroy(&attrs);
    (void)posix_spawn_file_actions_destroy(&actions);
    edit_malloc_guard_exempt_end();
    close(fds[1]);
    if (error) { close(fds[0]); return 1; }
    atomic_store_explicit(&owner->child_pid, (uint32_t)pid, memory_order_release);
    query_output output = {0}; output.cp = cp;
    int cancelled = 0, killed = 0, status = 0, eof = 0, reaped = 0;
    uint64_t start = discovery_time();
    while (!reaped || !eof) {
        if (discovery_stop(owner, ctx)) cancelled = 1;
        if ((cancelled || output.invalid || discovery_time() - start >= UINT64_C(30000000000)) && !killed) {
            /* The direct child may already be reaped while descendants hold
             * stdout. Pipe lifetime and process-group cleanup are independent. */
            (void)kill(-pid, SIGKILL); killed = 1;
            close(fds[0]); fds[0] = -1; eof = 1;
        }
        if (!eof) {
            char bytes[512];
            ssize_t n = read(fds[0], bytes, sizeof bytes);
            if (n > 0) query_feed(&output, bytes, (size_t)n);
            else if (n == 0) eof = 1;
            else if (errno != EAGAIN && errno != EINTR) { output.invalid = 1; eof = 1; }
        }
        if (!reaped) {
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) reaped = 1;
            else if (result < 0 && errno != EINTR) { reaped = 1; output.invalid = 1; }
        }
        if (!reaped || !eof) {
            struct pollfd fd = {fds[0], POLLIN, 0};
            if (!eof) (void)poll(&fd, 1, 1);
            else { os_sleep_ns(UINT64_C(1000000)); }
        }
    }
    if (fds[0] >= 0) close(fds[0]);
    atomic_store_explicit(&owner->child_pid, 0u, memory_order_release);
    if (cancelled || discovery_stop(owner, ctx)) return 0;
    if (!killed && !output.invalid && WIFEXITED(status) && WEXITSTATUS(status) == 0 && output.line == 4) *available = 1;
    char *end = NULL; errno = 0; unsigned long face = strtoul(output.index, &end, 10);
    if (!killed && !output.invalid && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        output.line == 4 && output.path[0] == '/' && output.covered &&
        end != output.index && !*end && !errno && face <= 0xffffu &&
        (!mono_only || strcmp(output.color, "False") == 0)) {
        memcpy(path, output.path, sizeof output.path); *index = (uint32_t)face;
    }
    return 1;
}
int font_fallback_isolated(font_fallback *owner, font_fallback_result *out, work_ctx *ctx)
{
    memset(out, 0, sizeof *out); uint64_t start = discovery_time();
    out->worker = os_thread_self();
    if (!query_child(owner, ctx, 0x4e2d, ":charset=4e2d:lang=zh-cn", 0,
                     out->cjk, &out->cjk_index, &out->have_fontconfig)) return 0;
    if (!query_child(owner, ctx, 0x1f600, ":charset=1f600:color=false", 1,
                     out->emoji, &out->emoji_index, &out->have_fontconfig)) return 0;
    out->elapsed_ns = discovery_time() - start;
    return 1;
}

int font_fallback_codepoint(font_fallback *owner, work_ctx *ctx, uint32_t cp,
                            char *path, uint32_t *index)
{
    /* Pattern buffer derives from the literal prefix/suffix plus the maximum
     * hexadecimal scalar representation (sizeof uint32_t * two digits). */
    char pattern[sizeof ":charset=:color=false" + sizeof cp * 2u];
    int n = snprintf(pattern, sizeof pattern, ":charset=%x:color=false", cp);
    if (n < 0 || (size_t)n >= sizeof pattern) return 0;
    path[0] = 0; *index = 0;
    int available = 0;
    return query_child(owner, ctx, cp, pattern, 1, path, index, &available);
}

#include "font/selection.h"
#include <strings.h>
#include <dlfcn.h>
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
    FcPattern *(*NameParse)(const FcChar8 *);
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
    FcResult   (*PatternGetInteger)(const FcPattern *, const char *, int, int *);
    FcResult   (*PatternGetString)(const FcPattern *, const char *, int, FcChar8 **);
    FcResult   (*PatternGetBool)(const FcPattern *, const char *, int, FcBool *);
    FcResult   (*PatternGetCharSet)(const FcPattern *, const char *, int, FcCharSet **);
} fc_api;

static uint64_t now_ns(void)
{
    return os_now_ns();
}

static int fc_bind(fc_api *a, void *h)
{
#define BIND(field, sym) do { *(void **)(&a->field) = dlsym(h, sym); if (!a->field) return 0; } while (0)
    BIND(InitLoadConfigAndFonts, "FcInitLoadConfigAndFonts");
    BIND(PatternCreate, "FcPatternCreate");
    BIND(NameParse, "FcNameParse");
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
    BIND(PatternGetInteger, "FcPatternGetInteger");
    BIND(PatternGetString, "FcPatternGetString");
    BIND(PatternGetBool, "FcPatternGetBool");
    BIND(PatternGetCharSet, "FcPatternGetCharSet");
#undef BIND
    return 1;
}

/* Best font covering cp. lang may be NULL. mono_only rejects colour fonts.
 * Copies the file path into out (empty when nothing suitable). */
static void fc_query(const fc_api *a, FcConfig *cfg, uint32_t cp, const char *lang,
                     int mono_only, char *out, uint32_t *index)
{
    out[0] = 0;
    *index = 0;
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
        int face = 0;
        int ok = a->PatternGetString(m, "file", 0, &file) == FC_RESULT_MATCH && file;
        /* FC_INDEX: face within a TTC. High 16 bits select a named variation
         * instance, which stb cannot render: reject those. */
        ok = ok && a->PatternGetInteger(m, "index", 0, &face) == FC_RESULT_MATCH &&
             face >= 0 && face <= 0xffff;
        ok = ok && a->PatternGetCharSet(m, "charset", 0, &mcs) == FC_RESULT_MATCH &&
             mcs && a->CharSetHasChar(mcs, cp);
        if (ok && mono_only && a->PatternGetBool(m, "color", 0, &color) == FC_RESULT_MATCH && color)
            ok = 0;
        if (ok && strlen((const char *)file) < FONT_FALLBACK_PATH_MAX) {
            memcpy(out, file, strlen((const char *)file) + 1);
            *index = (uint32_t)face;
        }
        a->PatternDestroy(m);
    }
done:
    if (cs) a->CharSetDestroy(cs);
    if (pat) a->PatternDestroy(pat);
}

void font_fallback_discover(font_fallback *fb, const char *soname)
{
    uint64_t t0 = now_ns();
    font_fallback_reset(fb);
    fb->worker = os_thread_self();
    void *h = soname ? dlopen(soname, RTLD_NOW | RTLD_LOCAL) : NULL;
    fc_api api;
    if (h && fc_bind(&api, h)) {
        FcConfig *cfg = api.InitLoadConfigAndFonts();
        if (cfg) {
            fb->have_fontconfig = 1;
            fc_query(&api, cfg, 0x4E2Du, "zh-cn", 0, fb->cjk, &fb->cjk_index);
            fc_query(&api, cfg, 0x1F600u, NULL, 1, fb->emoji, &fb->emoji_index);
            api.ConfigDestroy(cfg);   /* paths are copied out */
            api.Fini();
        }
    }
    /* handle intentionally kept open: nothing else uses fontconfig, and
     * dlclose of a library with live TLS/atexit hooks buys nothing. */
    fb->elapsed_ns = now_ns() - t0;
    atomic_store_explicit(&fb->done, 1u, memory_order_release);
}

/* Worker-only primary selection; use the same runtime Fontconfig API. */
int font_selection_resolve(const char *pattern, char *file, size_t cap, uint32_t *index)
{
    if (!pattern || !file || !cap || !index) return FONT_ERR_ARG;
    file[0] = 0; *index = 0;
    void *h = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
    fc_api api;
    if (!h || !fc_bind(&api, h)) return FONT_ERR_MISSING;
    FcConfig *cfg = api.InitLoadConfigAndFonts();
    FcPattern *requested = api.NameParse((const FcChar8 *)pattern);
    FcPattern *query = api.NameParse((const FcChar8 *)pattern);
    FcPattern *match = NULL;
    int result = FONT_ERR_MISSING;
    if (!cfg || !requested || !query) goto done;
    if (!api.ConfigSubstitute(cfg, query, FC_MATCH_PATTERN)) goto done;
    api.DefaultSubstitute(query);
    FcResult status = FC_RESULT_MATCH;
    match = api.FontMatch(cfg, query, &status);
    if (!match || status != FC_RESULT_MATCH) goto done;
    int family_ok = 0;
    for (int i = 0;; i++) {
        FcChar8 *want = NULL;
        if (api.PatternGetString(requested, "family", i, &want) != FC_RESULT_MATCH) break;
        if (!strcasecmp((const char *)want, "monospace")) family_ok = 1;
        for (int j = 0;; j++) {
            FcChar8 *got = NULL;
            if (api.PatternGetString(match, "family", j, &got) != FC_RESULT_MATCH) break;
            if (!strcasecmp((const char *)want, (const char *)got)) family_ok = 1;
        }
    }
    FcChar8 *path = NULL; int face = 0;
    if (family_ok && api.PatternGetString(match, "file", 0, &path) == FC_RESULT_MATCH && path &&
        api.PatternGetInteger(match, "index", 0, &face) == FC_RESULT_MATCH && face >= 0 && face <= UINT16_MAX &&
        strlen((const char *)path) < cap) {
        memcpy(file, path, strlen((const char *)path) + 1u); *index = (uint32_t)face; result = FONT_OK;
    }
done:
    if (match) api.PatternDestroy(match);
    if (query) api.PatternDestroy(query);
    if (requested) api.PatternDestroy(requested);
    if (cfg) api.ConfigDestroy(cfg);
    api.Fini();
    return result;
}

#endif
