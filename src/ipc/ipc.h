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
typedef struct ipc_server {
    int fd, listener, lock_fd;
    edit_arena arena;
    struct ipc_peer *peers;
    uint64_t next_token;
    char socket_path[108];
    uint64_t socket_device, socket_inode;
    bool owns_path;
} ipc_server;
typedef ipc_result (*ipc_open_callback)(const ipc_request *, ipc_token, void *);
/* Caller-owned, zero-init before init; runtime NULL uses XDG_RUNTIME_DIR,
 * or a Linux abstract socket if unset. IPC_EXISTS means another server won.
 * All server calls belong to the loop thread; fini only after successful init. */
ipc_result ipc_server_init(ipc_server *server, const char *runtime);
int ipc_server_fd(const ipc_server *server);
/* Nonblocking; borrowed request is valid only inside callback. Callback OK
 * queues ACK; other result rejects. Retain token for wait; copy desired data.
 * Call drain on epoll readability, and after report_closed. */
ipc_result ipc_server_drain(ipc_server *server, ipc_open_callback callback, void *ctx);
ipc_result ipc_server_report_closed(ipc_server *server, ipc_token token);
void ipc_server_fini(ipc_server *server);
/* Startup-only blocking handoff. timeout_ms bounds connect/send/ACK;
 * -1 means infinite. After ACK, --wait always waits without a deadline.
 * IPC_EXISTS/new_instance selection is the caller's startup policy. */
ipc_result ipc_client_send(const char *runtime, const ipc_request *request, int timeout_ms);
#endif
