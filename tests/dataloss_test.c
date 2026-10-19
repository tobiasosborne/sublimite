/* edit-dataloss-s6: adversarial data-loss campaign. Headless (null backend).
 * Every scenario forks a child editor, kills it (SIGKILL, from the parent or
 * from a worker-thread hook) or lets two instances race, then restarts and
 * compares bytes, modified flags and carets against the child's published
 * oracle. Fixture byte lengths are strlen(literal); waits use EDITOR_IDLE_NS
 * (G, existing loop-policy watchdog, not a latency claim). Geometry: cols =
 * strlen of the longest status prompt + margin, rows = tab + text + status + 1.
 *
 * Probes that FAIL are findings (see docs/worker-reports/edit-dataloss-s6.md)
 * and run only with --probe so `make check` stays green; everything else is a
 * regression test. */
#include "editor/private.h"
#include "editor/large.h"
#include "trace/trace.h"
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "dataloss_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
enum { COLS = 40, ROWS = 4, TEXT_CAP = 4096 };
typedef struct oracle { uint64_t len, cursor; uint8_t text[TEXT_CAP]; } oracle;
static uint64_t deadline(void) { return trace_now_ns() + EDITOR_IDLE_NS; }
static int turn(editor *e)
{
    int rc = editor_step(e, 0);
    if (rc != EDITOR_OK && rc != EDITOR_MORE)
        fprintf(stderr, "dataloss_test: step error=%d journal=%d active_BASE=[%s]\n", rc,
            e->stats.journal_error, e->buffer->base.path);
    return rc == EDITOR_OK || rc == EDITOR_MORE ? 0 : rc;
}
static int settle(editor *e)
{
    uint64_t limit = deadline();
    do { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    while (editor_get_stats(e).pending || editor_get_stats(e).render_active);
    return 0;
}
static int press(editor *e, uint32_t symbol, uint16_t mods, const char *text)
{
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = symbol, .mods = mods, .t0_ns = trace_now_ns()};
    if (text) { event.utf8_len = (uint8_t)strlen(text); memcpy(event.utf8, text, event.utf8_len); }
    T(editor_inject(e, &event) == 0); return settle(e);
}
static int saved(editor *e)
{
    uint64_t limit = deadline();
    while (editor_save_active(e)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return settle(e);
}
static int write_file(const char *path, const char *bytes)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    size_t len = strlen(bytes); T(write(fd, bytes, len) == (ssize_t)len); T(close(fd) == 0); return 0;
}
/* Returns -1 when the file is missing; otherwise the byte count read into out. */
static ssize_t read_file(const char *path, char *out, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return -1;
    size_t done = 0;
    for (;;) {
        ssize_t n = read(fd, out + done, cap - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n; if (done == cap) break;
    }
    close(fd); return (ssize_t)done;
}
static int disk_is(const char *path, const char *expected)
{
    char bytes[TEXT_CAP]; ssize_t n = read_file(path, bytes, sizeof bytes);
    if (n != (ssize_t)strlen(expected) || memcmp(bytes, expected, strlen(expected))) {
        fprintf(stderr, "dataloss_test: %s holds %zd bytes [%.*s], expected [%s]\n", path, n, n > 0 ? (int)n : 0, bytes, expected);
        T(false);
    }
    return 0;
}
static int text_is(editor *e, const char *expected)
{
    uint8_t bytes[TEXT_CAP]; uint64_t len = editor_length(e); T(len < sizeof bytes);
    T(editor_read(e, 0, bytes, (size_t)len) == 0);
    if (len != strlen(expected) || memcmp(bytes, expected, (size_t)len)) {
        fprintf(stderr, "dataloss_test: buffer holds %llu bytes [%.*s], expected [%s]\n", (unsigned long long)len, (int)len, bytes, expected);
        T(false);
    }
    return 0;
}
static int make_root(char *root, size_t cap)
{ T(snprintf(root, cap, "build/dataloss-XXXXXX") > 0); T(mkdtemp(root)); return 0; }
static int make_session(const char *root, char *out, size_t cap)
{
    T(snprintf(out, cap, "%s/session-XXXXXX", root) > 0);
    int fd = mkstemp(out); T(fd >= 0); T(close(fd) == 0); return 0;
}
static void remove_tree(const char *root)
{
    DIR *dir = opendir(root); if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        (void)unlinkat(dirfd(dir), entry->d_name, 0);
    closedir(dir); (void)rmdir(root);
}
/* edit-cxo: concurrently starting instances need the creator-held lease, as
 * src/main.c does; a bare mkstemp placeholder is retirable by the other. */
static int make_leased_session(const char *root, char *out, size_t cap, int *lease)
{ T(journal_session_create(root, out, cap, lease) == JOURNAL_OK); return 0; }
static int open_editor_leased(editor **e, render_backend *backend, const char *path, const char *jpath, const int *lease)
{
    *backend = (render_backend){0}; T(render_null_backend(backend) == 0);
    editor_config cfg = {.path = path, .journal_path = jpath, .session_lease = lease, .start_empty = !path, .cols = COLS, .rows = ROWS};
    *e = NULL; int rc = editor_open(e, &cfg, backend);
    if (rc) fprintf(stderr, "dataloss_test: editor_open returned %d\n", rc);
    T(rc == 0); return settle(*e);
}
static int open_editor(editor **e, render_backend *backend, const char *path, const char *jpath)
{ return open_editor_leased(e, backend, path, jpath, NULL); }
static int capture(editor *e, oracle *o)
{
    o->len = editor_length(e); T(o->len <= sizeof o->text);
    T(editor_read(e, 0, o->text, (size_t)o->len) == 0); o->cursor = editor_view(e).selection.cursor; return 0;
}
static int read_oracle(int fd, oracle *o)
{
    size_t received = 0;
    while (received < sizeof *o) {
        ssize_t n = read(fd, (uint8_t *)o + received, sizeof *o - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    return received == sizeof *o ? 0 : 1;
}
static int killed_by(pid_t child, int expected_signal)
{
    int status; T(waitpid(child, &status, 0) == child);
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == expected_signal))
        fprintf(stderr, "dataloss_test: child status signaled=%d sig=%d exited=%d code=%d\n",
            WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : 0, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : 0);
    T(WIFSIGNALED(status) && WTERMSIG(status) == expected_signal); return 0;
}
static int exited_with(pid_t child, int code)
{
    int status; T(waitpid(child, &status, 0) == child);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == code))
        fprintf(stderr, "dataloss_test: child status signaled=%d sig=%d exited=%d code=%d\n",
            WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : 0, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : 0);
    T(WIFEXITED(status) && WEXITSTATUS(status) == code); return 0;
}

/* (1) A keystroke is applied and journalled (page cache), then the process is
 * killed before any pump/fsync/flush. The recovered text must equal what the
 * editor showed. File-backed buffer: hot-exit's trial covers untitled only. */
static int kill_after_append_child(const char *source, const char *jpath, int fd)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(editor_set_cursor(e, strlen("base")) == 0); T(settle(e) == 0);
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &event) == 0);
    uint64_t limit = deadline(), before = editor_get_stats(e).mutations;
    while (editor_get_stats(e).mutations == before) { T(turn(e) == 0); T(trace_now_ns() < limit); }
    oracle o; T(capture(e, &o) == 0); T(write(fd, &o, sizeof o) == (ssize_t)sizeof o);
    for (;;) pause(); /* no further turn: nothing pumps the journal */
}
static int kill_after_append(void)
{
    char root[64], source[128], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, first, sizeof first) == 0);
    int pipes[2]; T(pipe(pipes) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(pipes[0]); _exit(kill_after_append_child(source, first, pipes[1])); }
    close(pipes[1]); oracle want; int got = read_oracle(pipes[0], &want); close(pipes[0]);
    T(kill(child, SIGKILL) == 0); T(killed_by(child, SIGKILL) == 0); T(got == 0);
    T(want.len == strlen("basex") && !memcmp(want.text, "basex", want.len) && want.cursor == 5);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    T(tabs_count(&e->tabs) == 1); T(text_is(e, "basex") == 0); T(editor_tab(e, 0)->modified);
    T(editor_view(e).selection.cursor == want.cursor);
    editor_close(e); remove_tree(root); return 0;
}

/* (2) SIGKILL from the save worker at every durability step. The target must
 * hold exactly the old or the new bytes; restart must show the edit as dirty
 * and a fresh Ctrl+S must land it. */
static void kill_at_step(void *ctx, int phase) { if (phase == *(const int *)ctx) (void)kill(getpid(), SIGKILL); }
static int killed_mid_save_child(const char *source, const char *jpath, int phase)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'x', 0, "x") == 0); T(e->buffer->save);
    T(savectl_set_step_hook(e->buffer->save, kill_at_step, &phase) == 0);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0);
    uint64_t limit = deadline();
    for (;;) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
}
static int killed_mid_save(int phase)
{
    char root[64], source[128], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, first, sizeof first) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(killed_mid_save_child(source, first, phase));
    T(killed_by(child, SIGKILL) == 0);
    char bytes[TEXT_CAP]; ssize_t n = read_file(source, bytes, sizeof bytes);
    bool old = n == (ssize_t)strlen("base") && !memcmp(bytes, "base", 4);
    bool new = n == (ssize_t)strlen("xbase") && !memcmp(bytes, "xbase", 5);
    if (!old && !new) fprintf(stderr, "dataloss_test: phase %d left %zd bytes [%.*s]\n", phase, n, n > 0 ? (int)n : 0, bytes);
    T(old || new);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    T(tabs_count(&e->tabs) == 1); T(text_is(e, "xbase") == 0); T(editor_tab(e, 0)->modified);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(!e->buffer->save_error); T(!editor_tab(e, 0)->modified); T(disk_is(source, "xbase") == 0);
    editor_close(e); remove_tree(root); return 0;
}

/* (3)/(7) Unsaved typing on two files, crash, then the first base is
 * truncated shorter than the journal's offsets (mtime restored) or deleted.
 * Launch must succeed, the untouched buffer keeps its edits, the typed bytes
 * survive in the set-aside journal. */
enum { BASE_SHORTER, BASE_DELETED };
static int changed_base_child(const char *source, const char *other, const char *jpath)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    e->retain_limit = 0; /* edit-fi2: pin edit-btj's set-aside fallback (no retained copy) */
    T(editor_set_cursor(e, strlen("base")) == 0); T(settle(e) == 0);
    T(press(e, 'M', 0, "MK") == 0); /* "baseMK": offsets 4..6 exceed a 2-byte base */
    uint64_t id; T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(press(e, 'x', 0, "x") == 0); _exit(0);
}
static bool file_contains(const char *path, const char *needle)
{
    char bytes[65536]; ssize_t n = read_file(path, bytes, sizeof bytes); if (n <= 0) return false;
    for (size_t i = 0; i + strlen(needle) <= (size_t)n; i++) if (!memcmp(bytes + i, needle, strlen(needle))) return true;
    return false;
}
static int changed_base(int kind)
{
    char root[64], source[128], other[128], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0); T(write_file(other, "keep") == 0);
    T(make_session(root, first, sizeof first) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) _exit(changed_base_child(source, other, first));
    T(exited_with(child, 0) == 0);
    if (kind == BASE_SHORTER) {
        struct stat st; T(stat(source, &st) == 0);
        T(write_file(source, "ba") == 0);
        struct timespec when[2] = {st.st_atim, st.st_mtim}; int fd = open(source, O_WRONLY); T(fd >= 0);
        T(futimens(fd, when) == 0); close(fd); /* same mtime: only size/ctime/CRC differ */
    } else T(unlink(source) == 0);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    fprintf(stderr, "dataloss_test: changed_base kind=%d tabs=%zu notice=[%s] aside=[%s]\n", kind, tabs_count(&e->tabs),
        e->recovery_notice ? e->recovery_notice : "", e->recovery_aside);
    T(*e->recovery_aside && file_contains(e->recovery_aside, "MK"));
    T(access(first, F_OK) < 0);
    /* The unchanged second buffer must come back dirty with its edit. */
    bool found = false;
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *tab = editor_tab(e, i);
        if (piece_len(tab->buffer) == strlen("xkeep")) {
            uint8_t bytes[8]; T(piece_read(tab->buffer, 0, bytes, 5) == 0);
            if (!memcmp(bytes, "xkeep", 5)) { found = true; T(tab->modified); }
        }
    }
    if (!found) fprintf(stderr, "dataloss_test: changed_base kind=%d: the untouched buffer's edit [xkeep] was not restored live\n", kind);
    T(found);
    if (kind == BASE_SHORTER) { T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(text_is(e, "ba") == 0); T(!editor_tab(e, 0)->modified); }
    editor_close(e); remove_tree(root); return 0;
}

/* (4) Reload (R) chosen for a stale tab while another tab's save is held
 * mid-write; the choice must wait for the save and then apply exactly. */
