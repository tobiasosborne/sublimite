#include "dnd/dnd.h"
#include "assert_death.h"

#include <stdio.h>
#include <string.h>

/* (P) Test plumbing: zero means success, one means failure; indexes start
 * at zero, sizeof(string)-one excludes its NUL, and increments visit each
 * entry once. Path coordinates are IPC's one-based origin. */
#define T(c) do { if (!(c)) { \
    fprintf(stderr, "dnd_test:%d: FAIL %s\n", __LINE__, #c); return 1; \
} } while (0)
#define BYTES(s) (const uint8_t *)(s), sizeof(s) - 1u

static int paths_equal(const ipc_request *request, const char *const *paths,
                       size_t count)
{
    T(request->count == count);
    T(request->cwd == NULL && request->stdin_data == NULL);
    T(request->stdin_size == 0 && !request->wait && !request->has_stdin &&
      !request->new_instance);
    T(request->activation_token == NULL && request->startup_id == NULL);
    for (size_t i = 0; i < count; i++) {
        T(request->paths[i].path != NULL);
        T(strcmp(request->paths[i].path, paths[i]) == 0);
        T(request->paths[i].line == 1u && request->paths[i].col == 1u);
    }
    for (size_t i = count; i < IPC_MAX_PATHS; i++) {
        T(request->paths[i].path == NULL);
        T(request->paths[i].line == 0 && request->paths[i].col == 0);
    }
    return 0;
}

