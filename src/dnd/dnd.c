#include "dnd/dnd.h"
#include "utf8/utf8.h"

#include <string.h>

/* (P) Arithmetic constants below describe byte parsing: zero-based indexes,
 * one byte per step/NUL, hexadecimal radix sixteen and ten decimal digits.
 * The negative hex result denotes an invalid digit. */
static uint8_t ascii_lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t)(c + ('a' - 'A')) : c;
}

static bool ascii_equal(const uint8_t *bytes, size_t size, const char *text)
{
    if (!text || strlen(text) != size) return false;
    for (size_t i = 0; i < size; i++)
        if (ascii_lower(bytes[i]) != ascii_lower((uint8_t)text[i])) return false;
    return true;
}

static int hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = ascii_lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool decode_path(const uint8_t *line, size_t size, const char *hostname,
                        char path[IPC_PATH_CAP], size_t *length)
{
    bool uri = line[0] != '/'; /* Caller supplies a nonempty entry. */
    if (uri) {
        const size_t prefix = sizeof "file://" - 1u;
        if (size < prefix || !ascii_equal(line, prefix, "file://")) return false;
        const uint8_t *slash = memchr(line + prefix, '/', size - prefix);
        if (!slash) return false;
        size_t authority = (size_t)(slash - (line + prefix));
        if (authority && !ascii_equal(line + prefix, authority, "localhost") &&
            !ascii_equal(line + prefix, authority, hostname)) return false;
        size -= (size_t)(slash - line);
        line = slash;
    }

    size_t used = 0;
    for (size_t i = 0; i < size;) {
        uint8_t byte = line[i++];
        if (byte == '\r' || (uri && (byte == '?' || byte == '#'))) return false;
        if (uri && byte == '%') {
            /* (P) A percent escape has exactly two hexadecimal digits. */
            if (size - i < 2u) return false;
            int high = hex_digit(line[i]), low = hex_digit(line[i + 1u]);
            if (high < 0 || low < 0) return false;
            byte = (uint8_t)(high * 16 + low);
            i += 2u;
        }
        if (!byte || used == IPC_PATH_CAP - 1u) return false;
        path[used++] = (char)byte;
    }
    for (size_t i = 0; i < used;) {
        utf8_step unit = utf8_decode((const uint8_t *)path + i, used - i);
        if (!unit.valid) return false;
        i += unit.len;
    }
    path[used] = '\0';
    *length = used;
    return true;
}

dnd_result dnd_parse_uri_list(const uint8_t *data, size_t size,
                             const char *local_hostname, edit_arena *arena,
                             ipc_request *out, dnd_counts *counts)
{
    if (out) memset(out, 0, sizeof *out);
    if (counts) memset(counts, 0, sizeof *counts);
    if (!out || !counts || !arena || !arena->base || arena->used > arena->size ||
        (!data && size) || (size && memchr(data, '\0', size))) return dnd_invalid;

    edit_arena_mark_t mark = edit_arena_mark(arena);
    for (size_t at = 0; at < size;) {
        const uint8_t *line = data + at;
        const uint8_t *lf = memchr(line, '\n', size - at);
        size_t length = lf ? (size_t)(lf - line) : size - at;
        at += length;
        if (lf) {
            at++;
            if (length && line[length - 1u] == '\r') length--;
        }
        if (!length || line[0] == '#') continue;

        char path[IPC_PATH_CAP];
        size_t decoded = 0;
        if (!decode_path(line, length, local_hostname, path, &decoded)) {
            counts->skipped++;
            continue;
        }
        if (out->count == IPC_MAX_PATHS) {
            counts->skipped++;
            counts->overflow++;
            continue;
        }
        char *copy = edit_arena_alloc(arena, decoded + 1u, _Alignof(char));
        if (!copy) {
            edit_arena_reset_to_mark(arena, mark);
            memset(out, 0, sizeof *out);
            memset(counts, 0, sizeof *counts);
            return dnd_no_space;
        }
        memcpy(copy, path, decoded + 1u);
        /* (P) IPC coordinates are one-based; origin preserves retained views. */
        out->paths[out->count++] = (ipc_path){copy, 1u, 1u};
    }
    return dnd_ok;
}