typedef struct held_save { atomic_bool entered, release; int phase; } held_save;
static void hold(void *ctx, int phase)
{
    held_save *h = ctx; if (phase != h->phase) return;
    atomic_store(&h->entered, true); while (!atomic_load(&h->release)) sched_yield();
}
static int enter_hold(editor *e, held_save *h)
{
    uint64_t limit = deadline();
    while (!atomic_load(&h->entered)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return 0;
}
static int stale_prompt(editor *e, const char *path, const char *bytes)
{
    T(write_file(path, bytes) == 0);
    uint64_t limit = deadline();
    while (!e->buffer->source_stale || !e->buffer->prompt_ns) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return settle(e);
}
/* A queued R/K choice is runnable work but not editor_stats.pending: spin
 * until check_sources consumed it, then settle. */
static int choose(editor *e, bool reload)
{
    T(press(e, reload ? 'r' : 'k', 0, NULL) == 0);
    uint64_t limit = deadline();
    while (e->source_choice) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    return settle(e);
}
static int reload_during_save(bool reload)
{
    char root[64], source[128], other[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0); T(write_file(other, "keep") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    uint64_t id; T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
    /* edit-7cj8 (P): the existing zero-limit seam keeps copies unavailable
     * until save owns the journal, making this peer-BASE race deterministic. */
    e->retain_limit = 0;
    T(press(e, 'y', 0, "y") == 0); T(stale_prompt(e, other, "external") == 0);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(press(e, 'x', 0, "x") == 0);
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(e->buffer->save, hold, &held) == 0);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(e, &held) == 0);
    e->retain_limit = JOURNAL_STARTUP_SCAN_BYTES;
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0); T(e->buffer->source_stale);
    uint64_t limit = deadline();
    while (!e->buffer->prompt_ns) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(press(e, reload ? 'r' : 'k', 0, NULL) == 0);
    T(disk_is(source, "base") == 0); /* still held */
    atomic_store(&held.release, true); T(saved(e) == 0);
    T(disk_is(source, "xbase") == 0); T(!editor_tab(e, 0)->modified);
    if (reload) { T(text_is(e, "external") == 0); T(!editor_tab(e, 1)->modified); T(!e->buffer->source_stale); }
    else {
        T(text_is(e, "ykeep") == 0); T(editor_tab(e, 1)->modified); T(!e->buffer->source_stale);
        T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
        T(!e->buffer->save_error); T(disk_is(other, "ykeep") == 0); T(!editor_tab(e, 1)->modified);
    }
    editor_close(e); remove_tree(root); return 0;
}

/* (5) Ctrl+S while the R/K prompt is showing: nothing may be written or
 * dropped; K then Ctrl+S lands the buffer; R shows the disk text. */
static int save_during_prompt(bool reload)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'x', 0, "x") == 0); T(stale_prompt(e, source, "external") == 0);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(disk_is(source, "external") == 0); T(text_is(e, "xbase") == 0);
    T(editor_tab(e, 0)->modified && e->buffer->source_stale);
    T(choose(e, reload) == 0); T(!e->buffer->source_stale);
    if (reload) { T(text_is(e, "external") == 0); T(!editor_tab(e, 0)->modified); T(disk_is(source, "external") == 0); }
    else {
        T(text_is(e, "xbase") == 0); T(editor_tab(e, 0)->modified);
        T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
        T(!e->buffer->save_error); T(disk_is(source, "xbase") == 0); T(!editor_tab(e, 0)->modified);
    }
    editor_close(e); remove_tree(root); return 0;
}

/* (6) Two dirty file tabs, one dirty untitled buffer and one dirty closed
 * tab; SIGKILL; restart must restore bytes, dirty flags, carets and the
 * closed tab, and the file tabs must then save their bytes. */
typedef struct mixed_oracle { oracle tabs[3]; size_t active; } mixed_oracle;
static int hot_exit_mixed_child(const char *a, const char *b, const char *d, const char *jpath, int fd)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, a, jpath) == 0);
    T(press(e, 'x', 0, "x") == 0);                                            /* A: "xbase", caret 1 */
    uint64_t id; T(editor_add_buffer(e, b, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, strlen("keep")) == 0); T(settle(e) == 0); T(press(e, 'y', 0, "y") == 0); /* B: "keepy", caret 5 */
    T(editor_add_buffer(e, NULL, (const uint8_t *)"untitled", strlen("untitled"), &id) == 0); T(settle(e) == 0);
    T(editor_set_cursor(e, 3) == 0); T(settle(e) == 0); T(press(e, 'z', 0, "z") == 0); /* C: "untzitled", caret 4 */
    T(editor_add_buffer(e, d, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(press(e, 'w', 0, "w") == 0);                                            /* D: "wgone", then closed */
    T(editor_close_tab(e, 3) == 0); T(settle(e) == 0); T(tabs_count(&e->tabs) == 3);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0);
    mixed_oracle o = {.active = 1};
    for (size_t i = 0; i < 3; i++) {
        const tabs_tab *tab = editor_tab(e, i); o.tabs[i].len = piece_len(tab->buffer);
        T(piece_read(tab->buffer, 0, o.tabs[i].text, (size_t)o.tabs[i].len) == 0);
        o.tabs[i].cursor = i == 1 ? editor_view(e).selection.cursor : tab->state.selection.cursor;
    }
    T(write(fd, &o, sizeof o) == (ssize_t)sizeof o);
    for (;;) pause();
}
static int mixed_equal(editor *e, const mixed_oracle *want)
{
    T(tabs_count(&e->tabs) == 3); T(tabs_active_index(&e->tabs) == want->active);
    for (size_t i = 0; i < 3; i++) {
        const tabs_tab *tab = editor_tab(e, i); uint8_t bytes[TEXT_CAP];
        T(piece_len(tab->buffer) == want->tabs[i].len); T(piece_read(tab->buffer, 0, bytes, (size_t)want->tabs[i].len) == 0);
        T(!memcmp(bytes, want->tabs[i].text, (size_t)want->tabs[i].len)); T(tab->modified);
        uint64_t cursor = i == want->active ? editor_view(e).selection.cursor : tab->state.selection.cursor;
        if (cursor != want->tabs[i].cursor) fprintf(stderr, "dataloss_test: tab %zu caret %llu, want %llu\n", i, (unsigned long long)cursor, (unsigned long long)want->tabs[i].cursor);
        T(cursor == want->tabs[i].cursor);
    }
    return 0;
}
static int hot_exit_mixed(void)
{
    char root[64], a[128], b[128], d[128], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(a, sizeof a, "%s/a", root) > 0); T(write_file(a, "base") == 0);
    T(snprintf(b, sizeof b, "%s/b", root) > 0); T(write_file(b, "keep") == 0);
    T(snprintf(d, sizeof d, "%s/d", root) > 0); T(write_file(d, "gone") == 0);
    T(make_session(root, first, sizeof first) == 0);
    int pipes[2]; T(pipe(pipes) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(pipes[0]); _exit(hot_exit_mixed_child(a, b, d, first, pipes[1])); }
    close(pipes[1]); mixed_oracle want; size_t received = 0;
    while (received < sizeof want) {
        ssize_t n = read(pipes[0], (uint8_t *)&want + received, sizeof want - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    close(pipes[0]); T(kill(child, SIGKILL) == 0); T(killed_by(child, SIGKILL) == 0); T(received == sizeof want);
    T(want.tabs[0].len == 5 && want.tabs[1].len == 5 && want.tabs[2].len == 9);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    T(mixed_equal(e, &want) == 0); T(settle(e) == 0); T(mixed_equal(e, &want) == 0);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(!e->buffer->save_error); T(disk_is(a, "xbase") == 0); T(!editor_tab(e, 0)->modified);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0); T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(!e->buffer->save_error); T(disk_is(b, "keepy") == 0); T(!editor_tab(e, 1)->modified);
    T(editor_reopen_tab(e) == 0); T(settle(e) == 0); T(tabs_count(&e->tabs) == 4);
    T(text_is(e, "wgone") == 0); T(editor_tab(e, tabs_active_index(&e->tabs))->modified);
    editor_close(e); remove_tree(root); return 0;
}

/* (8) Undo of a whole-buffer replacement is journalled as DELETE + sliced
 * INSERT across turns. SIGKILL between slices: the recovered text must be the
 * pre-undo or the post-undo text, never a prefix of the group. */
enum { GROUP_BYTES = 256u * 1024u }; /* 16 slices of JOURNAL_INSERT_SLICE_BYTES (G, wire bound) */
typedef struct torn_oracle { uint64_t len, progress, op_len; } torn_oracle;
static int torn_undo_child(const char *jpath, int fd)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, jpath) == 0);
    uint8_t *body = malloc(GROUP_BYTES); T(body);
    for (size_t i = 0; i < GROUP_BYTES; i++) body[i] = (uint8_t)('a' + i % 26u);
    uint64_t id; T(editor_add_buffer(e, NULL, body, GROUP_BYTES, &id) == 0); T(settle(e) == 0);
    T(press(e, 'a', PLAT_MOD_CTRL, NULL) == 0); T(press(e, 'x', 0, "x") == 0);
    T(editor_length(e) == 1);
    plat_event undo = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'z', .mods = PLAT_MOD_CTRL, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &undo) == 0);
    uint64_t limit = deadline();
    for (;;) {
        T(turn(e) == 0); T(trace_now_ns() < limit);
        for (size_t i = 0; i < e->op_count; i++) {
            editor_jop *op = &e->ops[i];
            if (op->insert && op->tree && op->progress > 0 && op->progress < op->len) {
                torn_oracle o = {editor_length(e), op->progress, op->len};
                T(write(fd, &o, sizeof o) == (ssize_t)sizeof o);
                (void)kill(getpid(), SIGKILL);
            }
        }
        if (!e->op_count && !editor_get_stats(e).pending && e->action == EDITOR_ACTION_NONE) {
            torn_oracle o = {editor_length(e), 0, 0}; T(write(fd, &o, sizeof o) == (ssize_t)sizeof o); _exit(3);
        }
    }
}
static int torn_undo_group(void)
{
    char root[64], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(make_session(root, first, sizeof first) == 0);
    int pipes[2]; T(pipe(pipes) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(pipes[0]); _exit(torn_undo_child(first, pipes[1])); }
    close(pipes[1]); torn_oracle want; size_t received = 0;
    while (received < sizeof want) {
        ssize_t n = read(pipes[0], (uint8_t *)&want + received, sizeof want - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    close(pipes[0]); int status; T(waitpid(child, &status, 0) == child); T(received == sizeof want);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 3) { fprintf(stderr, "dataloss_test: torn_undo_group: no slice window observed (len=%llu); scenario not exercised\n", (unsigned long long)want.len); T(false); }
    T(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    T(tabs_count(&e->tabs) == 1); uint64_t len = editor_length(e);
    fprintf(stderr, "dataloss_test: torn_undo_group: killed at insert progress %llu/%llu, editor showed %llu bytes, recovered %llu bytes\n",
        (unsigned long long)want.progress, (unsigned long long)want.op_len, (unsigned long long)want.len, (unsigned long long)len);
    T(len == 1 || len == GROUP_BYTES);
    editor_close(e); remove_tree(root); return 0;
}

/* (9) A clean buffer's source changes on disk and a key is typed in the same
 * frame (edit-ret silent refresh). The key must land on either text, never
 * vanish; if it landed on the old text the dirty-buffer prompt must show. */
static int reload_vs_keystroke(unsigned turns_between)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(write_file(source, "external") == 0);
    for (unsigned i = 0; i < turns_between; i++) T(turn(e) == 0);
    plat_event event = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'x', .utf8_len = 1, .utf8 = {'x'}, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &event) == 0); T(settle(e) == 0);
    uint64_t limit = deadline();
    /* Drain the inotify notification whichever order the turn took. */
    while (trace_now_ns() < limit && !e->buffer->source_stale && editor_length(e) != strlen("xexternal")) { T(turn(e) == 0); sched_yield(); }
    T(settle(e) == 0);
    uint8_t bytes[TEXT_CAP]; uint64_t len = editor_length(e); T(editor_read(e, 0, bytes, (size_t)len) == 0);
    fprintf(stderr, "dataloss_test: reload_vs_keystroke turns=%u text=[%.*s] stale=%d modified=%d\n", turns_between, (int)len, bytes, e->buffer->source_stale, editor_tab(e, 0)->modified);
    bool on_new = len == strlen("xexternal") && !memcmp(bytes, "xexternal", len);
    bool on_old = len == strlen("xbase") && !memcmp(bytes, "xbase", len);
    T(on_new || on_old); T(editor_tab(e, 0)->modified);
    if (on_old) T(e->buffer->source_stale);
    editor_close(e); remove_tree(root); return 0;
}

/* (10a) Two processes edit and save the same file. The outcomes are (P,
 * literal insertions at the beginning of "base"): A saves "Abase" and B
 * retains dirty "Bbase" with a stale prompt or save error; B saves "Bbase"
 * and A retains stale "Abase"; or clean B reloads "Abase", types "B", and
 * saves exactly "BAbase". A displaced edit must remain visible; a merged
 * save must contain both contributions and leave B clean without an error. */
