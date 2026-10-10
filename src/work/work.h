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
#define WORK_FOREGROUND_RESERVE 2u /* active request + undrained completion */
#define WORK_FOREGROUND_WORKER WORK_MAX_WORKERS /* separate mailbox identity */
#define WORK_MAX_MAILBOXES (WORK_MAX_WORKERS + 1u)

typedef enum work_class { WORK_BULK = 0, WORK_RASTER = 1, WORK_FOREGROUND = 2 } work_class;

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
    bool continue_;       /* worker-private: request another invocation */
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
    _Atomic uint32_t finished_epoch; /* physical completion acknowledgement */
    /* UI-only selective receive state; workers never read these fields. */
    void (*receive_cb)(const work_msg *, void *);
    void *receive_ud;
    uint32_t receive_epoch, receive_generation;
    uint32_t receive_scan_generation;
    uint32_t receive_pos[WORK_MAX_WORKERS], receive_next;
    uint32_t receive_foreground_pos; /* UI-only cursor for the optional lane */
    uint32_t mailbox_worker; /* under pool.mu: last invocation's producer identity */
} work_slot;

typedef struct work_mailbox {
    _Alignas(64) _Atomic uint32_t head;   /* consumer (UI) */
    _Alignas(64) _Atomic uint32_t tail;   /* producer (worker) */
    work_msg msgs[WORK_MAILBOX_CAP];
    uint8_t received[WORK_MAILBOX_CAP]; /* UI-only holes from selective receive */
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
    /* Optional foreground lane. Existing n_workers/arrays describe only the
     * bulk/raster workers; its ctx.worker is WORK_FOREGROUND_WORKER. */
    work_queue foreground_queue;
    pthread_cond_t foreground_cv;
    pthread_t foreground_thread;
    work_ctx foreground_wctx;
    work_mailbox foreground_mb;
    bool foreground_enabled, foreground_started;
} work_pool;

/* n_bulk in [1,1], n_raster in [0, WORK_MAX_RASTER]. 0 on success, -1 on
 * failure. Failure releases all initialized resources and leaves an inactive
 * pool safe for shutdown, refused submit/cancel, or another init. Never init
 * an active pool; shutdown first. */
int  work_pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster);
/* Same lifecycle/error contract, plus one dedicated foreground worker and
 * mailbox. WORK_FOREGROUND never waits for a bulk/raster function to return,
 * even during blocking bulk I/O. Two slots are reserved from other classes;
 * the existing raster reserve remains independent. Foreground/raster prefer
 * their own reserves, then share bulk capacity, never each other's reserves.
 * Foreground jobs must be bounded CPU work/continuations and poll cancellation
 * at least every 5 ms CPU; blocking I/O/discovery belongs to WORK_BULK. A long
 * foreground job delays subsequent foreground requests. OS scheduling and
 * memory-bandwidth contention are not hard wall-time guarantees. No change
 * to legacy work_pool_init behavior: it refuses WORK_FOREGROUND submissions. */
int  work_pool_init_foreground(work_pool *p, uint32_t n_bulk, uint32_t n_raster);
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
/* UI-only priority upgrade for an existing BULK/FOREGROUND lease. Requires a
 * foreground-enabled pool and a bounded, nonblocking continuation job. Queued
 * work moves immediately; a running invocation keeps its current physical
 * owner and moves only after it returns with work_continue. No preemption of
 * blocking I/O: never prioritize an I/O/discovery job. Identity, argument,
 * result reservations and completion ownership stay unchanged. Before the
 * first migrated invocation, the UI must drain results from the old worker;
 * other foreground jobs remain runnable while that delivery is pending.
 * This preserves per-lease result order across the mailbox handoff. Returns -1
 * for an inactive pool, stale identity or raster lease; otherwise 0, including
 * an already finished valid lease. No allocation or additional slot. */
int work_prioritize(work_pool *p, work_handle h);
/* UI-only atomic enqueue. Returns 0 on success, -1 on invalid arguments,
 * capacity/identity exhaustion, unavailable class, or inactive pool. count is
 * at most WORK_MAX_JOBS; zero succeeds without accessing jobs/handles/pool.
 * All available classes may appear, preserving input FIFO order within each class.
 * Failure changes no slots, queues, or output handles and starts no jobs.
 * Success fills handles in input order before any worker can dequeue the batch.
 * No allocation. Arrays must not overlap pool storage or each other. */
int work_submit_batch(work_pool *p, const work_job *jobs, size_t count, work_handle *handles);
/* UI-only physical lease acknowledgement. True for an invalid handle, after
 * shutdown, or once its worker has returned / queued cancellation removed it.
 * Cancellation alone does not finish a running lease. Valid across slot reuse
 * within the same initialized pool lifetime, whose identities never wrap. */
bool work_handle_finished(const work_pool *p, work_handle h);
/* Logical cancel: no-op on a stale handle. Removes queued jobs immediately;
 * running jobs hold their slot until they return. Does not free job arguments.
 * With raster workers configured, bulk cannot use the final
 * WORK_RASTER_RESERVE slots; raster prefers those slots then shares the rest. */
void work_cancel(work_pool *p, work_handle h);

/* Worker side. */
bool     work_should_stop(const work_ctx *c);   /* poll at least every 5 ms CPU */
/* Worker-only cooperative continuation. Request, then return promptly from
 * fn. The same lease/argument/generation goes to the back of its class FIFO;
 * finished stays false until the final invocation returns. No new slot or
 * allocation. Return false when cancelled. Arguments remain caller-owned
 * until work_handle_finished, including cancellation between invocations.
 * Invocations may change worker (also within the raster class). A new worker
 * waits for the old worker's result reservations/callbacks to drain before
 * invoking the continuation, preserving per-lease order. Other jobs still run.
 * Continuations must not wait for I/O or a full mailbox on the foreground
 * lane: retain progress and retry in a later invocation instead. */
bool     work_continue(work_ctx *c);
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

/* UI-only selective receive. Match slot, epoch AND application generation;
 * cancellation validation precedes delivery. Other live messages retain their
 * pending reservations, payload, and per-worker order for their own receiver
 * or a later ordinary drain. Stale messages may be discarded during any scan.
 * Examined messages (including foreign messages and consumed holes) count
 * against max_messages. A per-handle cursor makes repeated slices progress
 * past foreign traffic. Return delivered count; nested receives/drains return
 * zero. Deadlines, callbacks and continuation follow the bounded-drain rules.
 * A foreign message at the ring head retains ring capacity until drained;
 * each client must continue pumping the shared pool. No allocation. */
size_t work_mailbox_receive_bounded(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud, size_t max_messages, uint64_t deadline_ns);
size_t work_mailbox_receive(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud);
/* Optional UI-only handler for this identity/generation. Ordinary drains route
 * matching validated messages here instead of their fallback callback, so a
 * shared-pool dispatcher cannot steal another client's result. Selective
 * receive uses the same handler when bound. Bind after submit, before any UI
 * pumping; worker publication may already have occurred. Returns -1 for a
 * stale/invalid binding. cb=NULL removes only this identity's binding (also
 * allowed after cancellation/shutdown). Unbind before freeing ud; do not
 * init/shutdown/free a bound receiver from inside its callback. */
int work_mailbox_bind(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud);

#endif
