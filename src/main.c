#include "editor/editor.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool real_display(const char *s)
{
    if (!s) return true;
    const char *colon = strrchr(s, ':');
    if (!colon || !colon[1]) return true;
    const char *p = colon + 1; bool nonzero = false;
    while (*p >= '0' && *p <= '9') { if (*p != '0') nonzero = true; p++; }
    return !nonzero || (*p && *p != '.');
}
int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: edit <file>\n"); return 2; }
    const char *display = getenv("DISPLAY");
    if (real_display(display)) {
        const char *safe = getenv("EDIT_DISPLAY");
        if (!safe || real_display(safe)) safe = ":99";
        if (setenv("DISPLAY", safe, 1)) return 1;
    }
    size_t n = strlen(argv[1]);
    if (n > SIZE_MAX - 32) return 1;
    char *journal_path = malloc(n + 32); if (!journal_path) return 1;
    (void)snprintf(journal_path, n + 32, "%s.edit-journal-XXXXXX", argv[1]);
    int fd = mkstemp(journal_path);
    if (fd < 0) { perror("edit: journal"); free(journal_path); return 1; }
    close(fd);
    trace_init(); (void)trace_thread_register();
    render_backend backend = {0};
    /* The only application backend selection site. A future GL factory can
     * fill the same handle; the editor loop depends solely on render.h. */
    int rc = render_cpu_backend(&backend);
    editor *e = NULL;
    editor_config config = {.path = argv[1], .journal_path = journal_path};
    if (!rc) rc = editor_open(&e, &config, &backend);
    if (!rc) rc = editor_run(e);
    if (e) {
        int flushed = editor_flush(e);
        if (!rc) rc = flushed;
        editor_close(e);
    }
    if (rc) fprintf(stderr, "edit: error %d; journal: %s\n", rc, journal_path);
    else fprintf(stderr, "edit: unsaved session journal: %s\n", journal_path);
    free(journal_path);
    return rc ? 1 : 0;
}
