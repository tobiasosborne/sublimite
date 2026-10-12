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
#include "trace/trace.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#define T(c) do { if (!(c)) { fprintf(stderr, "dataloss_test:%d: FAIL %s\n", __LINE__, #c); return 1; } } while (0)
enum { COLS = 40, ROWS = 4, TEXT_CAP = 4096 };
typedef struct oracle { uint64_t len, cursor; uint8_t text[TEXT_CAP]; } oracle;
static uint64_t deadline(void) { return trace_now_ns() + EDITOR_IDLE_NS; }
static int turn(editor *e) { int rc = editor_step(e, 0); return rc == EDITOR_OK || rc == EDITOR_MORE ? 0 : rc; }
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
    T(press(e, 'y', 0, "y") == 0); T(stale_prompt(e, other, "external") == 0);
    T(editor_select_tab(e, 0) == 0); T(settle(e) == 0); T(press(e, 'x', 0, "x") == 0);
    held_save held = {.phase = FILE_STEP_TEMP_WRITTEN}; T(savectl_set_step_hook(e->buffer->save, hold, &held) == 0);
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(enter_hold(e, &held) == 0);
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

/* (10a) Two processes edit and save the same file. Whoever loses the race
 * must know: a dirty buffer with a stale prompt or a save error, never a
 * clean tab over foreign bytes. */
typedef struct race_oracle { int save_error; bool modified, stale; oracle text; } race_oracle;
static int two_process_child(const char *source, const char *jpath, const int *lease, int go, int fd)
{
    render_backend backend; editor *e; T(open_editor_leased(&e, &backend, source, jpath, lease) == 0);
    T(press(e, 'B', 0, "B") == 0);
    uint8_t byte; ssize_t n; do n = read(go, &byte, 1); while (n < 0 && errno == EINTR); T(n == 1);
    /* No turn between the parent's save and our Ctrl+S: the change notification
     * and the key race inside one turn. */
    T(press(e, 's', PLAT_MOD_CTRL, NULL) == 0); T(saved(e) == 0);
    race_oracle o = {.save_error = e->buffer->save_error, .modified = editor_tab(e, 0)->modified, .stale = e->buffer->source_stale};
    T(capture(e, &o.text) == 0); T(write(fd, &o, sizeof o) == (ssize_t)sizeof o); _exit(0);
}
static int two_process_save(void)
{
    char root[64], source[128], first[128], second[128]; T(make_root(root, sizeof root) == 0);
    T(snprintf(source, sizeof source, "%s/source", root) > 0); T(write_file(source, "base") == 0);
    int first_lease = -1, second_lease = -1;
    T(make_leased_session(root, first, sizeof first, &first_lease) == 0);
    T(make_leased_session(root, second, sizeof second, &second_lease) == 0);
    int go[2], report[2]; T(pipe(go) == 0); T(pipe(report) == 0);
    pid_t child = fork(); T(child >= 0);
    if (!child) { close(first_lease); close(go[1]); close(report[0]); _exit(two_process_child(source, second, &second_lease, go[0], report[1])); }
    close(second_lease); close(go[0]); close(report[1]);
    render_backend backend; editor *e; T(open_editor_leased(&e, &backend, source, first, &first_lease) == 0);
    close(first_lease); /* the editor holds its own duplicate */
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
    fprintf(stderr, "dataloss_test: two_process_save disk=[%.*s] A(stale=%d modified=%d) B(err=%d stale=%d modified=%d text=[%.*s])\n",
        n > 0 ? (int)n : 0, disk, e->buffer->source_stale, editor_tab(e, 0)->modified, b.save_error, b.stale, b.modified, (int)b.text.len, b.text.text);
    bool a_won = n == 5 && !memcmp(disk, "Abase", 5), b_won = n == 5 && !memcmp(disk, "Bbase", 5);
    T(a_won || b_won);
    if (a_won) { T(b.modified && (b.save_error || b.stale)); T(b.text.len == 5 && !memcmp(b.text.text, "Bbase", 5)); }
    else { T(e->buffer->source_stale); T(text_is(e, "Abase") == 0); }
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

int main(int argc, char **argv)
{
    bool probe = argc == 2 && !strcmp(argv[1], "--probe");
    trace_init(); T(trace_thread_register() >= 0);
    /* (P) argc includes the program name and its single selector argument. */
    if (argc == 2 && !strcmp(argv[1], "--stranded-orphan")) {
        T(stranded_orphan(false, false) == 0); T(stranded_orphan(true, false) == 0); T(stranded_orphan(false, true) == 0); return 0;
    }
    T(kill_after_append() == 0);
    for (int phase = FILE_STEP_TEMP_CREATED; phase <= FILE_STEP_DIR_SYNCED; phase++) T(killed_mid_save(phase) == 0);
    T(changed_base(BASE_SHORTER) == 0);
    T(reload_during_save(true) == 0); T(reload_during_save(false) == 0);
    T(save_during_prompt(true) == 0); T(save_during_prompt(false) == 0);
    T(hot_exit_mixed() == 0);
    T(reload_vs_keystroke(0) == 0); T(reload_vs_keystroke(1) == 0);
    T(two_process_save() == 0);
    if (!probe) { T(torn_undo_group() == 0); T(stranded_orphan(false, false) == 0); T(stranded_orphan(true, false) == 0); T(stranded_orphan(false, true) == 0); }
    if (probe) {
        /* Findings run to completion independently; each reports its own verdict. */
        int deleted = changed_base(BASE_DELETED), torn = torn_undo_group(), stranded = stranded_orphan(false, false);
        fprintf(stderr, "dataloss_test --probe: changed_base(deleted)=%s torn_undo_group=%s stranded_orphan=%s\n",
            deleted ? "FAIL" : "ok", torn ? "FAIL" : "ok", stranded ? "FAIL" : "ok");
        T(!deleted && !torn && !stranded);
    }
    puts("dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, "
         "Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery passed");
    return 0;
}