typedef struct race_oracle { int save_error; bool modified, stale; oracle text; } race_oracle;
static int two_process_child(const char *source, const char *jpath, const int *lease, int go, int fd, bool reload_before_type)
{
    render_backend backend; editor *e; T(open_editor_leased(&e, &backend, source, jpath, lease) == 0);
    if (reload_before_type) {
        T(text_is(e, "base") == 0);
        /* (P) One ready byte proves B opened the old base before A saves. */
        T(write(fd, "r", sizeof(char)) == (ssize_t)sizeof(char));
    } else T(press(e, 'B', 0, "B") == 0);
    uint8_t byte; ssize_t n; do n = read(go, &byte, 1); while (n < 0 && errno == EINTR); T(n == 1);
    if (reload_before_type) {
        /* The suite can fork after the process-wide inotify fd exists.
         * Probe explicitly: A's polling must not consume B's notice. */
        T(file_check(e->buffer->file, NULL));
        uint64_t limit = deadline();
        while (editor_length(e) != strlen("Abase")) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
        T(text_is(e, "Abase") == 0); T(!editor_tab(e, 0)->modified);
        T(press(e, 'B', 0, "B") == 0);
    }
    /* The race variant makes no turn between A's save and Ctrl+S; the
     * ordered variant proves a clean reload before B types and saves. */
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    race_oracle o = {.save_error = e->buffer->save_error, .modified = editor_tab(e, 0)->modified, .stale = e->buffer->source_stale};
    T(capture(e, &o.text) == 0); T(write(fd, &o, sizeof o) == (ssize_t)sizeof o); _exit(0);
}
static int two_process_save(bool reload_before_type)
{
    char root[64], source[128], first[128], second[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    int first_lease = -1, second_lease = -1;
    T(make_leased_session(root, first, sizeof first, &first_lease) == 0);
    T(make_leased_session(root, second, sizeof second, &second_lease) == 0);
    int go[2], report[2]; T(pipe(go) == 0); T(pipe(report) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) { close(first_lease); close(go[1]); close(report[0]); _exit(two_process_child(source, second, &second_lease, go[0], report[1], reload_before_type)); }
    close(second_lease); close(go[0]); close(report[1]);
    render_backend backend; editor *e; T(open_editor_leased(&e, &backend, source, first, &first_lease) == 0);
    close(first_lease); /* the editor holds its own duplicate */
    if (reload_before_type) {
        uint8_t ready; ssize_t n;
        do n = read(report[0], &ready, sizeof ready); while (n < 0 && errno == EINTR);
        T(n == (ssize_t)sizeof ready);
    }
    T(press(e, 'A', 0, "A") == 0); T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    T(!e->buffer->save_error); T(disk_is(source, "Abase") == 0);
    T(write(go[1], "g", 1) == 1); close(go[1]);
    race_oracle b; size_t received = 0;
    while (received < sizeof b) {
        ssize_t n = read(report[0], (uint8_t *)&b + received, sizeof b - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    close(report[0]); T(exited_with(child, 0) == 0); T(received == sizeof b);
    uint64_t limit = deadline();
    while (trace_now_ns() < limit && !e->buffer->source_stale) { T(turn(e) == 0); sched_yield(); }
    char disk[TEXT_CAP]; ssize_t n = read_file(source, disk, sizeof disk);
    fprintf(stderr, "dataloss_test: two_process_save reload_before_type=%d disk=[%.*s] A(stale=%d modified=%d) B(err=%d stale=%d modified=%d text=[%.*s])\n",
        reload_before_type, n > 0 ? (int)n : 0, disk, e->buffer->source_stale, editor_tab(e, 0)->modified, b.save_error, b.stale, b.modified, (int)b.text.len, b.text.text);
    /* (P) Exact lengths are the byte counts of the expected literals. */
    bool a_won = n == (ssize_t)strlen("Abase") && !memcmp(disk, "Abase", strlen("Abase"));
    bool b_won = n == (ssize_t)strlen("Bbase") && !memcmp(disk, "Bbase", strlen("Bbase"));
    bool merged = n == (ssize_t)strlen("BAbase") && !memcmp(disk, "BAbase", strlen("BAbase"));
    T(a_won || b_won || merged);
    if (reload_before_type) T(merged);
    if (a_won) { T(b.modified && (b.save_error || b.stale)); T(b.text.len == strlen("Bbase") && !memcmp(b.text.text, "Bbase", strlen("Bbase"))); }
    else if (b_won) { T(e->buffer->source_stale); T(text_is(e, "Abase") == 0); }
    else {
        T(!b.save_error && !b.modified && !b.stale);
        T(b.text.len == strlen("BAbase") && !memcmp(b.text.text, "BAbase", strlen("BAbase")));
    }
    editor_close(e); remove_tree(root); return 0;
}

/* (10b) Two instances share the journal directory (main.c's layout). One
 * crashes with unsaved edits while the other keeps running and later closes
 * cleanly. The next launches must surface the crashed instance's edits. */
static int orphan_child(const char *source, const char *jpath, int go, int ready, bool crash)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, crash ? 'x' : 'y', 0, crash ? "x" : "y") == 0);
    /* (P) One ready byte proves this instance has published its edit. */
    T(write(ready, "r", sizeof(char)) == (ssize_t)sizeof(char)); close(ready);
    uint8_t byte; ssize_t n; do n = read(go, &byte, sizeof byte); while (n < 0 && errno == EINTR); T(n == (ssize_t)sizeof byte);
    if (crash) _exit(0);
    T(settle(e) == 0); editor_close(e); _exit(0);
}
static int stranded_orphan(bool limited, bool shared_target)
{
    char root[IPC_PATH_CAP], a[IPC_PATH_CAP], b[IPC_PATH_CAP], first[IPC_PATH_CAP], second[IPC_PATH_CAP];
    T(make_root(root, sizeof root) == 0);
    T(snprintf(a, sizeof a, "%s/a", root) > 0); T(write_file(a, "base") == 0);
    if (shared_target) strcpy(b, a);
    else { T(snprintf(b, sizeof b, "%s/b", root) > 0); T(write_file(b, "base") == 0); }
    enum { ORPHAN_INSTANCES = 2 }; /* (P) Crashed instance + surviving instance. */
    T(make_session(root, first, sizeof first) == 0); T(make_session(root, second, sizeof second) == 0);
    /* (P) POSIX pipes have a read end and a write end. */
    int go1[2], go2[2], ready[2]; T(pipe(go1) == 0); T(pipe(go2) == 0); T(pipe(ready) == 0);
    pid_t crasher = fork(); T(crasher >= 0);
    if (!crasher) { close(go1[1]); close(go2[0]); close(go2[1]); close(ready[0]); _exit(orphan_child(a, first, go1[0], ready[1], true)); }
    /* (P) Serial startup isolates recovery from the sibling discovery race. */
    char ready_byte; T(read(ready[0], &ready_byte, sizeof ready_byte) == (ssize_t)sizeof ready_byte);
    pid_t survivor = fork(); T(survivor >= 0);
    if (!survivor) { close(go2[1]); close(go1[0]); close(go1[1]); close(ready[0]); _exit(orphan_child(b, second, go2[0], ready[1], false)); }
    close(go1[0]); close(go2[0]); close(ready[1]);
    /* (P) The remaining ready byte comes from the surviving instance. */
    T(read(ready[0], &ready_byte, sizeof ready_byte) == (ssize_t)sizeof ready_byte);
    close(ready[0]);
    T(write(go1[1], "g", 1) == 1); close(go1[1]); T(exited_with(crasher, 0) == 0);
    /* (P) Epoch mtime sorts before the survivor's real checkpoint; this is
     * ordering evidence, not an invented delay between closes. */
    const struct timespec oldest[2] = {{0, 0}, {0, 0}};
    T(utimensat(AT_FDCWD, first, oldest, 0) == 0);
    T(write(go2[1], "g", 1) == 1); close(go2[1]); T(exited_with(survivor, 0) == 0);
    /* (P) Generations cover initial recovery, its replacement checkpoint and
     * that checkpoint's replacement: the campaign's three-launch reproducer. */
    for (unsigned launch = 0; launch < 3; launch++) {
        bool seen_a = false, seen_b = false;
        char next[IPC_PATH_CAP]; T(make_session(root, next, sizeof next) == 0);
        render_backend backend = {0}; editor *e = NULL; T(render_null_backend(&backend) == 0);
        bool full = !limited || launch != 0;
        editor_config cfg = {.journal_path = next, .start_empty = true, .cols = COLS, .rows = ROWS,
            .tab_capacity = full ? 0 : 1}; /* (P) Single live slot forces deferred recovery. */
        T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
        for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
            const tabs_tab *tab = editor_tab(e, i);
            uint8_t bytes[sizeof "xbase" - 1]; /* (P) Marker bytes exclude NUL. */
            if (piece_len(tab->buffer) != sizeof bytes || piece_read(tab->buffer, 0, bytes, sizeof bytes)) continue;
            if (!memcmp(bytes, "xbase", sizeof bytes) && tab->modified) seen_a = true;
            if (!memcmp(bytes, "ybase", sizeof bytes) && tab->modified) seen_b = true;
        }
        fprintf(stderr, "dataloss_test: stranded_orphan launch %u: tabs=%zu seen_crashed=%d seen_survivor=%d\n", launch, tabs_count(&e->tabs), seen_a, seen_b);
        T(seen_b);
        if (full) {
            T(seen_a); T(tabs_count(&e->tabs) == ORPHAN_INSTANCES);
            T(editor_tab(e, tabs_active_index(&e->tabs))->path_len == strlen(b));
            T(!memcmp(editor_tab(e, tabs_active_index(&e->tabs))->path, b, strlen(b)));
            T(access(first, F_OK) < 0 && access(second, F_OK) < 0);
        } else { T(!seen_a && e->recovery_notice && access(first, F_OK) == 0); }
        editor_close(e);
    }
    DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, "session-", strlen("session-"))) {
        struct stat st; if (fstatat(dirfd(dir), entry->d_name, &st, 0)) continue;
        fprintf(stderr, "dataloss_test: stranded_orphan leftover %s %lld bytes\n", entry->d_name, (long long)st.st_size);
    }
    closedir(dir);
    remove_tree(root); return 0;
}

/* edit-hw1: (P) the two tabs are the Save As owner and the target peer;
 * pipe ends, zero sentinels and indexes follow POSIX/the tab ordering below.
 * Text sizes derive from literals; path storage reuses IPC_PATH_CAP. */
typedef struct shared_save_result { bool refused, untouched, peer_clean; } shared_save_result;
static int shared_save_crash(void *ctx, const char *from, const char *to)
{
    (void)from; (void)to;
    int fd = *(int *)ctx;
    shared_save_result result = {0};
    if (write(fd, &result, sizeof result) != (ssize_t)sizeof result) _exit(EXIT_FAILURE);
    /* The target has been written, but the preceding checkpoint still names
     * its old BASE. Crash immediately before publishing the replacement. */
    (void)kill(getpid(), SIGKILL); return -1;
}
/* edit-fi2: wait until no BASE copy is running or awaiting its reference
 * checkpoint (journal_set_io requires no active retention operation). */
static int retain_settled(editor *e)
{
    uint64_t limit = deadline();
    for (;;) {
        bool busy = false;
        for (size_t i = 0; i < e->buffer_capacity; i++) {
            const editor_buffer *b = e->buffers[i];
            if (b && (b->retain_state == EDITOR_RETAIN_RUNNING || (b->retain_state == EDITOR_RETAIN_READY && !b->retain_used))) busy = true;
        }
        if (!busy) return 0;
        T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield();
    }
}
static int shared_save_child(const char *source, const char *target, const char *jpath, bool dirty, int fd)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'A', 0, "A") == 0);
    editor_buffer *owner = e->buffer;
    uint64_t peer_id; T(editor_add_buffer(e, target, NULL, 0, &peer_id) == 0); T(settle(e) == 0);
    if (dirty) T(press(e, 'B', 0, "B") == 0);
    editor_buffer *peer = e->buffer;
    uint64_t owner_revision = owner->revision, peer_revision = peer->revision;
    size_t owner_history = owner->history_cursor, peer_history = peer->history_cursor;
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(retain_settled(e) == 0); T(journal_flush(e->journal) == 0);
    if (dirty) {
        journal_io io = {.ctx = &fd, .rename = shared_save_crash};
        T(journal_set_io(e->journal, &io) == 0);
    }
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) {
        char ch[] = {target[i], '\0'};
        T(press(e, (uint8_t)target[i], 0, ch) == 0);
    }
    T(press(e, XKB_KEY_Return, 0, NULL) == 0);
    shared_save_result result = {
        .refused = e->recovery_notice && !strcmp(e->recovery_notice, "target is open and modified in another tab"),
        .untouched = dirty && e->buffer == owner && owner->revision == owner_revision && peer->revision == peer_revision &&
            owner->history_cursor == owner_history && peer->history_cursor == peer_history &&
            !strcmp(owner->path, source) && !strcmp(peer->path, target)};
    const tabs_tab *tab = editor_tab(e, 1);
    uint8_t bytes[sizeof "Asource" - 1u]; /* (P) fixture bytes exclude NUL. */
    result.peer_clean = tab->id == peer_id && !tab->modified && piece_len(tab->buffer) == sizeof bytes &&
        !piece_read(tab->buffer, 0, bytes, sizeof bytes) && !memcmp(bytes, "Asource", sizeof bytes);
    T(write(fd, &result, sizeof result) == (ssize_t)sizeof result);
    /* No editor_close, flush, or later checkpoint after observing the command. */
    (void)kill(getpid(), SIGKILL); return 1;
}
static int shared_save_as(bool dirty)
{
    char root[IPC_PATH_CAP], source[IPC_PATH_CAP], target[IPC_PATH_CAP], first[IPC_PATH_CAP], next[IPC_PATH_CAP];
    T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, sizeof target, "%s/target", root) > 0); T(write_file(target, "base") == 0);
    char canonical[IPC_PATH_CAP];
    T(realpath(source, canonical)); strcpy(source, canonical);
    T(realpath(target, canonical)); strcpy(target, canonical);
    T(make_session(root, first, sizeof first) == 0);
    int report[2]; T(pipe(report) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(report[0]); _exit(shared_save_child(source, target, first, dirty, report[1])); }
    close(report[1]); shared_save_result result = {0}; size_t received = 0;
    while (received < sizeof result) {
        ssize_t n = read(report[0], (uint8_t *)&result + received, sizeof result - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    close(report[0]); T(killed_by(child, SIGKILL) == 0); T(received == sizeof result);
    char disk[TEXT_CAP]; ssize_t disk_len = read_file(target, disk, sizeof disk);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    bool owner_ok = false, peer_ok = false;
    for (size_t i = 0; i < tabs_count(&e->tabs); i++) {
        const tabs_tab *tab = editor_tab(e, i);
        char bytes[TEXT_CAP]; uint64_t len = piece_len(tab->buffer); T(len < sizeof bytes);
        T(piece_read(tab->buffer, 0, (uint8_t *)bytes, (size_t)len) == 0); bytes[len] = '\0';
        fprintf(stderr, "dataloss_test: shared_save_as(%s) restored %s modified=%d text=[%s]\n",
            dirty ? "dirty" : "clean", tab->path, tab->modified, bytes);
        if (i == 0) owner_ok = !strcmp(bytes, "Asource") && tab->modified == dirty;
        if (i == 1) peer_ok = !strcmp(bytes, dirty ? "Bbase" : "Asource") && tab->modified == dirty;
    }
    bool aside = false; DIR *dir = opendir(root); T(dir); struct dirent *entry;
    while ((entry = readdir(dir))) if (strstr(entry->d_name, ".set-aside-")) aside = true;
    closedir(dir);
    fprintf(stderr, "dataloss_test: shared_save_as(%s) refused=%d untouched=%d peer_clean=%d disk=[%.*s] set_aside=%d notice=[%s]\n",
        dirty ? "dirty" : "clean", result.refused, result.untouched, result.peer_clean,
        disk_len > 0 ? (int)disk_len : 0, disk, aside, e->recovery_notice ? e->recovery_notice : "");
    const char *expected_disk = dirty ? "base" : "Asource";
    bool disk_ok = disk_len == (ssize_t)strlen(expected_disk) && !memcmp(disk, expected_disk, strlen(expected_disk));
    bool paths_ok = dirty || (tabs_count(&e->tabs) == 2 && tabs_active_index(&e->tabs) == 0 &&
        editor_tab(e, 0)->path_len == editor_tab(e, 1)->path_len &&
        !memcmp(editor_tab(e, 0)->path, editor_tab(e, 1)->path, editor_tab(e, 0)->path_len));
    bool ok = tabs_count(&e->tabs) == 2 && owner_ok && peer_ok && paths_ok && disk_ok && !aside && !e->recovery_notice &&
        (dirty ? result.refused && result.untouched : !result.refused && result.peer_clean);
    editor_close(e); remove_tree(root); T(ok); return 0;
}

/* ---- edit-fi2: recovery survives an external rewrite of the original ----
 * (P) Fixture: 2 * FILE_PREFIX_MAX + 7 bytes, above the synchronous prefix,
 * so edit-28u checkpoints reference BASE instead of holding the text (the
 * case the bead is about). Bytes are a 26-letter cycle; the rewrite uses a
 * different cycle so original and rewritten bytes differ at every offset. */
#define FI2_BYTES (2u * FILE_PREFIX_MAX + 7u)
static uint8_t fi2_byte(size_t i, char first) { return (uint8_t)(first + (char)(i % 26u)); }
static int fi2_fill(const char *path, size_t n, char first)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    uint8_t chunk[JOURNAL_INSERT_SLICE_BYTES]; /* reuse the wire slice as an I/O chunk */
    for (size_t off = 0; off < n;) {
        size_t k = n - off < sizeof chunk ? n - off : sizeof chunk;
        for (size_t i = 0; i < k; i++) chunk[i] = fi2_byte(off + i, first);
        T(write(fd, chunk, k) == (ssize_t)k); off += k;
    }
    T(close(fd) == 0); return 0;
}
static bool fi2_retained(const char *root)
{
    DIR *dir = opendir(root); if (!dir) return false;
    struct dirent *entry; bool found = false;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, ".sublimite-base-", strlen(".sublimite-base-"))) found = true;
    closedir(dir); return found;
}
/* edit-7cj8 (P): the first byte above the existing retention bound exercises
 * its excluded generation. Sparse holes are original zero bytes; boundary
 * sentinels distinguish the old mapping from the renamed-in disk text. */
