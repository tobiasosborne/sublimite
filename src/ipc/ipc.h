#ifndef EDIT_IPC_H
#define EDIT_IPC_H
#include "base/base.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPC_MAX_PATHS 128u
#define IPC_PATH_CAP 4096u
#define IPC_MAX_WIRE (1024u * 1024u)
#define IPC_MAX_CLIENTS 32u
/* edit-457.21: bounded UI work per drain call (§10), request deadlines and
 * pressure eviction (§11), lifecycle lock deadline (§12), receive pool (§14). */
#define IPC_DRAIN_BYTES (256u * 1024u)
#define IPC_DRAIN_CALLBACKS 8u
#define IPC_DRAIN_ACCEPTS 8u
#define IPC_REQUEST_DEADLINE_MS 5000u
#define IPC_EVICT_IDLE_MS 500u
#define IPC_LOCK_TIMEOUT_MS 1000u
#define IPC_RX_SMALL_SIZE (64u * 1024u)
#define IPC_RX_SMALL_COUNT 16u
#define IPC_RX_BIG_COUNT 2u

typedef enum ipc_result {
    IPC_OK = 0, IPC_EXISTS, IPC_INVALID, IPC_LIMIT, IPC_IO,
    IPC_PROTOCOL, IPC_TIMEOUT, IPC_BUSY, IPC_REJECTED
} ipc_result;
typedef struct ipc_path { const char *path; uint32_t line, col; } ipc_path;
typedef struct ipc_request {
    ipc_path paths[IPC_MAX_PATHS];
    size_t count;
    const char *cwd;
    const uint8_t *stdin_data;
    size_t stdin_size;
    bool wait, has_stdin, new_instance;
} ipc_request;
typedef struct ipc_args { ipc_request request; edit_arena arena; } ipc_args;
/* Parse argv[1..], snapshot cwd; owns output until fini. Zero-init out.
 * An existing whole filename wins over suffix parsing. Coordinates are 1-based.
 * Failing parse frees output. '-' is unique; '--' ends option recognition. */
ipc_result ipc_parse_args(int argc, char *const argv[], ipc_args *out);
ipc_result ipc_args_read_stdin(ipc_args *args, int fd);
void ipc_args_fini(ipc_args *args);
/* Decoder borrows wire storage. Strings include a checked trailing NUL.
 * On any failure out is zeroed. Encoder never serializes new_instance. */
ipc_result ipc_wire_encode(const ipc_request *request, uint8_t *wire,
                           size_t capacity, size_t *size);
ipc_result ipc_wire_decode(const uint8_t *wire, size_t size, ipc_request *out);

typedef uint64_t ipc_token;
struct ipc_peer;
struct ipc_rx;
typedef struct ipc_server {
    int fd, listener, lock_fd;
    edit_arena arena;
    struct ipc_peer *peers;
    uint64_t next_token;
    char socket_path[108]; /* leaf name inside dir_fd (edit-457.21); abstract: empty */
    uint64_t socket_device, socket_inode;
    bool owns_path;
    /* edit-457.21 additions. Init sets the defaults; tests may lower them. */
    int dir_fd, timer_fd;
    uint32_t request_deadline_ms, evict_idle_ms;
    uint64_t wait_drops;  /* wait clients that vanished before their closed reply */
    uint64_t drain_bytes; /* bytes received by the most recent drain call */
    struct ipc_rx *rx;
} ipc_server;
typedef ipc_result (*ipc_open_callback)(const ipc_request *, ipc_token, void *);
/* Caller-owned, zero-init before init; runtime NULL uses XDG_RUNTIME_DIR,
 * or a Linux abstract socket if unset. IPC_EXISTS means another server won.
 * Test hook: when EDIT_IPC_NAMESPACE is set, the abstract name on both server
 * and client is sublimite-<uid>-<namespace> instead of the default sublimite-<uid>.
 * Use a unique namespace per test process, inherited by its forked clients.
 * Empty namespaces return IPC_INVALID; names too long return IPC_LIMIT.
 * Filesystem endpoints ignore this hook. Set it before IPC calls, and keep
 * it stable for the endpoint lifetime; do not change it from another thread.
 * All server calls belong to the loop thread; fini only after successful init. */
ipc_result ipc_server_init(ipc_server *server, const char *runtime);
int ipc_server_fd(const ipc_server *server);
/* Nonblocking; borrowed request is valid only inside callback. Callback OK
 * queues ACK; other result rejects. Retain token for wait; copy desired data.
 * Call drain on epoll readability, and after report_closed. */
ipc_result ipc_server_drain(ipc_server *server, ipc_open_callback callback, void *ctx);
ipc_result ipc_server_report_closed(ipc_server *server, ipc_token token);
void ipc_server_fini(ipc_server *server);
/* edit-457.21 additions. True while the wait client behind token is still
 * connected and unanswered. The loop MUST sweep its token associations after
 * any drain that raised server->wait_drops and drop entries that are not live
 * (a client that gave up). Live tokens never exceed IPC_MAX_CLIENTS. */
bool ipc_server_token_live(const ipc_server *server, ipc_token token);
/* Server with no endpoint: for --new-instance. It never binds, locks or
 * connects to the shared socket; only adopted wait channels live in it. */
ipc_result ipc_server_init_isolated(ipc_server *server);
/* --wait for a launch that is the UI process (primary or isolated). Before
 * the UI starts, main creates a pair with ipc_launcher_pair and fork()s. The
 * child (UI) adopts adopt_fd and associates the returned token with the
 * initial request; the parent calls ipc_launcher_wait(launcher_fd) and exits
 * with its result once the closed reply arrives (IPC_IO if the UI exits first). */
ipc_result ipc_launcher_pair(int fds[2]);
ipc_result ipc_server_adopt_wait(ipc_server *server, int adopt_fd, ipc_token *token);
ipc_result ipc_launcher_wait(int launcher_fd);
/* Startup-only blocking handoff. timeout_ms bounds connect/send/ACK;
 * -1 means infinite. After ACK, --wait always waits without a deadline.
 * IPC_EXISTS/new_instance selection is the caller's startup policy. */
ipc_result ipc_client_send(const char *runtime, const ipc_request *request, int timeout_ms);
#endif
