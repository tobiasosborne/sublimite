/* UI-owned wait set. Registrations borrow level-triggered sources; only the
 * source owner drains/closes them. No allocation in registration or waiting. */
#ifndef EDIT_EDITOR_WAIT_H
#define EDIT_EDITOR_WAIT_H
#include "os/os.h"

/* (P) Win32: 64 wait positions - message queue - blink/repeat/present/clip.
 * See docs/decisions/edit-177r.8.md for the source-count derivation. */
#define EDITOR_WAIT_CAP (64u - 1u - 4u)
typedef enum editor_wait_tag {
    EDITOR_WAIT_POOL = 1, EDITOR_WAIT_JOURNAL, EDITOR_WAIT_STARTUP,
    EDITOR_WAIT_CONFIG, EDITOR_WAIT_IPC, EDITOR_WAIT_CLOSE,
    EDITOR_WAIT_FILE, EDITOR_WAIT_RASTER
} editor_wait_tag;
typedef struct editor_wait_set {
#if !defined(_WIN32)
    os_wake readiness; /* private borrowed epoll view: never signal/drain */
    int sources[EDITOR_WAIT_CAP];
#else
    void *sources[EDITOR_WAIT_CAP];
#endif
    uint32_t tags[EDITOR_WAIT_CAP];
    size_t count;
} editor_wait_set;
os_err editor_wait_init(editor_wait_set *set);
void editor_wait_close(editor_wait_set *set);
os_err editor_wait_add(editor_wait_set *set, const os_wake *wake, uint32_t tag);
/* All buffer/config watches share one process source. token is the Linux fd
 * or the Windows service token returned by file_watch*_start. */
os_err editor_wait_file(editor_wait_set *set, int token);
os_err editor_wait_remove(editor_wait_set *set, uint32_t tag);
/* timeout_ms: negative forever, zero nonblocking. Tags are hints: source
 * owners tolerate stale readiness. EINTR returns an empty successful turn. */
os_err editor_wait_ready(editor_wait_set *set, int timeout_ms,
                         uint32_t *tags, size_t capacity, size_t *count);
#if defined(_WIN32)
/* Native shared file-watch service (manual-reset event), never owned here. */
os_err editor_wait_add_handle(editor_wait_set *set, void *handle, uint32_t tag);
/* Callback for plat_config. Returns SIZE_MAX if capacity is insufficient. */
size_t editor_wait_handles(void *set, void **handles, size_t capacity);
#else
/* Native signalfd, shared file-watch and raster completion descriptors. */
os_err editor_wait_add_fd(editor_wait_set *set, int fd, uint32_t tag);
const os_wake *editor_wait_wake(const editor_wait_set *set);
#endif
#endif
