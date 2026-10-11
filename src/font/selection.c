#include "font/selection.h"
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

int font_selection_parse(font_selection *s, const char *family, const char *file)
{
    if (!s) return FONT_ERR_ARG;
    if (!family) family = "";
    if (!file) file = "";
    if (strlen(family) >= sizeof s->family || strlen(file) >= sizeof s->file) return FONT_ERR_ARG;
    uint32_t index = 0;
    const char *hash = strrchr(file, '#');
    if (hash) {
        if (hash == file || !hash[1]) return FONT_ERR_ARG;
        for (const char *p = hash + 1; *p; p++) {
            /* TTC index occupies fontconfig's low half of its index field. */
            if (*p < '0' || *p > '9' || index > (UINT16_MAX - (uint32_t)(*p - '0')) / 10u) return FONT_ERR_ARG;
            index = index * 10u + (uint32_t)(*p - '0');
        }
    }
    memcpy(s->family, family, strlen(family) + 1u);
    size_t n = hash ? (size_t)(hash - file) : strlen(file);
    memcpy(s->file, file, n); s->file[n] = 0; s->index = index;
    return FONT_OK;
}
void font_selection_free(font_selection *s)
{
    edit_arena_free(&s->files);
    memset(&s->face, 0, sizeof s->face); /* no descriptor may retain released font bytes */
}
int font_selection_load(font_selection *s, uint32_t px, bool em,
                        font_cell max_cell, work_ctx *ctx)
{
    font_selection_free(s);
    if (!s->file[0] && !s->family[0]) return FONT_OK;
    char path[FONT_FALLBACK_PATH_MAX]; uint32_t index = s->index;
    memcpy(path, s->file, sizeof path);
    if (!path[0]) {
        int rc = font_selection_resolve(s->family, path, sizeof path, &index);
        if (rc) return rc;
        /* The version retains the resolved primary for zoom rebuilds. A new
         * family generation still receives the original copied request. */
        memcpy(s->file, path, sizeof s->file); s->index = index;
    }
    if (work_should_stop(ctx)) return FONT_MORE;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return FONT_ERR_INIT;
    struct stat sb;
    if (fstat(fd, &sb) || !S_ISREG(sb.st_mode) || sb.st_size <= 0 ||
        (uintmax_t)sb.st_size > SIZE_MAX) { close(fd); return FONT_ERR_INIT; }
    size_t len = (size_t)sb.st_size;
    if (edit_arena_init(&s->files, len)) { close(fd); return FONT_ERR_NOMEM; }
    unsigned char *bytes = edit_arena_alloc(&s->files, len, 1);
    int rc = bytes ? FONT_OK : FONT_ERR_NOMEM;
    size_t used = 0;
    /* Read directly into the version's arena: stdio's stream/buffer heap
     * allocations would otherwise recur on every zoom from a custom face. */
    while (!rc && used < len) {
        if (work_should_stop(ctx)) { rc = FONT_MORE; break; }
        size_t remaining = len - used;
        size_t chunk = remaining > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : remaining;
        ssize_t n = read(fd, bytes + used, chunk);
        if (n > 0) used += (size_t)n;
        else if (n == 0 || errno != EINTR) rc = FONT_ERR_INIT;
    }
    close(fd);
    if (!rc) rc = font_init_index(&s->face, bytes, len, index);
    if (!rc) rc = font_monospace(&s->face);
    if (!rc) rc = em ? font_set_em_px(&s->face, px) : font_set_px(&s->face, px);
    /* Admit only cells fitting the existing prepaid atlas, minimap and glyph
     * scratch bounds. A refused face takes the embedded fallback on the worker. */
    if (!rc && (!s->face.cell.cell_w || !s->face.cell.cell_h ||
        s->face.cell.cell_w > max_cell.cell_w || s->face.cell.cell_h > max_cell.cell_h)) rc = FONT_ERR_INIT;
    return rc;
}
