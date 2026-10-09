#include "editor/editor.h"
#include "editor/runtime.h"
#include "journal/journal.h"
#include "raster/raster.h"
#include "trace/trace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_directory(char *path)
{
    /* Resolved absolute path, setup only. Existing parents may be shared. */
    for (char *p = path + 1; ; p++) {
        if (*p != '/' && *p != '\0') continue;
        char saved = *p; *p = '\0';
        int rc = mkdir(path, 0700);
        if (rc && errno == EEXIST) {
            struct stat st; rc = stat(path, &st);
            if (!rc && !S_ISDIR(st.st_mode)) { errno = ENOTDIR; rc = -1; }
        }
        *p = saved;
        if (rc) return -1;
        if (!saved) return 0;
    }
}
int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: sublimite <file>\n"); return 2; }
    const char *display = getenv("DISPLAY");
    const char *selected = editor_runtime_display(display, getenv("EDIT_DISPLAY"), getenv("EDIT_ALLOW_REAL_DISPLAY"));
    if (selected != display && setenv("DISPLAY", selected, 1)) return 1;
    char journal_path[4097];
    if (journal_default_dir(journal_path, sizeof journal_path) != JOURNAL_OK) {
        fprintf(stderr, "sublimite: cannot resolve journal directory\n"); return 1;
    }
    if (make_directory(journal_path)) { perror("sublimite: journal directory"); return 1; }
    const char suffix[] = "/session-XXXXXX";
    size_t n = strlen(journal_path);
    if (n > sizeof journal_path - sizeof suffix) return 1;
    memcpy(journal_path + n, suffix, sizeof suffix);
    int fd = mkstemp(journal_path);
    if (fd < 0) { perror("sublimite: journal"); return 1; }
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
    /* editor_close joins workers: the trace rings are quiescent here. */
    const char *dump_path = getenv("EDIT_TRACE_DUMP");
    if (dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        int dumped = dump ? trace_dump(dump) : -1;
        if (dump && fclose(dump)) dumped = -1;
        if (dumped) {
            fprintf(stderr, "sublimite: cannot write trace dump: %s\n", dump_path);
            if (!rc) rc = EDITOR_ERR_IO;
        }
    }
    if (rc) fprintf(stderr, "sublimite: error %d; journal: %s\n", rc, journal_path);
    else fprintf(stderr, "sublimite: unsaved session journal: %s\n", journal_path);
    return rc ? 1 : 0;
}
