#include "dnd/dnd.h"
#include "utf8/utf8.h"

#include <stdio.h>
#include <string.h>

/* (P) All sizes derive from IPC bounds or the supplied input. Zero/one are
 * empty/unit counts and zero-based indexes; coordinates use IPC's one-based
 * origin. Percent encoding uses three bytes per input byte, two hex digits,
 * a four-bit shift and the low-nibble mask (sixteen possible hex digits). */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "dnd_fuzz: %s:%d: %s\n", __FILE__, __LINE__, #c); \
    __builtin_trap(); } } while (0)

static void cleared(const ipc_request *request, const dnd_counts *counts)
{
    CHECK(request->count == 0 && request->cwd == NULL);
    CHECK(request->stdin_data == NULL && request->stdin_size == 0);
    CHECK(!request->wait && !request->has_stdin && !request->new_instance);
    CHECK(request->activation_token == NULL && request->startup_id == NULL);
    for (size_t i = 0; i < IPC_MAX_PATHS; i++) {
        CHECK(request->paths[i].path == NULL);
        CHECK(request->paths[i].line == 0 && request->paths[i].col == 0);
    }
    CHECK(counts->skipped == 0 && counts->overflow == 0);
}

static size_t entries(const uint8_t *data, size_t size)
{
    size_t count = 0, start = 0;
    for (size_t end = 0; end <= size; end++) {
        if (end < size && data[end] != '\n') continue;
        size_t length = end - start;
        if (end < size && length && data[end - 1u] == '\r') length--;
        if (length && data[start] != '#') count++;
        start = end + 1u;
    }
    return count;
}

static bool valid_utf8(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size;) {
        utf8_step unit = utf8_decode(data + i, size - i);
        if (!unit.valid) return false;
        i += unit.len;
    }
    return true;
}

static void raw_bytes(const uint8_t *data, size_t size, base_arena *arena)
{
    ipc_request request;
    dnd_counts counts;
    dnd_result result = dnd_parse_uri_list(data, size, "thinkpad", arena, &request, &counts);
    if (size && memchr(data, '\0', size)) {
        CHECK(result == DND_INVALID);
        cleared(&request, &counts);
        CHECK(arena->used == 0);
        return;
    }
    CHECK(result == DND_OK);
    CHECK(request.count <= IPC_MAX_PATHS && counts.overflow <= counts.skipped);
    CHECK(request.count + counts.skipped == entries(data, size));
    CHECK(!counts.overflow || request.count == IPC_MAX_PATHS);
    CHECK(request.cwd == NULL && request.stdin_data == NULL && request.stdin_size == 0);
    CHECK(!request.wait && !request.has_stdin && !request.new_instance);
    CHECK(request.activation_token == NULL && request.startup_id == NULL);
    size_t consumed = 0;
    for (size_t i = 0; i < request.count; i++) {
        const char *path = request.paths[i].path;
        CHECK((const unsigned char *)path == arena->base + consumed);
        const char *nul = memchr(path, '\0', arena->used - consumed);
        CHECK(nul != NULL);
        size_t length = (size_t)(nul - path);
        CHECK(length > 0 && length < IPC_PATH_CAP && path[0] == '/');
        CHECK(valid_utf8((const uint8_t *)path, length));
        CHECK(request.paths[i].line == 1u && request.paths[i].col == 1u);
        consumed += length + 1u;
    }
    CHECK(consumed == arena->used);
    for (size_t i = request.count; i < IPC_MAX_PATHS; i++)
        CHECK(request.paths[i].path == NULL);

    /* (P) A second parse gets a capacity selected by the input from the
     * closed range [empty, required storage]. It must be atomic on failure. */
    size_t required = arena->used;
    size_t capacity = size ? data[0] % (required + 1u) : 0;
    size_t original = arena->size;
    base_arena_reset_to_mark(arena, 0);
    arena->size = capacity;
    result = dnd_parse_uri_list(data, size, "thinkpad", arena, &request, &counts);
    if (capacity < required) {
        CHECK(result == DND_NO_SPACE);
        cleared(&request, &counts);
        CHECK(arena->used == 0);
    } else {
        CHECK(result == DND_OK && arena->used == required);
    }
    arena->size = original;
}

static void encoded_bytes(const uint8_t *data, size_t size, base_arena *arena)
{
    const char hex[] = "0123456789abcdef";
    const char prefix[] = "file://localhost/";
    char uri[sizeof prefix - 1u + IPC_PATH_CAP * 3u];
    size_t payload = size < IPC_PATH_CAP ? size : IPC_PATH_CAP;
    size_t used = sizeof prefix - 1u;
    memcpy(uri, prefix, used);
    for (size_t i = 0; i < payload; i++) {
        uri[used++] = '%';
        uri[used++] = hex[data[i] >> 4];
        uri[used++] = hex[data[i] & 15u];
    }
    /* (P) The slash and trailing NUL consume two bytes of IPC_PATH_CAP. */
    bool accept = payload <= IPC_PATH_CAP - 2u &&
        (!payload || !memchr(data, '\0', payload)) && valid_utf8(data, payload);
    ipc_request request;
    dnd_counts counts;
    CHECK(dnd_parse_uri_list((const uint8_t *)uri, used, NULL,
                             arena, &request, &counts) == DND_OK);
    CHECK(counts.overflow == 0);
    CHECK(request.count == (accept ? 1u : 0u));
    CHECK(counts.skipped == (accept ? 0u : 1u));
    if (accept) {
        const char *path = request.paths[0].path;
        CHECK(path[0] == '/' && strlen(path) == payload + 1u);
        CHECK(!payload || memcmp(path + 1u, data, payload) == 0);
    } else CHECK(arena->used == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    base_arena arena;
    /* (P) IPC_MAX_PATHS retained entries, each including NUL in IPC_PATH_CAP. */
    if (base_arena_init(&arena, IPC_MAX_PATHS * IPC_PATH_CAP)) return 0;
    raw_bytes(data, size, &arena);
    base_arena_reset_to_mark(&arena, 0);
    encoded_bytes(data, size, &arena);
    base_arena_free(&arena);
    return 0;
}
