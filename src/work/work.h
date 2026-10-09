/* work.h - fixed worker pool + UI-thread mailbox (P1.8).
 * Threads are created once in work_pool_init (each calls trace_thread_register).
 * Pool lifecycle, eventfd access, submit, cancel and drain: UI thread only;
 * serialize them on that thread. Do not overlap shutdown/init with any use.
 * Callbacks may submit/cancel; nested drains return 0. Shutdown/init must wait
 * until the outer drain returns. A pool must be initialized before any use;
 * after shutdown, submit/cancel are harmless and shutdown is idempotent.
 * work_publish / work_should_stop: worker thread (inside a job) only.
 * No allocation here; the caller owns storage aligned to _Alignof(work_pool)
 * (64 bytes). Heap callers use aligned_alloc(_Alignof(work_pool), sizeof *p).
 * Idle workers sleep on a condvar (no wakeups when nothing is queued). */
#ifndef EDITOR_WORK_WORK_H
#define EDITOR_WORK_WORK_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define WORK_MAX_RASTER   8u
#define WORK_MAX_WORKERS  (1u + WORK_MAX_RASTER)
#define WORK_MAX_JOBS     64u     /* in-flight (queued + running) jobs */
#define WORK_MAILBOX_CAP  256u    /* messages per worker, power of two */
#define WORK_MSG_DATA     48u
#define WORK_RASTER_RESERVE (WORK_MAX_RASTER + 1u) /* one full worker batch + completion */
#define WORK_DRAIN_MAX_MESSAGES 64u
#define WORK_DRAIN_BUDGET_NS 500000ull  /* (G) between-callback UI slice budget */

typedef enum work_class { WORK_BULK = 0, WORK_RASTER = 1 } work_class;

/* 64-byte message. kind/generation/data belong to the app; slot_/epoch_ are
 * filled by work_publish and used to drop messages of cancelled jobs. */
typedef struct work_msg {
    uint32_t kind;
    uint32_t generation;
    uint32_t slot_;
    uint32_t epoch_;
    uint8_t  data[WORK_MSG_DATA];
} work_msg;

struct work_pool;
struct work_slot;

typedef struct work_ctx {
    struct work_pool *pool;
    struct work_slot *slot;
    uint32_t epoch;        /* snapshot taken at submit */
    uint32_t generation;   /* caller's tag, copied from the job */
    uint32_t worker;       /* mailbox index */
    void    *arg;
} work_ctx;

typedef struct work_job {
    void (*fn)(work_ctx *);
    void *arg;
    uint32_t generation;
    work_class cls;
} work_job;

typedef struct work_handle {
    uint32_t slot;
    uint32_t epoch;        /* 0 = invalid */
} work_handle;

typedef struct work_slot {
    work_job job;
    _Atomic uint32_t epoch;      /* bumped by cancel; job stops when != ctx.epoch */
    _Atomic uint64_t cancel_ns;  /* CLOCK_MONOTONIC at cancel; written before epoch */
    _Atomic uint32_t busy;       /* 1 from submit until the worker is done with it */
    _Atomic uint32_t pending;    /* published, not yet drained; slot is not reused while >0 */
} work_slot;

typedef struct work_mailbox {
    _Alignas(64) _Atomic uint32_t head;   /* consumer (UI) */
    _Alignas(64) _Atomic uint32_t tail;   /* producer (worker) */
    work_msg msgs[WORK_MAILBOX_CAP];
} work_mailbox;

typedef struct work_queue {
    uint32_t q[WORK_MAX_JOBS];
    uint32_t epochs[WORK_MAX_JOBS];
    uint32_t head, count;
} work_queue;

typedef struct work_pool {
    work_slot slots[WORK_MAX_JOBS];
    work_mailbox mb[WORK_MAX_WORKERS];
    work_queue queue[2];
    pthread_mutex_t mu;
    pthread_cond_t cv[2];
    pthread_t threads[WORK_MAX_WORKERS];
    work_ctx wctx[WORK_MAX_WORKERS];
    uint32_t n_workers, n_bulk;
    bool shutting_down;                 /* under mu */
    int efd;
    _Atomic uint64_t dropped_stale;
    _Atomic uint64_t dropped_full;
    bool draining;                     /* UI-only callback reentrancy guard */
    uint32_t drain_next;                /* UI-only round-robin mailbox cursor */
} work_pool;

/* n_bulk in [1,1], n_raster in [0, WORK_MAX_RASTER]. 0 on success, -1 on
 * failure. Failure releases all initialized resources and leaves an inactive
 * pool safe for shutdown, refused submit/cancel, or another init. Never init
 * an active pool; shutdown first. */
int  work_pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster);
/* Cancels everything, drops queued jobs, joins threads. Running jobs must poll.
 * After return every busy bit and queue count is zero. Arguments always remain
 * caller-owned, including dropped jobs. Undrained messages remain reserved
 * until drained (and are stale); drain is allowed after shutdown. */
void work_pool_shutdown(work_pool *p);
int  work_pool_eventfd(const work_pool *p);

/* Returns handle with epoch 0 if the pool is full / shutting down, or all
 * otherwise free slots exhausted their identities. Slots retire before epoch
 * wrap. Handles belong to one initialized pool lifetime; discard all handles
 * before shutdown/reinitialization and never use them with another pool. */
work_handle work_submit(work_pool *p, work_job job);
/* Logical cancel: no-op on a stale handle. Removes queued jobs immediately;
 * running jobs hold their slot until they return. Does not free job arguments.
 * With raster workers configured, bulk cannot use the final
 * WORK_RASTER_RESERVE slots; raster prefers those slots then shares the rest. */
void work_cancel(work_pool *p, work_handle h);

/* Worker side. */
bool     work_should_stop(const work_ctx *c);   /* poll at least every 5 ms CPU */
uint64_t work_cancel_time_ns(const work_ctx *c);/* when cancelled (0 if not) */
/* Returns false if dropped (stale job or mailbox full). */
bool     work_publish(work_ctx *c, const work_msg *m);

/* UI side: delivers at most WORK_DRAIN_MAX_MESSAGES examined messages, stopping
 * between callbacks at WORK_DRAIN_BUDGET_NS from entry. Returns delivered count
 * (stale messages also consume budget). Each callback must itself be bounded;
 * a callback or OS descheduling can overrun the wall-time deadline. Check input
 * between calls. Nested drains on the same pool return 0. Partial drains re-arm
 * eventfd so the caller can resume after checking input. */
size_t work_mailbox_drain(work_pool *p, void (*cb)(const work_msg *, void *), void *ud);
/* Explicit slice controls: maximum examined messages and absolute
 * CLOCK_MONOTONIC deadline in ns (0 disables the deadline). max_messages=0
 * consumes nothing. Callback must be non-NULL. Valid after shutdown as well. */
size_t work_mailbox_drain_bounded(work_pool *p, void (*cb)(const work_msg *, void *),
                                  void *ud, size_t max_messages, uint64_t deadline_ns);
/* UI-only continuation check; also true when all remaining messages are stale. */
bool work_mailbox_pending(const work_pool *p);

#endif
