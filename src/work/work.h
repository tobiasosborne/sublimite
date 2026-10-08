/* work.h - fixed worker pool + UI-thread mailbox (P1.8).
 * Threads are created once in work_pool_init (each calls trace_thread_register).
 * work_submit / work_cancel / work_mailbox_drain: UI thread only.
 * work_publish / work_should_stop: worker thread (inside a job) only.
 * No allocation here; the caller owns the work_pool storage. Idle workers
 * sleep on a condvar (no wakeups when nothing is queued). */
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
} work_pool;

/* n_bulk in [1,1], n_raster in [0, WORK_MAX_RASTER]. 0 on success. */
int  work_pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster);
/* Cancels everything, drops queued jobs, joins threads. Running jobs must poll. */
void work_pool_shutdown(work_pool *p);
int  work_pool_eventfd(const work_pool *p);

/* Returns handle with epoch 0 if the pool is full / shutting down. */
work_handle work_submit(work_pool *p, work_job job);
/* Logical cancel: returns immediately. No-op on a stale handle. */
void work_cancel(work_pool *p, work_handle h);

/* Worker side. */
bool     work_should_stop(const work_ctx *c);   /* poll at least every 5 ms CPU */
uint64_t work_cancel_time_ns(const work_ctx *c);/* when cancelled (0 if not) */
/* Returns false if dropped (stale job or mailbox full). */
bool     work_publish(work_ctx *c, const work_msg *m);

/* UI side: clears the eventfd, delivers pending live messages. Returns count. */
size_t work_mailbox_drain(work_pool *p, void (*cb)(const work_msg *, void *), void *ud);

#endif