typedef struct detached_hold { atomic_bool entered, release; } detached_hold;
static ssize_t detached_hold_write(void *ctx, int fd, const uint8_t *bytes, size_t n, uint64_t off)
{
    detached_hold *hold = ctx;
    atomic_store(&hold->entered, true);
    while (!atomic_load(&hold->release)) sched_yield();
    return pwrite(fd, bytes, n, (off_t)off);
}
static int detached_child(const char *source, const char *other, const char *jpath, int ready, bool window)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = source, .journal_path = jpath, .cols = COLS, .rows = ROWS,
        .closed_capacity = window ? sizeof "x" - 1u : 0, /* (P) minimal eviction history, or default */
        /* (P) map every nonempty original, or force private COPY for the
         * in-place rewrite while the retained generation is not READY. */
        .copy_threshold = window ? UINT64_MAX : sizeof "x" - 1u};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t limit = deadline();
    while (e->buffer->pending_open) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0);
    detached_hold hold = {0};
    if (window) {
        e->retain_limit = 0; /* (P) no copy until the edit is durable */
        /* (P) In the binary session graph, source + these literal-named
         * leaves cross a sibling branch. Evicting its only leaf leaves an
         * empty internal branch that validation must skip safely. */
        const char *temporary[] = {"branch", "evicted"};
        for (size_t i = 0; i < sizeof temporary / sizeof *temporary; i++) {
            uint64_t id;
            T(editor_add_buffer(e, NULL, (const uint8_t *)temporary[i], strlen(temporary[i]), &id) == 0);
            T(settle(e) == 0);
        }
        for (size_t i = 0; i < sizeof temporary / sizeof *temporary; i++) {
            T(editor_close_tab(e, tabs_count(&e->tabs) - 1u) == 0); T(settle(e) == 0);
        }
        T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    }
    T(press(e, 'X', 0, "XY") == 0); T(editor_flush(e) == 0);
    if (window) {
        journal_io io = {.ctx = &hold, .write = detached_hold_write};
        T(journal_set_io(e->journal, &io) == 0);
        e->retain_limit = JOURNAL_STARTUP_SCAN_BYTES;
        limit = deadline();
        while (!atomic_load(&hold.entered)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
        T(e->buffer->retain_state == EDITOR_RETAIN_RUNNING && !e->buffer->retain_used);
        T(write_file(source, "external\n") == 0);
        atomic_store(&hold.release, true); /* old BASE fails the copy's validation */
    } else {
        T(write_file(other, "external\n") == 0); T(rename(other, source) == 0);
    }
    T(file_check(e->buffer->file, NULL));
    limit = deadline();
    while (*e->buffer->base.path && trace_now_ns() < limit) { T(turn(e) == 0); sched_yield(); }
    fprintf(stderr, "dataloss_test: edit-7cj8 %s checkpoint=%s\n", window ? "copy window" : "detached",
        *e->buffer->base.path ? "missing" : "durable");
    T(write(ready, "r", sizeof "r" - 1u) == (ssize_t)(sizeof "r" - 1u));
    for (;;) pause();
}
typedef struct detached_fixture {
    editor *e;
    render_backend backend;
    pid_t child;
    char journal[IPC_PATH_CAP]; /* config borrows this until wrapper teardown */
} detached_fixture;
static int detached_body(const char *root, detached_fixture *f, bool window)
{
    char source[IPC_PATH_CAP], other[IPC_PATH_CAP], first[IPC_PATH_CAP];
    T(snprintf(source, sizeof source, "%s/source", root) > 0);
    T(snprintf(other, sizeof other, "%s/rewrite", root) > 0);
    const uint64_t bytes = window ? FI2_BYTES : JOURNAL_STARTUP_SCAN_BYTES + (sizeof "x" - 1u);
    int fd = open(source, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600); T(fd >= 0);
    bool sized = ftruncate(fd, (off_t)bytes) == 0;
    bool head = pwrite(fd, "original\n", sizeof "original\n" - 1u, 0) == (ssize_t)(sizeof "original\n" - 1u);
    bool tail = pwrite(fd, "tail\n", sizeof "tail\n" - 1u, (off_t)(bytes - (sizeof "tail\n" - 1u))) == (ssize_t)(sizeof "tail\n" - 1u);
    T(close(fd) == 0); T(sized && head && tail);
    T(make_session(root, first, sizeof first) == 0);
    int pipes[2]; T(pipe(pipes) == 0); f->child = fork(); T(f->child >= 0);
    if (!f->child) { close(pipes[0]); _exit(detached_child(source, other, first, pipes[1], window)); }
    close(pipes[1]); char ready; ssize_t got = read(pipes[0], &ready, sizeof ready); close(pipes[0]);
    T(kill(f->child, SIGKILL) == 0); T(killed_by(f->child, SIGKILL) == 0); f->child = 0;
    T(got == (ssize_t)sizeof ready);
    T(make_session(root, f->journal, sizeof f->journal) == 0);
    T(open_editor(&f->e, &f->backend, NULL, f->journal) == 0);
    editor *e = f->e;
    if (!tabs_count(&e->tabs) && tabs_closed_count(&e->tabs)) {
        T(editor_reopen_tab(e) == 0);
        uint64_t limit = deadline();
        while (e->restore_active || e->buffer->placeholder || e->buffer->pending_open) {
            T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield();
        }
        T(settle(e) == 0);
    }
    fprintf(stderr, "dataloss_test: edit-7cj8 %s recovered original=%s modified=%d\n", window ? "copy window" : "detached",
        editor_length(e) == bytes + (sizeof "XY" - 1u) ? "complete" : "lost", editor_tab(e, 0)->modified);
    T(editor_length(e) == bytes + (sizeof "XY" - 1u));
    uint8_t chunk[JOURNAL_INSERT_SLICE_BYTES];
    T(editor_read(e, 0, chunk, sizeof "XYoriginal\n" - 1u) == 0);
    T(!memcmp(chunk, "XYoriginal\n", sizeof "XYoriginal\n" - 1u));
    uint64_t end = bytes - (sizeof "tail\n" - 1u);
    for (uint64_t off = sizeof "original\n" - 1u; off < end;) {
        size_t n = end - off < sizeof chunk ? (size_t)(end - off) : sizeof chunk;
        T(editor_read(e, off + (sizeof "XY" - 1u), chunk, n) == 0);
        for (size_t i = 0; i < n; i++) T(chunk[i] == 0); /* (P) sparse holes */
        off += n;
    }
    T(editor_read(e, end + (sizeof "XY" - 1u), chunk, sizeof "tail\n" - 1u) == 0);
    T(!memcmp(chunk, "tail\n", sizeof "tail\n" - 1u));
    T(editor_tab(e, 0)->modified); T(!*e->recovery_aside); T(disk_is(source, "external\n") == 0);
    return 0;
}
static int detached_rewrite(bool window)
{
    char root[sizeof "build/dataloss-XXXXXX"]; T(make_root(root, sizeof root) == 0);
    detached_fixture f = {0}; int rc = detached_body(root, &f, window);
    if (f.child > 0) { (void)kill(f.child, SIGKILL); (void)waitpid(f.child, NULL, 0); }
    if (f.e) editor_close(f.e);
    remove_tree(root); return rc;
}
/* Child: open, type "XY" at offset 0, then keep turning until the journal
 * names a retained copy (or the idle watchdog expires: the red build never
 * retains), report, and wait to be killed. */
static int rewrite_child(const char *source, const char *root, const char *jpath, int fd)
{
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'X', 0, "XY") == 0);
    uint64_t limit = deadline();
    while (trace_now_ns() < limit && !(fi2_retained(root) && file_contains(jpath, ".sublimite-base-"))) {
        T(turn(e) == 0); sched_yield();
    }
    T(write(fd, "r", 1) == 1);
    for (;;) pause();
}
enum { REWRITE_RENAME, REWRITE_IN_PLACE };
static int external_rewrite(int kind)
{
    char root[64], source[128], other[128], first[128], next[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(fi2_fill(source, FI2_BYTES, 'a') == 0);
    T(snprintf(other, sizeof other, "%s/rewrite", root) > 0);
    T(make_session(root, first, sizeof first) == 0);
    int pipes[2]; T(pipe(pipes) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(pipes[0]); _exit(rewrite_child(source, root, first, pipes[1])); }
    close(pipes[1]); char byte; T(read(pipes[0], &byte, 1) == 1); close(pipes[0]);
    /* The rewrite happens while the editor still lives (git checkout, a
     * build step), then the editor dies without a clean exit. */
    if (kind == REWRITE_RENAME) { T(fi2_fill(other, FI2_BYTES - 1u, 'A') == 0); T(rename(other, source) == 0); }
    else T(fi2_fill(source, FI2_BYTES - 1u, 'A') == 0);
    T(kill(child, SIGKILL) == 0); T(killed_by(child, SIGKILL) == 0);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    uint64_t len = editor_length(e);
    uint8_t head[8] = {0}; T(editor_read(e, 0, head, len < sizeof head ? (size_t)len : sizeof head) == 0);
    fprintf(stderr, "dataloss_test: external_rewrite kind=%s tabs=%zu len=%llu (want %u) head=[%.8s] modified=%d notice=[%s] aside=[%s]\n",
        kind == REWRITE_RENAME ? "rename-over" : "in-place", tabs_count(&e->tabs), (unsigned long long)len, FI2_BYTES + 2u, head,
        tabs_count(&e->tabs) ? editor_tab(e, 0)->modified : 0, e->recovery_notice ? e->recovery_notice : "", e->recovery_aside);
    T(tabs_count(&e->tabs) == 1);
    T(len == FI2_BYTES + 2u); T(!memcmp(head, "XY", 2));
    uint8_t chunk[JOURNAL_INSERT_SLICE_BYTES];
    for (size_t off = 0; off < FI2_BYTES;) {
        size_t k = FI2_BYTES - off < sizeof chunk ? FI2_BYTES - off : sizeof chunk;
        T(editor_read(e, 2u + off, chunk, k) == 0);
        for (size_t i = 0; i < k; i++) if (chunk[i] != fi2_byte(off + i, 'a')) {
            fprintf(stderr, "dataloss_test: external_rewrite: original byte %zu differs\n", off + i); T(false);
        }
        off += k;
    }
    T(editor_tab(e, 0)->modified);
    T(e->recovery_notice && strstr(e->recovery_notice, "changed on disk"));
    T(editor_tab(e, 0)->path_len == strlen(source) && !memcmp(editor_tab(e, 0)->path, source, strlen(source)));
    /* The disk keeps the external bytes; nothing was written to the target. */
    struct stat st; T(stat(source, &st) == 0); T(st.st_size == (off_t)(FI2_BYTES - 1u));
    editor_close(e); remove_tree(root); return 0;
}
/* (c) A clean open writes no retained artifact; a saved buffer's copy is
 * removed once the saved file is BASE. */
static int retain_lifecycle(void)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(fi2_fill(source, FI2_BYTES, 'a') == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, XKB_KEY_Right, 0, NULL) == 0); T(press(e, XKB_KEY_Left, 0, NULL) == 0);
    for (unsigned i = 0; i < 64u; i++) { T(turn(e) == 0); sched_yield(); } /* (P) 64 = WORK_DRAIN_MAX_MESSAGES maintenance turns */
    T(!fi2_retained(root));
    T(press(e, 'q', 0, "q") == 0);
    uint64_t limit = deadline();
    while (!fi2_retained(root) && trace_now_ns() < limit) { T(turn(e) == 0); sched_yield(); }
    fprintf(stderr, "dataloss_test: retain_lifecycle: retained after first edit=%d\n", fi2_retained(root));
    T(fi2_retained(root));
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    limit = deadline();
    while (fi2_retained(root) && trace_now_ns() < limit) { T(turn(e) == 0); sched_yield(); }
    fprintf(stderr, "dataloss_test: retain_lifecycle: retained after save=%d\n", fi2_retained(root));
    T(!fi2_retained(root));
    editor_close(e); T(!fi2_retained(root));
    remove_tree(root); return 0;
}
/* edit-9ev6 (recovery-3 F2): editor_close while a BASE copy is RUNNING. The
 * retention job borrows the journal's IO callbacks (r->io = &j->io); close
 * must quiesce it before the journal (an arena, unmapped on close) and its
 * pool go. The metadata_sync hook (used only by retention) holds the job after
 * its copy until the pool cancels it; the job then reaches sync_parent, which
 * reads r->io. Before the fix that read hit the unmapped journal under ASan. */