static int lists(base_arena *arena)
{
    const char crlf[] = "# Nemo list\r\n\r\nfile:///tmp/one\r\n"
                        "file:///tmp/two\r\n";
    const char lf[] = "\n# ignored\nfile:///tmp/one\n\nfile:///tmp/two";
    const char *const want[] = {"/tmp/one", "/tmp/two"};
    ipc_request request;
    dnd_counts counts;
    T(dnd_parse_uri_list(BYTES(crlf), "thinkpad", arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
    T(counts.skipped == 0 && counts.overflow == 0);
    T(dnd_parse_uri_list(BYTES(lf), "thinkpad", arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
    T(counts.skipped == 0 && counts.overflow == 0);
    const char empty[] = "# no paths\r\n\r\n\n";
    T(dnd_parse_uri_list(BYTES(empty), NULL, arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, NULL, 0) == 0);
    T(counts.skipped == 0 && counts.overflow == 0);
    T(dnd_parse_uri_list(NULL, 0, NULL, arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, NULL, 0) == 0);
    return 0;
}

static int hosts(base_arena *arena)
{
    const char *const accepted[] = {"file:///p", "file://localhost/p",
        "file://thinkpad/p", "FILE://THINKPAD/p", "file://LOCALHOST/p"};
    const char *const rejected[] = {"file://foreign/p", "file://thinkpad.evil/p",
        "file://think/p", "file://user@thinkpad/p", "file://thinkpad:80/p",
        "file://localhost", "https:///p", "file:/p", "file:relative", "relative"};
    /* (P) :80 is a syntactic port fixture; local file authorities have no port. */
    const char *const want[] = {"/p"};
    ipc_request request;
    dnd_counts counts;
    for (size_t i = 0; i < sizeof accepted / sizeof accepted[0]; i++) {
        T(dnd_parse_uri_list((const uint8_t *)accepted[i], strlen(accepted[i]),
                            "thinkpad", arena, &request, &counts) == DND_OK);
        T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
        T(counts.skipped == 0 && counts.overflow == 0);
    }
    for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; i++) {
        T(dnd_parse_uri_list((const uint8_t *)rejected[i], strlen(rejected[i]),
                            "thinkpad", arena, &request, &counts) == DND_OK);
        T(paths_equal(&request, NULL, 0) == 0);
        T(counts.skipped == 1u && counts.overflow == 0); /* (P) rejected fixture */
    }
    T(dnd_parse_uri_list(BYTES("file://thinkpad/p"), NULL,
                        arena, &request, &counts) == DND_OK);
    T(request.count == 0 && counts.skipped == 1u); /* (P) unknown local hostname */
    return 0;
}

static int decoding(base_arena *arena)
{
    /* (P) %20 is the space byte; C3 A9 is UTF-8 U+00E9; mixed hex case
     * encodes the same bytes. Bare paths preserve literal percent, ? and #. */
    const char input[] = "file:///tmp/caf%C3%A9%20notes\n"
        "file://localhost/tmp/caf%c3%a9\nfile:///tmp/caf\xc3\xa9\n"
        "file:///tmp/a%23b%3Fc%25d%2Be\n/tmp/100% ready?#\n/tmp/directory/";
    const char *const want[] = {"/tmp/caf\xc3\xa9 notes", "/tmp/caf\xc3\xa9",
        "/tmp/caf\xc3\xa9", "/tmp/a#b?c%d+e", "/tmp/100% ready?#", "/tmp/directory/"};
    ipc_request request;
    dnd_counts counts;
    base_malloc_guard_begin();
    dnd_result result = dnd_parse_uri_list(BYTES(input), NULL, arena, &request, &counts);
    size_t mallocs = base_malloc_guard_end();
    T(result == DND_OK && mallocs == 0);
    T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
    T(counts.skipped == 0 && counts.overflow == 0);
    return 0;
}

static int invalid_entries(base_arena *arena)
{
    const char *const bad[] = {"file:///tmp/%00", "file:///tmp/%", "file:///tmp/%A",
        "file:///tmp/%GG", "file:///tmp/%0g", "file:///tmp/?query", "file:///tmp/#fragment",
        "file:///tmp/a\rb", "/tmp/a\rb", "file:///tmp/last\r",
        "file:///tmp/%C0%AF", "file:///tmp/%ED%A0%80", "file:///tmp/%FF",
        "file:///tmp/\xc3", "/tmp/\x80"};
    /* (P) C0 AF is overlong UTF-8, ED A0 80 is a surrogate, FF and a lone
     * continuation/lead are invalid scalars. They cannot become UTF-8 paths. */
    ipc_request request;
    dnd_counts counts;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        base_arena_mark_t mark = base_arena_mark(arena);
        T(dnd_parse_uri_list((const uint8_t *)bad[i], strlen(bad[i]), NULL,
                            arena, &request, &counts) == DND_OK);
        T(paths_equal(&request, NULL, 0) == 0);
        T(counts.skipped == 1u && counts.overflow == 0); /* (P) one bad entry */
        T(base_arena_mark(arena) == mark);
    }
    const char mixed[] = "file:///good\nfile:///bad%00\nfile:///also-good\n";
    const char *const want[] = {"/good", "/also-good"};
    T(dnd_parse_uri_list(BYTES(mixed), NULL, arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
    T(counts.skipped == 1u && counts.overflow == 0); /* (P) the %00 line only */
    return 0;
}

static int entry_limit(base_arena *arena)
{
    enum { extra = 2 }; /* (P) acceptance: 130 entries = IPC_MAX_PATHS + 2. */
    const char line[] = "/p\n";
    char input[(IPC_MAX_PATHS + extra) * (sizeof line - 1u)];
    for (size_t i = 0; i < sizeof input; i += sizeof line - 1u)
        memcpy(input + i, line, sizeof line - 1u);
    ipc_request request;
    dnd_counts counts;
    T(dnd_parse_uri_list((const uint8_t *)input, sizeof input, NULL,
                        arena, &request, &counts) == DND_OK);
    T(request.count == IPC_MAX_PATHS);
    T(counts.skipped == extra && counts.overflow == extra);
    for (size_t i = 0; i < request.count; i++) T(strcmp(request.paths[i].path, "/p") == 0);
    T(arena->used == IPC_MAX_PATHS * sizeof "/p"); /* (P) only kept strings + NUL */
    return 0;
}

static int path_limit(base_arena *arena)
{
    enum { long_path = 5000 }; /* (P) acceptance's oversized path byte length. */
    char input[long_path + sizeof "\n/p"];
    memset(input, 'a', long_path);
    input[0] = '/';
    memcpy(input + long_path, "\n/p", sizeof "\n/p");
    ipc_request request;
    dnd_counts counts;
    const char *const want[] = {"/p"};
    T(dnd_parse_uri_list(BYTES(input), NULL, arena, &request, &counts) == DND_OK);
    T(paths_equal(&request, want, sizeof want / sizeof want[0]) == 0);
    T(counts.skipped == 1u && counts.overflow == 0); /* (P) oversized entry only */

    char boundary[IPC_PATH_CAP + 1u]; /* (P) cap bytes plus the fixture NUL. */
    memset(boundary, 'a', sizeof boundary);
    boundary[0] = '/';
    boundary[IPC_PATH_CAP - 1u] = '\0'; /* (P) largest path fits cap with its NUL. */
    T(dnd_parse_uri_list((const uint8_t *)boundary, IPC_PATH_CAP - 1u, NULL,
                        arena, &request, &counts) == DND_OK);
    T(request.count == 1u && counts.skipped == 0);
    T(strlen(request.paths[0].path) == IPC_PATH_CAP - 1u);
    boundary[IPC_PATH_CAP - 1u] = 'a';
    boundary[IPC_PATH_CAP] = '\0';
    T(dnd_parse_uri_list((const uint8_t *)boundary, IPC_PATH_CAP, NULL,
                        arena, &request, &counts) == DND_OK);
    T(request.count == 0 && counts.skipped == 1u);

    /* (P) Every decoded 'a' occupies three URI bytes (%61). The decoded
     * slash plus cap-minus-two 'a' bytes fits, despite the larger URI. */
    char encoded[sizeof "file:///" - 1u + (IPC_PATH_CAP - 2u) * (sizeof "%61" - 1u)];
    memcpy(encoded, "file:///", sizeof "file:///" - 1u);
    for (size_t i = sizeof "file:///" - 1u; i < sizeof encoded; i += sizeof "%61" - 1u)
        memcpy(encoded + i, "%61", sizeof "%61" - 1u);
    T(dnd_parse_uri_list((const uint8_t *)encoded, sizeof encoded, NULL,
                        arena, &request, &counts) == DND_OK);
    T(request.count == 1u && counts.skipped == 0);
    T(strlen(request.paths[0].path) == IPC_PATH_CAP - 1u);
    return 0;
}

static int storage_and_errors(base_arena *arena)
{
    const char input[] = "/one\n/two";
    char *prefix = base_arena_alloc(arena, sizeof "prefix", _Alignof(char));
    T(prefix != NULL);
    memcpy(prefix, "prefix", sizeof "prefix");
    base_arena_mark_t mark = base_arena_mark(arena);
    size_t capacity = arena->size;
    arena->size = mark + sizeof "/one"; /* (P) first path fits exactly; next cannot. */
    ipc_request request;
    dnd_counts counts;
    T(dnd_parse_uri_list(BYTES(input), NULL, arena, &request, &counts) == DND_NO_SPACE);
    T(paths_equal(&request, NULL, 0) == 0);
    T(counts.skipped == 0 && counts.overflow == 0);
    T(arena->used == mark && strcmp(prefix, "prefix") == 0);
    arena->size = capacity;
    char mutable[] = "/one";
    T(dnd_parse_uri_list(BYTES(mutable), NULL, arena, &request, &counts) == DND_OK);
    mutable[0] = '?';
    T(strcmp(request.paths[0].path, "/one") == 0); /* (P) output owns a copy. */

    mark = base_arena_mark(arena);
    const char nul[] = "/good\n/bad\0tail";
    T(dnd_parse_uri_list(BYTES(nul), NULL, arena, &request, &counts) == DND_INVALID);
    T(paths_equal(&request, NULL, 0) == 0);
    T(counts.skipped == 0 && counts.overflow == 0 && arena->used == mark);
    return 0;
}

/* A missing required object, an uninitialized arena and data=NULL with a
 * nonzero size are caller bugs, not drag payloads (A15): each dies by the
 * always-on ASSERT in a forked child. In the child one IPC_PATH_CAP arena
 * (P: one maximum path including its NUL) is more than the call reaches. */
static void null_out(void)
{
    base_arena arena;
    if (base_arena_init(&arena, IPC_PATH_CAP) != 0) return;
    dnd_counts counts;
    (void)dnd_parse_uri_list(BYTES("/one"), NULL, &arena, NULL, &counts);
}
static void null_counts(void)
{
    base_arena arena;
    if (base_arena_init(&arena, IPC_PATH_CAP) != 0) return;
    ipc_request request;
    (void)dnd_parse_uri_list(BYTES("/one"), NULL, &arena, &request, NULL);
}
static void null_arena(void)
{
    ipc_request request;
    dnd_counts counts;
    (void)dnd_parse_uri_list(BYTES("/one"), NULL, NULL, &request, &counts);
}
static void uninitialized_arena(void)
{
    base_arena arena = {0};
    ipc_request request;
    dnd_counts counts;
    (void)dnd_parse_uri_list(BYTES("/one"), NULL, &arena, &request, &counts);
}
static void null_data(void)
{
    base_arena arena;
    if (base_arena_init(&arena, IPC_PATH_CAP) != 0) return;
    ipc_request request;
    dnd_counts counts;
    (void)dnd_parse_uri_list(NULL, sizeof "/one", NULL, &arena, &request, &counts);
}

static int precondition_deaths(void)
{
    T(assert_death(null_out, "src/dnd/dnd.c",
                   "out != NULL && counts != NULL && arena != NULL"));
    T(assert_death(null_counts, "src/dnd/dnd.c",
                   "out != NULL && counts != NULL && arena != NULL"));
    T(assert_death(null_arena, "src/dnd/dnd.c",
                   "out != NULL && counts != NULL && arena != NULL"));
    T(assert_death(uninitialized_arena, "src/dnd/dnd.c",
                   "arena->base != NULL && arena->used <= arena->size"));
    T(assert_death(null_data, "src/dnd/dnd.c", "data != NULL || size == 0"));
    return 0;
}

int main(void)
{
    /* Before anything else: the death children fork a one-thread process. */
    if (precondition_deaths()) return 1;
    /* (P) Every kept path needs at most IPC_PATH_CAP bytes including NUL;
     * the IPC path count bounds storage without a new parser budget. */
    base_arena arena;
    T(base_arena_init(&arena, IPC_MAX_PATHS * IPC_PATH_CAP) == 0);
    int (*const cases[])(base_arena *) = {lists, hosts, decoding, invalid_entries,
                                        entry_limit, path_limit, storage_and_errors};
    int result = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        base_arena_reset_to_mark(&arena, 0);
        if (cases[i](&arena)) { result = 1; break; }
    }
    base_arena_free(&arena);
    if (!result) puts("dnd_test: all parser cases passed");
    return result;
}
