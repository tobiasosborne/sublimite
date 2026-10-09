#include "work/work.h"
#include "base/base.h"
#include "trace/trace.h"

#include <errno.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void mailbox_notify(const work_pool *p)
{
    uint64_t one = 1;
    ssize_t r;
    do {
        r = write(p->efd, &one, sizeof one);
    } while (r < 0 && errno == EINTR);
    /* EAGAIN means the nonblocking eventfd is already readable. */
}

static void *worker_main(void *vp)
{
    work_ctx *wc = vp;
    work_pool *p = wc->pool;
    uint32_t cls = wc->worker < p->n_bulk ? (uint32_t)WORK_BULK : (uint32_t)WORK_RASTER;
    work_queue *q = &p->queue[cls];

    (void)trace_thread_register();
    pthread_mutex_lock(&p->mu);
    for (;;) {
        while (q->count == 0 && !p->shutting_down)
            pthread_cond_wait(&p->cv[cls], &p->mu);
        if (p->shutting_down)
            break;
        uint32_t si = q->q[q->head], ep = q->epochs[q->head];
        q->head = (q->head + 1u) % WORK_MAX_JOBS;
        q->count--;
        work_slot *s = &p->slots[si];
        pthread_mutex_unlock(&p->mu);

        if (atomic_load_explicit(&s->epoch, memory_order_acquire) == ep) {
            work_ctx c = { p, s, ep, s->job.generation, wc->worker, s->job.arg };
            s->job.fn(&c);
        }
        atomic_store_explicit(&s->busy, 0, memory_order_release);
        pthread_mutex_lock(&p->mu);
    }
    pthread_mutex_unlock(&p->mu);
    return NULL;
}

int work_pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster)
{
    *p = (work_pool){.efd = -1};
    if (n_bulk != 1 || n_raster > WORK_MAX_RASTER)
        return -1;
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        atomic_init(&p->slots[i].epoch, 1u);
    p->n_bulk = n_bulk;
    p->n_workers = n_bulk + n_raster;
    p->efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (p->efd < 0)
        return -1;
    if (pthread_mutex_init(&p->mu, NULL) != 0)
        goto fail_fd;
    if (pthread_cond_init(&p->cv[0], NULL) != 0)
        goto fail_mutex;
    if (pthread_cond_init(&p->cv[1], NULL) != 0)
        goto fail_cond;
    for (uint32_t i = 0; i < p->n_workers; i++) {
        p->wctx[i].pool = p;
        p->wctx[i].worker = i;
        if (pthread_create(&p->threads[i], NULL, worker_main, &p->wctx[i]) != 0) {
            p->n_workers = i;
            work_pool_shutdown(p);
            return -1;
        }
    }
    return 0;

fail_cond:
    pthread_cond_destroy(&p->cv[0]);
fail_mutex:
    pthread_mutex_destroy(&p->mu);
fail_fd:
    close(p->efd);
    p->efd = -1;
    p->n_workers = 0;
    return -1;
}

void work_pool_shutdown(work_pool *p)
{
    if (p->efd < 0)
        return;
    pthread_mutex_lock(&p->mu);
    p->shutting_down = true;
    uint64_t stamp = now_ns();
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) {
        work_slot *s = &p->slots[i];
        if (atomic_load_explicit(&s->epoch, memory_order_relaxed) == UINT32_MAX)
            continue;
        /* Preserve an earlier cancellation's timestamp. Publish the shutdown
         * timestamp before workers can observe the epoch invalidation. */
        if (atomic_load_explicit(&s->cancel_ns, memory_order_relaxed) == 0)
            atomic_store_explicit(&s->cancel_ns, stamp, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel);
    }
    /* A dequeued job is owned by its worker until join. A queued job has no
     * worker owner and must be retired here, without invoking or freeing args. */
    for (uint32_t cls = 0; cls < 2; cls++) {
        work_queue *q = &p->queue[cls];
        while (q->count) {
            atomic_store_explicit(&p->slots[q->q[q->head]].busy, 0, memory_order_release);
            q->head = (q->head + 1u) % WORK_MAX_JOBS;
            q->count--;
        }
        q->head = 0;
    }
    pthread_cond_broadcast(&p->cv[0]);
    pthread_cond_broadcast(&p->cv[1]);
    pthread_mutex_unlock(&p->mu);
    for (uint32_t i = 0; i < p->n_workers; i++)
        pthread_join(p->threads[i], NULL);
    close(p->efd);
    p->efd = -1;
    pthread_cond_destroy(&p->cv[0]);
    pthread_cond_destroy(&p->cv[1]);
    pthread_mutex_destroy(&p->mu);
}

