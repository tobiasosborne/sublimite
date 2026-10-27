#include "dnd/dnd.h"
#include "utf8/utf8.h"

#include <assert.h>
#include <string.h>

/* (P) All sizes derive from IPC bounds or the supplied input. Zero/one are
 * empty/unit counts and zero-based indexes; coordinates use IPC's one-based
 * origin. Percent encoding uses three bytes per input byte, two hex digits,
 * a four-bit shift and the low-nibble mask (sixteen possible hex digits). */
static void cleared(const ipc_request *request, const dnd_counts *counts)
{
    assert(request->count == 0 && request->cwd == NULL);
    assert(request->stdin_data == NULL && request->stdin_size == 0);
    assert(!request->wait && !request->has_stdin && !request->new_instance);
    for (size_t i = 0; i < IPC_MAX_PATHS; i++) {
        assert(request->paths[i].path == NULL);
        assert(request->paths[i].line == 0 && request->paths[i].col == 0);
    }
    assert(counts->skipped == 0 && counts->overflow == 0);
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

static void raw_bytes(const uint8_t *data, size_t size, edit_arena *arena)
{
    ipc_request request;
    dnd_counts counts;
    dnd_result result = dnd_parse_uri_list(data, size, "thinkpad", arena, &request, &counts);
    if (size && memchr(data, '\0', size)) {
        assert(result == dnd_invalid);
        cleared(&request, &counts);
        assert(arena->used == 0);
        return;
    }
    assert(result == dnd_ok);
    assert(request.count <= IPC_MAX_PATHS && counts.overflow <= counts.skipped);
    assert(request.count + counts.skipped == entries(data, size));
    assert(!counts.overflow || request.count == IPC_MAX_PATHS);
    assert(request.cwd == NULL && request.stdin_data == NULL && request.stdin_size == 0);
    assert(!request.wait && !request.has_stdin && !request.new_instance);
    size_t consumed = 0;
    for (size_t i = 0; i < request.count; i++) {
        const char *path = request.paths[i].path;
        assert((const unsigned char *)path == arena->base + consumed);
        const char *nul = memchr(path, '\0', arena->used - consumed);
        assert(nul != NULL);
        size_t length = (size_t)(nul - path);
        assert(length > 0 && length < IPC_PATH_CAP && path[0] == '/');
        assert(valid_utf8((const uint8_t *)path, length));
        assert(request.paths[i].line == 1u && request.paths[i].col == 1u);
        consumed += length + 1u;
    }
    assert(consumed == arena->used);
    for (size_t i = request.count; i < IPC_MAX_PATHS; i++)
        assert(request.paths[i].path == NULL);

    /* (P) A second parse gets a capacity selected by the input from the
     * closed range [empty, required storage]. It must be atomic on failure. */
    size_t required = arena->used;
    size_t capacity = size ? data[0] % (required + 1u) : 0;
    size_t original = arena->size;
    edit_arena_reset(arena);
    arena->size = capacity;
    result = dnd_parse_uri_list(data, size, "thinkpad", arena, &request, &counts);
    if (capacity < required) {
        assert(result == dnd_no_space);
        cleared(&request, &counts);
        assert(arena->used == 0);
    } else {
        assert(result == dnd_ok && arena->used == required);
    }
    arena->size = original;
}

static void encoded_bytes(const uint8_t *data, size_t size, edit_arena *arena)
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
    assert(dnd_parse_uri_list((const uint8_t *)uri, used, NULL,
                              arena, &request, &counts) == dnd_ok);
    assert(counts.overflow == 0);
    assert(request.count == (accept ? 1u : 0u));
    assert(counts.skipped == (accept ? 0u : 1u));
    if (accept) {
        const char *path = request.paths[0].path;
        assert(path[0] == '/' && strlen(path) == payload + 1u);
        assert(!payload || memcmp(path + 1u, data, payload) == 0);
    } else assert(arena->used == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    edit_arena arena;
    /* (P) IPC_MAX_PATHS retained entries, each including NUL in IPC_PATH_CAP. */
    if (edit_arena_init(&arena, IPC_MAX_PATHS * IPC_PATH_CAP)) return 0;
    raw_bytes(data, size, &arena);
    edit_arena_reset(&arena);
    encoded_bytes(data, size, &arena);
    edit_arena_free(&arena);
    return 0;
}