typedef struct held_retain {
    atomic_bool entered, armed;
    work_pool *pool; work_handle handle; /* written by the test before armed */
} held_retain;
static int hold_retain_sync(void *ctx, int fd)
{
    held_retain *h = ctx;
    if (!atomic_load(&h->entered)) {
        atomic_store(&h->entered, true);
        while (!atomic_load(&h->armed) ||
               atomic_load(&h->pool->slots[h->handle.slot].epoch) == h->handle.epoch) sched_yield();
    }
    return fsync(fd);
}
static int retain_close(void)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(fi2_fill(source, FI2_BYTES, 'a') == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(journal_flush(e->journal) == 0);
    held_retain held = {0};
    journal_io io = {.ctx = &held, .metadata_sync = hold_retain_sync};
    T(journal_set_io(e->journal, &io) == 0); /* no edit yet: no retention operation */
    /* The journal pool has one worker: a held copy would queue every journal
     * write behind it. Make the edit (and its metadata) durable first, then
     * let retention start: close then finds a durable prefix, so its flush
     * returns without joining the copy (the finding's sequence). */
    e->retain_limit = 0; T(press(e, 'q', 0, "q") == 0); T(editor_flush(e) == 0);
    e->retain_limit = JOURNAL_STARTUP_SCAN_BYTES;
    uint64_t limit = deadline();
    while (!atomic_load(&held.entered)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    editor_buffer *b = e->buffer; T(b->retain_state == EDITOR_RETAIN_RUNNING);
    held.pool = b->retain_pool; held.handle = b->retain_handle; atomic_store(&held.armed, true);
    editor_close(e); /* the job is released only by a cancellation */
    fprintf(stderr, "dataloss_test: retain_close: closed with a held copy, retained after close=%d\n", fi2_retained(root));
    T(!fi2_retained(root)); /* an unreferenced copy is removed (fi2 policy) */
    remove_tree(root); return 0;
}
/* (d) 1 GiB sparse open: time editor_open + settle and a first edit; above
 * the retention bound no copy is made. (P) 2^30 bytes; sparse (ftruncate),
 * removed on every exit path by the wrapper. */
static int open_gib_body(const char *root, const char *source)
{
    char jpath[128]; T(make_session(root, jpath, sizeof jpath) == 0);
    int fd = open(source, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    bool sized = ftruncate(fd, (off_t)1 << 30) == 0; close(fd); T(sized);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = source, .journal_path = jpath, .cols = COLS, .rows = ROWS};
    editor *e = NULL; uint64_t t0 = trace_now_ns();
    T(editor_open(&e, &cfg, &backend) == 0);
    uint64_t t1 = trace_now_ns(); T(settle(e) == 0); uint64_t t2 = trace_now_ns();
    T(press(e, 'q', 0, "q") == 0); uint64_t t3 = trace_now_ns();
    for (unsigned i = 0; i < 64u; i++) { T(turn(e) == 0); sched_yield(); } /* (P) 64 = WORK_DRAIN_MAX_MESSAGES */
    bool copy = fi2_retained(root);
    printf("dataloss_test: open-gib (M) editor_open=%.3f ms settle=%.3f ms first edit=%.3f ms retained_copy=%d\n",
        (double)(t1 - t0) / 1e6, (double)(t2 - t1) / 1e6, (double)(t3 - t2) / 1e6, copy);
    editor_close(e); T(!copy); return 0;
}
/* edit-fi2 measurement: a BASE at the retention bound (e->retain_limit,
 * JOURNAL_STARTUP_SCAN_BYTES), sparse: first edit -> copy READY (worker,
 * includes fsync), and the UI turn that writes the reference checkpoint. */
static int retain_bound_body(const char *root, const char *source)
{
    char jpath[128]; T(make_session(root, jpath, sizeof jpath) == 0);
    int fd = open(source, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    bool sized = ftruncate(fd, (off_t)JOURNAL_STARTUP_SCAN_BYTES) == 0; close(fd); T(sized);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(e->buffer->base.size <= e->retain_limit);
    T(press(e, 'q', 0, "q") == 0);
    uint64_t t0 = trace_now_ns(), ready = 0, turn_max = 0, limit = trace_now_ns() + 6u * EDITOR_IDLE_NS;
    while (!e->buffer->retain_used) {
        uint64_t a = trace_now_ns(); T(turn(e) == 0); uint64_t b = trace_now_ns();
        if (!ready && e->buffer->retain_state == EDITOR_RETAIN_READY) ready = b;
        if (b - a > turn_max) turn_max = b - a;
        T(b < limit); sched_yield();
    }
    printf("dataloss_test: retain-bound (M) base=%llu B first edit->copy ready=%.3f ms longest UI turn (includes the reference checkpoint)=%.3f ms\n",
        (unsigned long long)e->buffer->base.size, (double)(ready - t0) / 1e6, (double)turn_max / 1e6);
    editor_close(e); return 0;
}
static int retain_bound(void)
{
    char root[64], source[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/bound", root) > 0);
    int rc = retain_bound_body(root, source);
    remove_tree(root); return rc;
}
static int open_gib(void)
{
    char root[64], source[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/gib", root) > 0);
    int rc = open_gib_body(root, source);
    remove_tree(root); return rc;
}

/* edit-qzam: this editor's own jobs that hold the session directory guard
 * LOCK_SH (journal.c): the edit-fi2/7cj8 BASE copy (RUNNING) and the worker
 * checkpoint (e->publish, edit-ze59) that references a READY copy or, for an
 * unprotected stale BASE, the edit-7cj8 detached original. Both follow the
 * first edit by themselves; a test that takes the guard LOCK_EX|LOCK_NB to
 * stand in for another editor's sweep waits until neither runs or is owed. */
static bool guard_quiet(const editor *e)
{
    const editor_buffer *b = e->buffer;
    if (e->publish || b->retain_state == EDITOR_RETAIN_RUNNING) return false;
    if (b->retain_state == EDITOR_RETAIN_READY) return b->retain_used;
    return !editor_retain_unprotected(b);
}
static int wait_guard_quiet(editor *e)
{
    uint64_t limit = deadline();
    while (!guard_quiet(e)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    fprintf(stderr, "dataloss_test: edit-qzam own guard holders done: retain_state=%u retain_used=%d\n",
        e->buffer->retain_state, e->buffer->retain_used);
    return 0;
}

/* edit-ivv (d): Reload/Keep chosen while another editor's directory sweep
 * holds the shared guard LOCK_EX (journal_test --rotate-busy pattern): the
 * choice survives the refused checkpoint and lands on a later journal tick.
 * (P) zero = no refusal/empty; (E) watchdog EDITOR_IDLE_NS as settle. */
static int ivv_choice(bool reload)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'x', 0, "x") == 0); T(stale_prompt(e, source, "external") == 0);
    T(wait_guard_quiet(e) == 0); /* edit-qzam: no own job holds or is owed the guard */
    int guard = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC); T(guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0);
    T(press(e, reload ? 'r' : 'k', 0, NULL) == 0);
    uint64_t limit = deadline();
    while (e->source_choice && !e->busy_refusals && trace_now_ns() < limit) { T(turn(e) == 0); sched_yield(); }
    T(turn(e) == 0);
    fprintf(stderr, "dataloss_test: edit-ivv %s with guard held: refusals=%u choice=%u stale=%d\n",
        reload ? "reload" : "keep", e->busy_refusals, e->source_choice, e->buffer->source_stale);
    T(e->busy_refusals > 0 && e->source_choice && e->buffer->source_stale);
    T(!e->recovery_notice || !strstr(e->recovery_notice, "busy")); /* within the first tick: silent */
    if (!reload) {
        /* (P) a second consecutive refusal has outlasted the one-tick bound. */
        limit = deadline();
        while (e->busy_refusals < 2u) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
        fprintf(stderr, "dataloss_test: edit-ivv keep past the bound: refusals=%u notice=%s\n", e->busy_refusals,
            e->recovery_notice ? e->recovery_notice : "(none)");
        T(e->recovery_notice && strstr(e->recovery_notice, "busy") && e->source_choice && e->buffer->source_stale);
    }
    T(close(guard) == 0);
    limit = deadline();
    while (e->buffer->source_stale) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0); T(!e->busy_refusals);
    if (reload) { T(text_is(e, "external") == 0); T(!editor_tab(e, 0)->modified); }
    else { T(text_is(e, "xbase") == 0); T(editor_tab(e, 0)->modified); }
    fprintf(stderr, "dataloss_test: edit-ivv %s landed after the guard was released\n", reload ? "reload" : "keep");
    editor_close(e); remove_tree(root); return 0;
}

/* edit-ze59 (recovery-3 F10): retention publication and Save As do their
 * filesystem transactions off the UI thread. (G) G1 typing p99 = 2 ms,
 * perf/01-perf-target.md §0.2; enforced on release builds only, where the
 * malloc guard is active (the editor_large_test latency convention), and
 * printed everywhere. (E) watchdogs reuse EDITOR_IDLE_NS. */
#define ZE59_G1_P99_NS UINT64_C(2000000)
typedef struct ze59_stall {
    _Atomic bool hold, timed_out, done;
    _Atomic unsigned blocked;
    pthread_mutex_t mutex; pthread_cond_t cond;
} ze59_stall;
/* Journal data barriers (fdatasync: the journal worker's batch sync and the
 * rotation temp) wait while held; namespace barriers do not, so the BASE copy
 * (metadata_sync + directory sync) still completes. */