int work_pool_eventfd(const work_pool *p) { return p->efd; }

work_handle work_submit(work_pool *p, work_job job)
{
    work_handle h = {0, 0};
    uint32_t cls = job.cls == WORK_BULK ? 0u : 1u;
    if (p->efd < 0 || p->shutting_down || job.fn == NULL ||
        (cls == 1u && p->n_workers <= p->n_bulk))
        return h;
    pthread_mutex_lock(&p->mu);
    uint32_t shared = p->n_workers > p->n_bulk ? WORK_MAX_JOBS - WORK_RASTER_RESERVE : WORK_MAX_JOBS;
    uint32_t limit = cls == 0u ? shared : WORK_MAX_JOBS;
    for (uint32_t scan = 0; scan < limit; scan++) {
        uint32_t i = cls == 0u ? scan : (shared + scan) % WORK_MAX_JOBS;
        work_slot *s = &p->slots[i];
        if (atomic_load_explicit(&s->busy, memory_order_acquire) ||
            atomic_load_explicit(&s->pending, memory_order_acquire))
            continue;
        /* Never repeat an identity. Keep one increment for cancellation;
         * exhausted slots remain retired until explicit pool reinitialization. */
        if (atomic_load_explicit(&s->epoch, memory_order_relaxed) >= UINT32_MAX - 1u)
            continue;
        s->job = job;
        uint32_t ep = atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel) + 1u;
        atomic_store_explicit(&s->cancel_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&s->busy, 1, memory_order_relaxed);
        work_queue *q = &p->queue[cls];
        uint32_t pos = (q->head + q->count) % WORK_MAX_JOBS;
        q->q[pos] = i;
        q->epochs[pos] = ep;
        q->count++;
        pthread_cond_signal(&p->cv[cls]);
        pthread_mutex_unlock(&p->mu);
        h.slot = i;
        h.epoch = ep;
        return h;
    }
    pthread_mutex_unlock(&p->mu);
    return h;
}

void work_cancel(work_pool *p, work_handle h)
{
    if (p->efd < 0 || p->shutting_down || h.epoch == 0 ||
        h.epoch == UINT32_MAX || h.slot >= WORK_MAX_JOBS)
        return;
    pthread_mutex_lock(&p->mu);
    work_slot *s = &p->slots[h.slot];
    uint32_t expect = h.epoch;
    if (atomic_load_explicit(&s->epoch, memory_order_acquire) != expect) {
        pthread_mutex_unlock(&p->mu);
        return;
    }
    /* Record time first; the epoch bump (release) publishes it. */
    atomic_store_explicit(&s->cancel_ns, now_ns(), memory_order_relaxed);
    (void)atomic_compare_exchange_strong_explicit(&s->epoch, &expect, expect + 1u,
        memory_order_acq_rel, memory_order_relaxed);
    /* Serialize removal with worker dequeue: only an entry still in the queue
     * may release busy here. Compact in place to preserve FIFO ordering. */
    uint32_t cls = s->job.cls == WORK_BULK ? 0u : 1u;
    work_queue *q = &p->queue[cls];
    for (uint32_t offset = 0; offset < q->count; offset++) {
        uint32_t pos = (q->head + offset) % WORK_MAX_JOBS;
        if (q->q[pos] != h.slot || q->epochs[pos] != h.epoch)
            continue;
        for (uint32_t next = offset + 1u; next < q->count; next++) {
            uint32_t src = (q->head + next) % WORK_MAX_JOBS;
            uint32_t dst = (q->head + next - 1u) % WORK_MAX_JOBS;
            q->q[dst] = q->q[src];
            q->epochs[dst] = q->epochs[src];
        }
        q->count--;
        atomic_store_explicit(&s->busy, 0, memory_order_release);
        break;
    }
    pthread_mutex_unlock(&p->mu);
}

