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
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
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
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
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
    if (posix_spawn_file_actions_init(&actions) != 0) { close(fds[0]); close(fds[1]); return 1; }
    int error = posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[0]);
    if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[1]);
    if (!error) error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    const char *program = owner->discovery_program ? owner->discovery_program : "/usr/bin/fc-match";
    char *const args[] = {(char *)program, "-f", "%{file}\n%{index}\n%{color}\n%{charset}\n", (char *)pattern, NULL};
    pid_t pid = 0;
    if (!error) error = posix_spawn(&pid, program, &actions, NULL, args, environ);
    (void)posix_spawn_file_actions_destroy(&actions); close(fds[1]);
    if (error) { close(fds[0]); return 1; }
    atomic_store_explicit(&owner->child_pid, (uint32_t)pid, memory_order_release);
    query_output output = {0}; output.cp = cp;
    int cancelled = 0, killed = 0, status = 0, eof = 0, reaped = 0;
    uint64_t start = discovery_time();
    while (!reaped || !eof) {
        if (discovery_stop(owner, ctx)) cancelled = 1;
        if (!reaped && (cancelled || output.invalid || discovery_time() - start >= UINT64_C(30000000000)) && !killed) {
            (void)kill(pid, SIGKILL); killed = 1;
        }
        char bytes[512];
        ssize_t n = read(fds[0], bytes, sizeof bytes);
        if (n > 0) query_feed(&output, bytes, (size_t)n);
        else if (n == 0) eof = 1;
        else if (errno != EAGAIN && errno != EINTR) { output.invalid = 1; eof = 1; }
        if (!reaped) {
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) reaped = 1;
            else if (result < 0 && errno != EINTR) { reaped = 1; output.invalid = 1; }
        }
        if (!reaped || !eof) {
            struct pollfd fd = {fds[0], POLLIN, 0};
            if (!eof) (void)poll(&fd, 1, 1);
            else { struct timespec delay = {0,1000000}; (void)nanosleep(&delay, NULL); }
        }
    }
    close(fds[0]); atomic_store_explicit(&owner->child_pid, 0u, memory_order_release);
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
    out->worker = pthread_self();
    if (!query_child(owner, ctx, 0x4e2d, ":charset=4e2d:lang=zh-cn", 0,
                     out->cjk, &out->cjk_index, &out->have_fontconfig)) return 0;
    if (!query_child(owner, ctx, 0x1f600, ":charset=1f600:color=false", 1,
                     out->emoji, &out->emoji_index, &out->have_fontconfig)) return 0;
    out->elapsed_ns = discovery_time() - start;
    return 1;
}