static int ze59_sync(void *ctx, int fd, bool directory)
{
    ze59_stall *s = ctx;
    if (!directory && atomic_load(&s->hold)) {
        atomic_fetch_add(&s->blocked, 1u);
        while (atomic_load(&s->hold)) sched_yield();
    }
    int rc; do { rc = directory ? fsync(fd) : fdatasync(fd); } while (rc < 0 && errno == EINTR);
    return rc;
}
static void *ze59_watchdog(void *ctx)
{
    ze59_stall *s = ctx;
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until);
    /* (P) ns per s split for timespec. */
    uint64_t ns = (uint64_t)until.tv_nsec + EDITOR_IDLE_NS;
    until.tv_sec += (time_t)(ns / UINT64_C(1000000000)); until.tv_nsec = (long)(ns % UINT64_C(1000000000));
    pthread_mutex_lock(&s->mutex);
    while (!atomic_load(&s->done))
        if (pthread_cond_timedwait(&s->cond, &s->mutex, &until)) break;
    pthread_mutex_unlock(&s->mutex);
    if (!atomic_load(&s->done) && atomic_load(&s->hold)) { atomic_store(&s->timed_out, true); atomic_store(&s->hold, false); }
    return NULL;
}
static bool ze59_has(editor *e, uint8_t ch)
{
    /* The fixtures type at offset 0: the leading TEXT_CAP bytes suffice. */
    uint8_t bytes[TEXT_CAP]; uint64_t len = editor_length(e);
    if (len > sizeof bytes) len = sizeof bytes;
    if (editor_read(e, 0, bytes, (size_t)len)) return false;
    return memchr(bytes, ch, (size_t)len) != NULL;
}
static void ze59_stamp(void)
{
    char power[32] = "unknown"; FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(power, sizeof power, f)) power[strcspn(power, "\n")] = '\0'; fclose(f); }
    double load = 0; f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", &load) != 1) load = 0; fclose(f); }
    printf(" [power=%s load1=%.2f build=%s]\n", power, load, edit_malloc_guard_active() ? "release" : "sanitized");
}
static int ze59_retain_stall_body(const char *root, ze59_stall *stall, pthread_t *watchdog, bool *started)
{
    char source[128], jpath[128];
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base text") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(journal_flush(e->journal) == 0);
    journal_io io = {.ctx = stall, .sync = ze59_sync}; T(journal_set_io(e->journal, &io) == 0);
    atomic_store(&stall->hold, true);
    T(pthread_create(watchdog, NULL, ze59_watchdog, stall) == 0); *started = true;
    plat_event x = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'X', .t0_ns = trace_now_ns(), .utf8_len = 1, .utf8 = "X"};
    T(editor_inject(e, &x) == 0);
    /* Retention becomes READY (the copy's barriers are not held); the next
     * journal sync then stalls the journal worker. */
    uint64_t limit = deadline(), turn_max = 0;
    while (e->buffer->retain_state != EDITOR_RETAIN_READY || !atomic_load(&stall->blocked)) {
        uint64_t a = trace_now_ns(); T(turn(e) == 0); uint64_t b = trace_now_ns();
        if (b - a > turn_max) turn_max = b - a;
        T(b < limit || atomic_load(&stall->timed_out)); if (atomic_load(&stall->timed_out)) break;
        sched_yield();
    }
    printf("dataloss_test: edit-ze59 stall: retain_state=%u retain_used=%d blocked=%u longest UI turn=%.3f ms timed_out=%d",
        e->buffer->retain_state, e->buffer->retain_used, atomic_load(&stall->blocked), (double)turn_max / 1e6,
        atomic_load(&stall->timed_out)); ze59_stamp();
    T(!atomic_load(&stall->timed_out) && atomic_load(&stall->hold));
    /* A keystroke while the journal worker is stalled and retention is READY. */
    plat_event y = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'Y', .t0_ns = trace_now_ns(), .utf8_len = 1, .utf8 = "Y"};
    T(editor_inject(e, &y) == 0);
    limit = deadline();
    while (!ze59_has(e, 'Y') || e->dirty || e->queue_count) {
        T(turn(e) == 0); T(trace_now_ns() < limit); if (atomic_load(&stall->timed_out)) break;
        sched_yield();
    }
    uint64_t latency = e->active_frame.submit_ns - y.t0_ns;
    printf("dataloss_test: edit-ze59 keystroke under stalled journal worker + READY retention: input->submit=%.3f ms (M) "
           "bound=%.3f ms (G1 p99) held=%d timed_out=%d", (double)latency / 1e6, (double)ZE59_G1_P99_NS / 1e6,
           atomic_load(&stall->hold), atomic_load(&stall->timed_out)); ze59_stamp();
    T(!atomic_load(&stall->timed_out) && atomic_load(&stall->hold));
    if (edit_malloc_guard_active()) T(latency <= ZE59_G1_P99_NS);
    T(!e->buffer->retain_used); /* still unpublished: the journal is not quiescent */
    atomic_store(&stall->hold, false);
    limit = deadline();
    while (!e->buffer->retain_used) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0); T(text_is(e, "XYbase text") == 0);
    printf("dataloss_test: edit-ze59 retention published off the UI thread after the stall released\n");
    editor_close(e); return 0;
}
static int ze59_retain_stall(void)
{
    char root[64]; T(make_root(root, sizeof root) == 0);
    ze59_stall stall = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    pthread_t watchdog; bool started = false;
    int rc = ze59_retain_stall_body(root, &stall, &watchdog, &started);
    atomic_store(&stall.hold, false);
    if (started) {
        pthread_mutex_lock(&stall.mutex); atomic_store(&stall.done, true); pthread_cond_signal(&stall.cond);
        pthread_mutex_unlock(&stall.mutex); pthread_join(watchdog, NULL);
    }
    remove_tree(root); return rc;
}
/* edit-y315: a Save As checkpoint still owed at exit is written by the worker
 * publication (editor_publish_start), and the exit path waits for it at most
 * one journal tick (JOURNAL_DEFAULT_SYNC_NS, the tick editor_flush_settle
 * already allows for a BUSY retry). Past that the journal stays for recovery.
 * (G) The UI bound below is that deadline tick plus the flush_settle retry
 * tick (edit-ivv): 2 * JOURNAL_DEFAULT_SYNC_NS. (E) The stall's watchdog reuses
 * EDITOR_IDLE_NS (ze59). (P) Text lengths come from fixture literals; counters
 * count observed rename callbacks (one per journal rotation). */
#define Y315_EXIT_BOUND_NS (2u * JOURNAL_DEFAULT_SYNC_NS)
typedef struct y315_io {
    pthread_t ui;
    _Atomic unsigned ui_rotations, worker_rotations;
    ze59_stall stall;
} y315_io;
typedef struct y315_result {
    uint64_t ui_ns;
    int flushed;
    unsigned ui_rotations, worker_rotations, blocked;
    bool timed_out;
} y315_result;
/* Journal rotations only (rename is the rotation's publish step); held while
 * the stall is armed, so a rotation started on any thread waits. */
static int y315_rename(void *ctx, const char *from, const char *to)
{
    y315_io *io = ctx;
    if (pthread_equal(pthread_self(), io->ui)) atomic_fetch_add(&io->ui_rotations, 1u);
    else atomic_fetch_add(&io->worker_rotations, 1u);
    if (atomic_load(&io->stall.hold)) {
        atomic_fetch_add(&io->stall.blocked, 1u);
        while (atomic_load(&io->stall.hold)) sched_yield();
    }
    return rename(from, to);
}
/* Exit while a Save As write is in flight (main's sequence after quit): the
 * exit flush adopts it (rebind, checkpoint owed) and owes the checkpoint;
 * no maintenance turn runs in between to start the publication. */
static int y315_owed(const char *root, y315_io *io, char *target, char *jpath, editor **out, render_backend *backend)
{
    char absolute[IPC_PATH_CAP], source[IPC_PATH_CAP];
    T(realpath(root, absolute));
    T(snprintf(source, sizeof source, "%s/source", absolute) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, IPC_PATH_CAP, "%s/target", absolute) > 0);
    T(make_session(root, jpath, IPC_PATH_CAP) == 0); /* the editor keeps the path: caller storage */
    T(open_editor(out, backend, source, jpath) == 0);
    editor *e = *out; e->retain_limit = 0;
    T(press(e, 'A', 0, "A") == 0); T(editor_flush(e) == 0);
    journal_io hooks = {.ctx = io, .rename = y315_rename}; T(journal_set_io(e->journal, &hooks) == 0);
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) { char ch[] = {target[i], '\0'}; T(press(e, (uint8_t)target[i], 0, ch) == 0); }
    plat_event enter = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Return, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &enter) == 0);
    uint64_t limit = deadline();
    do { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); } while (!e->save_as);
    T(!e->publish && !e->publish_owed);
    return 0;
}
static int y315_stall_child(const char *root, int fd)
{
    y315_io io = {.ui = pthread_self(), .stall = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER}};
    char target[IPC_PATH_CAP], jpath[IPC_PATH_CAP]; render_backend backend; editor *e = NULL;
    T(y315_owed(root, &io, target, jpath, &e, &backend) == 0);
    atomic_store(&io.stall.hold, true);
    pthread_t watchdog; T(pthread_create(&watchdog, NULL, ze59_watchdog, &io.stall) == 0);
    /* main's exit sequence: the final flush, then teardown. */
    uint64_t t0 = trace_now_ns();
    int flushed = editor_flush_settle(e);
    editor_close(e);
    y315_result r = {.ui_ns = trace_now_ns() - t0, .flushed = flushed, .ui_rotations = atomic_load(&io.ui_rotations),
        .worker_rotations = atomic_load(&io.worker_rotations), .blocked = atomic_load(&io.stall.blocked),
        .timed_out = atomic_load(&io.stall.timed_out)};
    T(write(fd, &r, sizeof r) == (ssize_t)sizeof r);
    return 0; /* _exit: a rotation still held dies with the process */
}
static int y315_stall(void)
{
    char root[IPC_PATH_CAP], next[IPC_PATH_CAP], target[IPC_PATH_CAP], absolute[IPC_PATH_CAP];
    T(make_root(root, sizeof root) == 0); T(realpath(root, absolute));
    T(snprintf(target, sizeof target, "%s/target", absolute) > 0);
    int report[2]; T(pipe(report) == 0); pid_t child = fork(); T(child >= 0);
    if (!child) { close(report[0]); _exit(y315_stall_child(root, report[1])); }
    close(report[1]); y315_result r = {0}; size_t received = 0;
    while (received < sizeof r) {
        ssize_t n = read(report[0], (uint8_t *)&r + received, sizeof r - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        received += (size_t)n;
    }
    close(report[0]); T(exited_with(child, 0) == 0); T(received == sizeof r);
    printf("dataloss_test: edit-y315 owed Save As checkpoint at exit, stalled rotation: UI exit=%.3f ms bound=%.3f ms (G) "
           "flush=%d UI rotations=%u worker rotations=%u held=%u timed_out=%d", (double)r.ui_ns / 1e6,
           (double)Y315_EXIT_BOUND_NS / 1e6, r.flushed, r.ui_rotations, r.worker_rotations, r.blocked, r.timed_out);
    ze59_stamp();
    T(r.ui_ns <= Y315_EXIT_BOUND_NS && !r.timed_out);
    T(r.ui_rotations == 0 && r.worker_rotations > 0 && r.flushed == EDITOR_MORE);
    /* The kept journal still names the former target (ze59: the text
     * recovers, modified); the new target holds the written bytes. */
    T(disk_is(target, "Asource") == 0);
    T(make_session(root, next, sizeof next) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, NULL, next) == 0);
    printf("dataloss_test: edit-y315 recovered %zu tab(s): %s modified=%d\n", tabs_count(&e->tabs),
        tabs_count(&e->tabs) ? e->buffer->path : "", tabs_count(&e->tabs) ? editor_tab(e, 0)->modified : 0);
    int ok = tabs_count(&e->tabs) == 1 && !text_is(e, "Asource") && editor_tab(e, 0)->modified;
    editor_close(e); remove_tree(root); T(ok); return 0;
}
static int y315_healthy(void)
{
    char root[IPC_PATH_CAP], target[IPC_PATH_CAP], jpath[IPC_PATH_CAP]; T(make_root(root, sizeof root) == 0);
    y315_io io = {.ui = pthread_self(), .stall = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER}};
    render_backend backend; editor *e = NULL;
    int rc = y315_owed(root, &io, target, jpath, &e, &backend);
    int flushed = rc ? 0 : editor_flush_settle(e);
    bool owed = e && e->publish_owed, rebound = e && !strcmp(e->buffer->path, target);
    if (e) editor_close(e);
    remove_tree(root); T(rc == 0);
    printf("dataloss_test: edit-y315 owed Save As checkpoint at exit, healthy journal: flush=%d owed=%d UI rotations=%u worker rotations=%u\n",
        flushed, owed, atomic_load(&io.ui_rotations), atomic_load(&io.worker_rotations));
    T(flushed == 0 && !owed && rebound && atomic_load(&io.ui_rotations) == 0 && atomic_load(&io.worker_rotations) > 0);
    return 0;
}
/* (G) the bead's fixture: a 100 MB file = 100 * 1000 * 1000 B, sparse. */
#define ZE59_SAVE_AS_BYTES (UINT64_C(100) * 1000u * 1000u)
static int ze59_save_as_body(const char *root)
{
    char source[160], target[PATH_MAX], jpath[128], absolute[PATH_MAX];
    T(realpath(root, absolute)); T(snprintf(source, sizeof source, "%s/large", root) > 0);
    T(snprintf(target, sizeof target, "%s/copy", absolute) > 0);
    int fd = open(source, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); T(fd >= 0);
    bool sized = ftruncate(fd, (off_t)ZE59_SAVE_AS_BYTES) == 0; close(fd); T(sized);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) {
        plat_event ch = {.kind = PLAT_EV_KEY, .press = true, .keysym = (uint8_t)target[i], .t0_ns = trace_now_ns(), .utf8_len = 1};
        ch.utf8[0] = (uint8_t)target[i]; T(editor_inject(e, &ch) == 0);
    }
    T(settle(e) == 0);
    plat_event enter = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Return, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &enter) == 0);
    plat_event z = {.kind = PLAT_EV_KEY, .press = true, .keysym = 'Z', .t0_ns = trace_now_ns(), .utf8_len = 1, .utf8 = "Z"};
    T(editor_inject(e, &z) == 0);
    uint64_t limit = trace_now_ns() + 6u * EDITOR_IDLE_NS, landed = 0, saved = 0, turn_max = 0;
    while (!landed || !saved) {
        uint64_t a = trace_now_ns(); T(turn(e) == 0); uint64_t b = trace_now_ns();
        if (b - a > turn_max) turn_max = b - a;
        /* Text holds Z and no frame is owed: the last submission contains it. */
        if (!landed && ze59_has(e, 'Z') && !e->dirty && !e->queue_count) landed = e->active_frame.submit_ns;
        if (!saved && !strcmp(e->buffer->path, target)) saved = b;
        if (b >= limit) fprintf(stderr, "dataloss_test: edit-ze59 save-as stuck: landed=%d saved=%d path=%s notice=%s go=%d\n",
            landed != 0, saved != 0, e->buffer->path, e->recovery_notice ? e->recovery_notice : "(none)", e->go.active);
        T(b < limit); sched_yield();
    }
    T(settle(e) == 0);
    struct stat st; T(stat(target, &st) == 0);
    printf("dataloss_test: edit-ze59 Save As %llu B sparse: Z input->submit=%.3f ms command->rebound=%.3f ms longest UI turn=%.3f ms "
           "(M) G1 p99=%.3f ms written=%lld B", (unsigned long long)ZE59_SAVE_AS_BYTES, (double)(landed - z.t0_ns) / 1e6,
           (double)(saved - enter.t0_ns) / 1e6, (double)turn_max / 1e6, (double)ZE59_G1_P99_NS / 1e6, (long long)st.st_size);
    ze59_stamp();
    T(landed < saved); /* the keystroke landed while the target was still being written */
    T((uint64_t)st.st_size == ZE59_SAVE_AS_BYTES); /* the snapshot at the command, before Z */
    T(editor_tab(e, 0)->modified); /* Z follows the saved cutoff */
    /* Z modified the rebound buffer, so a copy of its new 100 MB BASE is
     * retained; it finishes and is published before teardown (a copy still
     * running at editor_close outlives the journal: recovery-3 shutdown
     * finding, outside this bead). */
    T(retain_settled(e) == 0); T(e->buffer->retain_used);
    editor_close(e); return 0;
}
static int ze59_save_as(void)
{
    char root[64]; T(make_root(root, sizeof root) == 0);
    int rc = ze59_save_as_body(root);
    remove_tree(root); return rc;
}
/* edit-e3z5 (a): Save As onto a path open in another (clean) tab while the
 * shared directory guard is held. The write lands; the worker publication
 * (which carries the peer's reload since edit-9fzc) is refused (JOURNAL_BUSY)
 * and retried on the journal tick instead of reporting "save as failed".
 * peer=false has no peer. (P) zero = no refusal; (E) watchdog EDITOR_IDLE_NS. */