bool work_should_stop(const work_ctx *c)
{
    return atomic_load_explicit(&c->slot->epoch, memory_order_acquire) != c->epoch;
}

uint64_t work_cancel_time_ns(const work_ctx *c)
{
    if (!work_should_stop(c))
        return 0;
    return atomic_load_explicit(&c->slot->cancel_ns, memory_order_relaxed);
}

bool work_publish(work_ctx *c, const work_msg *m)
{
    work_pool *p = c->pool;
    if (work_should_stop(c)) {
        atomic_fetch_add_explicit(&p->dropped_stale, 1, memory_order_relaxed);
        return false;
    }
    work_mailbox *mb = &p->mb[c->worker];
    uint32_t t = atomic_load_explicit(&mb->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&mb->head, memory_order_acquire);
    if (t - h >= WORK_MAILBOX_CAP) {
        atomic_fetch_add_explicit(&p->dropped_full, 1, memory_order_relaxed);
        return false;
    }
    work_msg *dst = &mb->msgs[t % WORK_MAILBOX_CAP];
    *dst = *m;
    dst->slot_ = (uint32_t)(c->slot - p->slots);
    dst->epoch_ = c->epoch;
    atomic_fetch_add_explicit(&c->slot->pending, 1u, memory_order_relaxed);
    atomic_store_explicit(&mb->tail, t + 1u, memory_order_release);
    mailbox_notify(p);
    return true;
}

bool work_mailbox_pending(const work_pool *p)
{
    for (uint32_t w = 0; w < p->n_workers; w++)
        if (atomic_load_explicit(&p->mb[w].head, memory_order_relaxed) !=
            atomic_load_explicit(&p->mb[w].tail, memory_order_acquire))
            return true;
    return false;
}

size_t work_mailbox_drain_bounded(work_pool *p, void (*cb)(const work_msg *, void *),
                                  void *ud, size_t max_messages, uint64_t deadline_ns)
{
    if (p->draining || cb == NULL)
        return 0;
    p->draining = true;
    uint64_t v;
    if (p->efd >= 0) {
        ssize_t r;
        do {
            r = read(p->efd, &v, sizeof v);
        } while (r < 0 && errno == EINTR);
    }
    size_t n = 0, examined = 0;
    uint32_t empty = 0;
    while (examined < max_messages && empty < p->n_workers) {
        if (deadline_ns && now_ns() >= deadline_ns)
            break;
        uint32_t w = p->drain_next;
        p->drain_next = (w + 1u) % p->n_workers;
        work_mailbox *mb = &p->mb[w];
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        uint32_t t = atomic_load_explicit(&mb->tail, memory_order_acquire);
        if (h == t) {
            empty++;
            continue;
        }
        empty = 0;
        work_msg m = mb->msgs[h % WORK_MAILBOX_CAP];
        atomic_store_explicit(&mb->head, h + 1u, memory_order_release);
        work_slot *s = &p->slots[m.slot_];
        if (atomic_load_explicit(&s->epoch, memory_order_acquire) != m.epoch_) {
            atomic_fetch_add_explicit(&p->dropped_stale, 1, memory_order_relaxed);
        } else {
            cb(&m, ud);
            n++;
        }
        /* Callback retains the slot reservation even if it submits a job. */
        atomic_fetch_sub_explicit(&s->pending, 1u, memory_order_release);
        examined++;
    }
    if (p->efd >= 0 && work_mailbox_pending(p))
        mailbox_notify(p);
    p->draining = false;
    return n;
}

size_t work_mailbox_drain(work_pool *p, void (*cb)(const work_msg *, void *), void *ud)
{
    return work_mailbox_drain_bounded(p, cb, ud, WORK_DRAIN_MAX_MESSAGES,
                                      now_ns() + WORK_DRAIN_BUDGET_NS);
}