static const editor_buffer *e3z5_buffer(const editor *e, uint64_t id)
{
    for (size_t i = 0; i < e->buffer_capacity; i++)
        if (e->buffers[i] && !e->buffers[i]->retired && e->buffers[i]->id == id) return e->buffers[i];
    return NULL;
}
static bool e3z5_tab_holds(const editor *e, size_t index, const char *expected)
{
    const tabs_tab *tab = editor_tab(e, index); uint8_t bytes[TEXT_CAP];
    uint64_t len = tab ? piece_len(tab->buffer) : 0;
    return tab && len == strlen(expected) && !piece_read(tab->buffer, 0, bytes, (size_t)len) && !memcmp(bytes, expected, (size_t)len);
}
static int e3z5_tab_is(const editor *e, size_t index, const char *expected)
{
    const tabs_tab *tab = editor_tab(e, index); T(tab);
    char bytes[TEXT_CAP]; uint64_t len = piece_len(tab->buffer); T(len < sizeof bytes);
    T(piece_read(tab->buffer, 0, (uint8_t *)bytes, (size_t)len) == 0);
    if (len != strlen(expected) || memcmp(bytes, expected, (size_t)len)) {
        fprintf(stderr, "dataloss_test: tab %zu holds [%.*s], expected [%s]\n", index, (int)len, bytes, expected);
        T(false);
    }
    return 0;
}
static int e3z5_save_as(bool peer)
{
    char root[IPC_PATH_CAP], source[IPC_PATH_CAP], target[IPC_PATH_CAP], jpath[IPC_PATH_CAP], canonical[IPC_PATH_CAP];
    T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, sizeof target, "%s/target", root) > 0); T(write_file(target, "base") == 0);
    T(realpath(source, canonical)); strcpy(source, canonical);
    T(realpath(target, canonical)); strcpy(target, canonical);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'A', 0, "A") == 0);
    uint64_t peer_id = 0;
    if (peer) { T(editor_add_buffer(e, target, NULL, 0, &peer_id) == 0); T(settle(e) == 0); }
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(retain_settled(e) == 0); T(journal_flush(e->journal) == 0);
    int guard = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC); T(guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0);
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) { char ch[] = {target[i], '\0'}; T(press(e, (uint8_t)target[i], 0, ch) == 0); }
    /* No settle: an owed publication is pending work until the guard frees. */
    plat_event enter = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Return, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &enter) == 0);
    uint64_t limit = deadline();
    /* edit-ze59: the write runs on a worker and editor_save_as_tick adopts it
     * (peer reloads there); the command's checkpoint is the worker
     * publication (publish_owed), whose refusal is adopted later still. */
    while (e->file_command != KEYS_ACTION_NONE || e->save_as || !e->busy_refusals)
    { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    const editor_buffer *other = peer ? e3z5_buffer(e, peer_id) : NULL;
    fprintf(stderr, "dataloss_test: edit-e3z5 save_as(%s) with guard held: notice=%s refusals=%u owed=%d path_moved=%d owner_modified=%d peer_stale=%d peer_modified=%d\n",
        peer ? "peer" : "alone", e->recovery_notice ? e->recovery_notice : "(none)", e->busy_refusals, e->publish_owed,
        !strcmp(e->buffer->path, target), editor_tab(e, 0)->modified, other ? other->source_stale : -1,
        peer ? editor_tab(e, 1)->modified : -1);
    T(!e->recovery_notice); /* within the first tick a refusal is silent; never "save as failed" */
    T(disk_is(target, "Asource") == 0);
    T(e->busy_refusals == 1u && !strcmp(e->buffer->path, target) && !editor_tab(e, 0)->modified);
    /* edit-9fzc: the clean peer took the written bytes; its checkpoint is the owed publication. */
    if (peer) { T(other && !other->source_stale && !editor_tab(e, 1)->modified); T(e3z5_tab_is(e, 1, "Asource") == 0); }
    T(e->publish_owed && !e->busy_checkpoint); /* the worker publication stays owed */
    T(close(guard) == 0);
    limit = deadline();
    while (e->busy_refusals || e->publish_owed || e->publish || (peer && !e3z5_tab_holds(e, 1, "Asource")))
    { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0);
    T(!e->recovery_notice && !e->busy_refusals && !e->publish_owed && !e->busy_checkpoint && !editor_tab(e, 0)->modified);
    if (peer) {
        other = e3z5_buffer(e, peer_id);
        T(other && !other->source_stale && !editor_tab(e, 1)->modified); T(e3z5_tab_is(e, 1, "Asource") == 0);
    }
    fprintf(stderr, "dataloss_test: edit-e3z5 save_as(%s) landed after the guard was released\n", peer ? "peer" : "alone");
    editor_close(e); remove_tree(root); return 0;
}
/* edit-e3z5 (b): an explicit Reload with every buffer slot taken. (P) Slots =
 * tab_capacity + closed_capacity = 2 + 1: the stale tab, a retained closed tab
 * and a second live tab fill them. The Reload is not silently dropped: a
 * notice says why and the prompt stays, so R works once a slot frees. */
static int e3z5_no_slot(void)
{
    char root[64], source[128], other[128], third[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0); T(write_file(other, "other") == 0);
    T(snprintf(third, sizeof third, "%s/third", root) > 0); T(write_file(third, "third") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = source, .journal_path = jpath, .cols = COLS, .rows = ROWS,
        .tab_capacity = 2, .closed_capacity = 1};
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    uint64_t id;
    T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
    T(editor_close_tab(e, 1) == 0); T(settle(e) == 0);
    T(editor_add_buffer(e, third, NULL, 0, &id) == 0); T(settle(e) == 0);
    size_t used = 0; for (size_t i = 0; i < e->buffer_capacity; i++) used += e->buffers[i] != NULL;
    T(used == e->buffer_capacity);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    T(press(e, 'x', 0, "x") == 0); T(stale_prompt(e, source, "external") == 0);
    T(choose(e, true) == 0);
    fprintf(stderr, "dataloss_test: edit-e3z5 reload with no free slot: notice=%s stale=%d choice=%u\n",
        e->recovery_notice ? e->recovery_notice : "(none)", e->buffer->source_stale, e->source_choice);
    T(e->recovery_notice && strstr(e->recovery_notice, "reload") && e->buffer->source_stale);
    T(text_is(e, "xbase") == 0);
    /* Closing the live tab evicts the retained one (closed capacity 1), whose
     * slot maintenance frees; the prompt is still there and R now lands. */
    T(editor_close_tab(e, 1) == 0); T(settle(e) == 0);
    T(e->buffer->source_stale); T(choose(e, true) == 0);
    fprintf(stderr, "dataloss_test: edit-e3z5 reload after a slot freed: notice=%s stale=%d\n",
        e->recovery_notice ? e->recovery_notice : "(none)", e->buffer->source_stale);
    T(!e->recovery_notice && !e->buffer->source_stale); T(text_is(e, "external") == 0); T(!editor_tab(e, 0)->modified);
    editor_close(e); remove_tree(root); return 0;
}

/* edit-9fzc: (P) the tabs are the Save As owner and its clean target peer;
 * lengths come from fixture literals, indexes from that insertion order.
 * Zero is the API success/empty sentinel; counters count observed callbacks.
 * The watchdog reuses EDITOR_IDLE_NS (E), with no new timing bound. */
typedef struct followup_io {
    pthread_t ui;
    _Atomic unsigned ui_rotations, worker_rotations;
    _Atomic bool hold;
} followup_io;
typedef struct followup_fixture {
    editor *e;
    render_backend backend;
    followup_io io;
    char source[IPC_PATH_CAP], target[IPC_PATH_CAP], journal[IPC_PATH_CAP];
} followup_fixture;
static int followup_rename(void *ctx, const char *from, const char *to)
{
    followup_io *io = ctx;
    if (pthread_equal(pthread_self(), io->ui)) atomic_fetch_add(&io->ui_rotations, 1u);
    else atomic_fetch_add(&io->worker_rotations, 1u);
    return rename(from, to);
}
static int followup_peer_body(const char *root, followup_fixture *fixture)
{
    char *source = fixture->source, *target = fixture->target, *jpath = fixture->journal;
    followup_io *io = &fixture->io;
    char absolute[IPC_PATH_CAP];
    T(realpath(root, absolute));
    T(snprintf(source, IPC_PATH_CAP, "%s/source", absolute) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, IPC_PATH_CAP, "%s/target", absolute) > 0); T(write_file(target, "base") == 0);
    T(make_session(root, jpath, IPC_PATH_CAP) == 0);
    T(open_editor(&fixture->e, &fixture->backend, source, jpath) == 0);
    editor *e = fixture->e; e->retain_limit = 0;
    T(press(e, 'A', 0, "A") == 0);
    uint64_t peer_id; T(editor_add_buffer(e, target, NULL, 0, &peer_id) == 0); T(settle(e) == 0);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    journal_io hooks = {.ctx = io, .rename = followup_rename};
    T(journal_set_io(e->journal, &hooks) == 0);
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) {
        char ch[] = {target[i], '\0'}; T(press(e, (uint8_t)target[i], 0, ch) == 0);
    }
    T(press(e, XKB_KEY_Return, 0, NULL) == 0);
    uint64_t limit = deadline();
    while (e->save_as || e->publish || e->publish_owed) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0); T(disk_is(target, "Asource") == 0); T(text_is(e, "Asource") == 0);
    const tabs_tab *peer = editor_tab(e, 1);
    uint8_t bytes[sizeof "Asource" - 1u];
    T(peer && peer->id == peer_id && !peer->modified && piece_len(peer->buffer) == sizeof bytes);
    T(piece_read(peer->buffer, 0, bytes, sizeof bytes) == 0 && !memcmp(bytes, "Asource", sizeof bytes));
    T(!e->recovery_notice);
    printf("dataloss_test: edit-9fzc Save As peer: UI rotations=%u worker rotations=%u peer clean=1\n",
        atomic_load(&io->ui_rotations), atomic_load(&io->worker_rotations));
    T(atomic_load(&io->ui_rotations) == 0 && atomic_load(&io->worker_rotations) > 0);
    return 0;
}
static void followup_hold(work_ctx *ctx)
{
    followup_io *io = ctx->arg;
    while (atomic_load(&io->hold) && !work_should_stop(ctx)) sched_yield();
}
static int followup_lease_body(const char *root, followup_fixture *fixture)
{
    char *source = fixture->source, *target = fixture->target, *jpath = fixture->journal;
    followup_io *io = &fixture->io;
    T(snprintf(source, IPC_PATH_CAP, "%s/source", root) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, IPC_PATH_CAP, "%s/target", root) > 0); T(write_file(target, "base") == 0);
    T(make_session(root, jpath, IPC_PATH_CAP) == 0);
    T(open_editor(&fixture->e, &fixture->backend, source, jpath) == 0);
    editor *e = fixture->e; e->retain_limit = 0;
    T(press(e, 'A', 0, "A") == 0);
    editor_buffer *owner = e->buffer;
    uint64_t peer_id; T(editor_add_buffer(e, target, NULL, 0, &peer_id) == 0); T(settle(e) == 0);
    T(press(e, 'B', 0, "B") == 0); editor_buffer *peer = e->buffer;
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(editor_flush(e) == 0);
    /* Hold the main bulk lane before the save prepare is queued: its journal
     * lease is real, but it cannot race this test's inspection of retention. */
    atomic_store(&io->hold, true);
    work_handle held = work_submit(&e->pool, (work_job){followup_hold, io, 0, WORK_BULK});
    T(held.epoch != 0); T(editor_save_request(e, 0) == 0); T(editor_save_leased(e));
    e->retain_limit = JOURNAL_STARTUP_SCAN_BYTES;
    editor_retain_tick(e);
    bool idle = owner->retain_state == EDITOR_RETAIN_IDLE && peer->retain_state == EDITOR_RETAIN_IDLE;
    printf("dataloss_test: edit-9fzc Ctrl+S prepare: leased=%d retention idle=%d\n", editor_save_leased(e), idle);
    T(idle);
    atomic_store(&io->hold, false); T(saved(e) == 0);
    uint64_t limit = deadline();
    while (!peer->retain_used) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(disk_is(source, "Asource") == 0); T(disk_is(target, "base") == 0);
    T(editor_select_tab(e, 1) == 0); T(settle(e) == 0); T(text_is(e, "Bbase") == 0);
    T(editor_tab(e, 1)->id == peer_id && editor_tab(e, 1)->modified);
    puts("dataloss_test: edit-9fzc retention resumed after the save lease");
    return 0;
}
static int followup(bool peer)
{
    char root[IPC_PATH_CAP]; T(make_root(root, sizeof root) == 0);
    followup_fixture fixture = {.io.ui = pthread_self()};
    int rc = peer ? followup_peer_body(root, &fixture) : followup_lease_body(root, &fixture);
    atomic_store(&fixture.io.hold, false);
    if (fixture.e) editor_close(fixture.e);
    remove_tree(root); return rc;
}

/* edit-utin (a): Save As onto a path open in a clean peer tab whose reload
 * then fails for a reason other than JOURNAL_BUSY. The file is written, so
 * this is never "save as failed": the peer becomes clean and stale, keeps the
 * old text (what the user chose to overwrite), and the clean refresh replaces
 * it once the cause clears. Landed on edit-9fzc's shape: the peer reload is
 * editor_reload_saved_peer (deferred checkpoint, so never REFUSED) and the
 * command's checkpoint is the owed worker publication (publish_owed). NO_SLOT: (P) slots = tab_capacity + closed_capacity
 * = 2 + 1, filled by the owner, a retained closed tab and the peer. UNREADABLE:
 * the written target is made mode 0 after the worker's rename, before the
 * adoption; no journal, since a journal's BASE capture of the written target
 * (same worker, right after the write) would race the chmod. (E) watchdogs reuse
 * EDITOR_IDLE_NS. */
typedef enum utin_reason { UTIN_NO_SLOT, UTIN_UNREADABLE } utin_reason;
static int utin_save_as(utin_reason reason)
{
    static const char *const names[] = {"no slot", "unreadable"};
    char root[IPC_PATH_CAP], source[IPC_PATH_CAP], target[IPC_PATH_CAP], other[IPC_PATH_CAP], jpath[IPC_PATH_CAP], canonical[IPC_PATH_CAP];
    T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "source") == 0);
    T(snprintf(target, sizeof target, "%s/target", root) > 0); T(write_file(target, "base") == 0);
    T(snprintf(other, sizeof other, "%s/other", root) > 0); T(write_file(other, "other") == 0);
    T(realpath(source, canonical)); strcpy(source, canonical);
    T(realpath(target, canonical)); strcpy(target, canonical);
    bool journaled = reason != UTIN_UNREADABLE;
    if (journaled) T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path = source, .journal_path = journaled ? jpath : NULL, .cols = COLS, .rows = ROWS};
    if (reason == UTIN_NO_SLOT) { cfg.tab_capacity = 2; cfg.closed_capacity = 1; }
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); T(settle(e) == 0);
    T(press(e, 'A', 0, "A") == 0);
    uint64_t id, peer_id;
    if (reason == UTIN_NO_SLOT) {
        T(editor_add_buffer(e, other, NULL, 0, &id) == 0); T(settle(e) == 0);
        T(editor_close_tab(e, 1) == 0); T(settle(e) == 0);
    }
    T(editor_add_buffer(e, target, NULL, 0, &peer_id) == 0); T(settle(e) == 0);
    if (reason == UTIN_NO_SLOT) {
        size_t used = 0; for (size_t i = 0; i < e->buffer_capacity; i++) used += e->buffers[i] != NULL;
        T(used == e->buffer_capacity);
    }
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0);
    if (journaled) { T(retain_settled(e) == 0); T(journal_flush(e->journal) == 0); }
    struct stat before; T(stat(target, &before) == 0);
    T(press(e, 's', PLAT_MOD_CTRL | PLAT_MOD_SHIFT, NULL) == 0); T(e->go.active);
    for (size_t i = 0; target[i]; i++) { char ch[] = {target[i], '\0'}; T(press(e, (uint8_t)target[i], 0, ch) == 0); }
    plat_event enter = {.kind = PLAT_EV_KEY, .press = true, .keysym = XKB_KEY_Return, .t0_ns = trace_now_ns()};
    T(editor_inject(e, &enter) == 0);
    uint64_t limit = deadline();
    /* Return is queued: run until the write started and was adopted (or refused). */
    bool started = false;
    if (reason == UTIN_UNREADABLE) {
        /* No turn runs between the worker's rename and the chmod, so the
         * adoption (maintenance) finds the written target unreadable. */
        while (!e->save_as && !e->recovery_notice) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
        T(e->save_as);
        for (struct stat now;; sched_yield()) {
            T(trace_now_ns() < limit);
            if (!stat(target, &now) && now.st_ino != before.st_ino) break;
        }
        T(chmod(target, 0) == 0);
    }
    while (!(started || e->recovery_notice) || e->file_command != KEYS_ACTION_NONE || e->save_as)
    { T(turn(e) == 0); started |= e->save_as != NULL; T(trace_now_ns() < limit); sched_yield(); }
    const editor_buffer *peer = e3z5_buffer(e, peer_id);
    fprintf(stderr, "dataloss_test: edit-utin save_as(%s): notice=%s path_moved=%d owner_modified=%d peer_stale=%d peer_modified=%d\n",
        names[reason], e->recovery_notice ? e->recovery_notice : "(none)", !strcmp(e->buffer->path, target),
        editor_tab(e, 0)->modified, peer ? peer->source_stale : -1, editor_tab(e, 1) ? editor_tab(e, 1)->modified : -1);
    T(!e->recovery_notice); /* the file is written: never "save as failed" */
    T(!strcmp(e->buffer->path, target) && !editor_tab(e, 0)->modified);
    T(peer && peer->source_stale && !editor_tab(e, 1)->modified); /* clean and stale, not modified */
    if (journaled) T(e->publish_owed || e->publish); /* edit-ze59: the rebind's worker publication */
    T(e3z5_tab_is(e, 1, "base") == 0); /* the overwritten text until the refresh lands */
    size_t peer_tab = 1;
    if (reason == UTIN_UNREADABLE) T(chmod(target, 0600) == 0);
    else { T(settle(e) == 0); T(editor_close_tab(e, 0) == 0); peer_tab = 0; } /* evicts the retained tab: a slot frees */
    T(disk_is(target, "Asource") == 0);
    limit = deadline();
    while (!e3z5_tab_holds(e, peer_tab, "Asource") || e->publish_owed || e->publish)
    { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0);
    peer = e3z5_buffer(e, peer_id);
    T(peer && !peer->source_stale && !editor_tab(e, peer_tab)->modified && !e->recovery_notice);
    fprintf(stderr, "dataloss_test: edit-utin save_as(%s): peer refreshed once the cause cleared\n", names[reason]);
    editor_close(e); remove_tree(root); return 0;
}
/* edit-utin (b): the two DEFERRED branches of the static reload_source leave
 * the buffer untouched; reached through editor_reload_saved_peer (edit-9fzc),
 * the only exported entry, a deferral shows as rc 0 with the buffer neither
 * retired nor replaced. A save in flight on the buffer's own file (file_save_busy;
 * only the file-level save, editor_large_save_begin, takes it) is held at
 * FILE_STEP_TEMP_WRITTEN; staged journal ops are e->op_count, set only around
 * the direct call (no editor turn sees it). */
static int utin_deferred(void)
{
    char root[64], source[128], jpath[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    T(make_session(root, jpath, sizeof jpath) == 0);
    render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
    T(press(e, 'x', 0, "x") == 0);
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN};
    file_set_step_hook(e->buffer->file, hold, &held);
    T(editor_large_save_begin(e) == 0); T(enter_hold(e, &held) == 0);
    editor_buffer *b = e->buffer;
    T(editor_reload_saved_peer(e, b, 0) == 0);
    fprintf(stderr, "dataloss_test: edit-utin reload under a file save in flight: replaced=%d\n", e->buffer != b || b->retired);
    T(e->buffer == b && !b->retired); T(text_is(e, "xbase") == 0);
    atomic_store(&held.release, true);
    uint64_t limit = deadline();
    while (file_save_busy(b->file)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0);
    e->op_count = 1u;
    int rc = editor_reload_saved_peer(e, b, 0);
    e->op_count = 0;
    fprintf(stderr, "dataloss_test: edit-utin reload with staged journal ops: replaced=%d\n", e->buffer != b || b->retired);
    T(rc == 0 && e->buffer == b && !b->retired); T(text_is(e, "xbase") == 0);
    T(editor_reload_saved_peer(e, b, 0) == 0);
    T(e->buffer != b && b->retired && e->publish_owed); /* DONE: replaced, its checkpoint owed (edit-9fzc) */
    limit = deadline();
    while (e->publish_owed || e->publish) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
    T(settle(e) == 0); T(text_is(e, "xbase") == 0); /* the large save wrote "xbase" */
    editor_close(e); remove_tree(root); return 0;
}


int main(int argc, char **argv)
{
    bool probe = argc == 2 && !strcmp(argv[1], "--probe");
    trace_init(); T(trace_thread_register() >= 0);
    /* (P) argc includes the program name and its single selector argument. */
    if (argc == 2 && !strcmp(argv[1], "--shared-save-dirty")) return shared_save_as(true);
    if (argc == 2 && !strcmp(argv[1], "--shared-save-clean")) return shared_save_as(false);
    if (argc == 2 && !strcmp(argv[1], "--stranded-orphan")) {
        T(stranded_orphan(false, false) == 0); T(stranded_orphan(true, false) == 0); T(stranded_orphan(false, true) == 0); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--two-process-reload")) return two_process_save(true);
    if (argc == 2 && !strcmp(argv[1], "--open-gib")) return open_gib();
    if (argc == 2 && !strcmp(argv[1], "--y315")) { T(y315_healthy() == 0); return y315_stall(); }
    if (argc == 2 && !strcmp(argv[1], "--y315-stall")) return y315_stall();
    if (argc == 2 && !strcmp(argv[1], "--retain-bound")) return retain_bound();
    if (argc == 2 && !strcmp(argv[1], "--rewrite-rename")) return external_rewrite(REWRITE_RENAME);
    if (argc == 2 && !strcmp(argv[1], "--rewrite-in-place")) return external_rewrite(REWRITE_IN_PLACE);
    if (argc == 2 && !strcmp(argv[1], "--retain-lifecycle")) return retain_lifecycle();
    if (argc == 2 && !strcmp(argv[1], "--ze59-stall")) return ze59_retain_stall();
    if (argc == 2 && !strcmp(argv[1], "--ze59-save-as")) return ze59_save_as();
    if (argc == 2 && !strcmp(argv[1], "--9fzc-peer")) return followup(true);
    if (argc == 2 && !strcmp(argv[1], "--9fzc-lease")) return followup(false);
    if (argc == 2 && !strcmp(argv[1], "--edit-ivv")) { T(ivv_choice(false) == 0); T(ivv_choice(true) == 0); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--retain-close")) return retain_close();
    if (argc == 2 && !strcmp(argv[1], "--detached-rewrite")) return detached_rewrite(false);
    if (argc == 2 && !strcmp(argv[1], "--retain-window")) return detached_rewrite(true);
    if (argc == 2 && !strcmp(argv[1], "--reload-held")) return reload_during_save(true) || reload_during_save(false);
    if (argc == 2 && !strcmp(argv[1], "--edit-e3z5")) {
        /* every case runs, so one red run shows each failure */
        int failed = e3z5_save_as(true); failed |= e3z5_save_as(false); failed |= e3z5_no_slot(); return failed;
    }
    if (argc == 2 && !strcmp(argv[1], "--edit-utin")) {
        /* every case runs, so one red run shows each failure */
        int failed = utin_save_as(UTIN_NO_SLOT); failed |= utin_save_as(UTIN_UNREADABLE); failed |= utin_deferred(); return failed;
    }
    T(kill_after_append() == 0);
    for (int phase = FILE_STEP_TEMP_CREATED; phase <= FILE_STEP_DIR_SYNCED; phase++) T(killed_mid_save(phase) == 0);
    T(changed_base(BASE_SHORTER) == 0);
    T(reload_during_save(true) == 0); T(reload_during_save(false) == 0);
    T(ivv_choice(false) == 0); T(ivv_choice(true) == 0);
    T(e3z5_save_as(true) == 0); T(e3z5_save_as(false) == 0); T(e3z5_no_slot() == 0);
    T(utin_save_as(UTIN_NO_SLOT) == 0); T(utin_save_as(UTIN_UNREADABLE) == 0); T(utin_deferred() == 0);
    T(save_during_prompt(true) == 0); T(save_during_prompt(false) == 0);
    T(hot_exit_mixed() == 0);
    T(reload_vs_keystroke(0) == 0); T(reload_vs_keystroke(1) == 0);
    T(two_process_save(false) == 0); T(two_process_save(true) == 0);
    T(shared_save_as(true) == 0); T(shared_save_as(false) == 0);
    T(external_rewrite(REWRITE_RENAME) == 0); T(external_rewrite(REWRITE_IN_PLACE) == 0); T(retain_lifecycle() == 0);
    T(y315_healthy() == 0); T(y315_stall() == 0);
    T(retain_close() == 0);
    T(detached_rewrite(false) == 0); T(detached_rewrite(true) == 0);
    T(ze59_retain_stall() == 0); T(ze59_save_as() == 0);
    T(followup(true) == 0); T(followup(false) == 0);
    if (!probe) { T(torn_undo_group() == 0); T(stranded_orphan(false, false) == 0); T(stranded_orphan(true, false) == 0); T(stranded_orphan(false, true) == 0); }
    if (probe) {
        /* Findings run to completion independently; each reports its own verdict. */
        int deleted = changed_base(BASE_DELETED), torn = torn_undo_group(), stranded = stranded_orphan(false, false);
        fprintf(stderr, "dataloss_test --probe: changed_base(deleted)=%s torn_undo_group=%s stranded_orphan=%s\n",
            deleted ? "FAIL" : "ok", torn ? "FAIL" : "ok", stranded ? "FAIL" : "ok");
        T(!deleted && !torn && !stranded);
    }
    puts("dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, "
         "Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery, "
         "edit-fi2 recovery after rename-over/in-place rewrite and retained-copy lifecycle, "
         "edit-ze59 stalled journal worker and large Save As keep typing live passed");
    return 0;
}
