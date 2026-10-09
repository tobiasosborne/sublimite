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
        atomic_store_explicit(&s->finished_epoch, ep, memory_order_release);
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
            atomic_store_explicit(&p->slots[q->q[q->head]].finished_epoch,
                                  q->epochs[q->head], memory_order_release);
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
    /* Preserve the original single-submit class interpretation. */
    job.cls = job.cls == WORK_BULK ? WORK_BULK : WORK_RASTER;
    (void)work_submit_batch(p, &job, 1, &h);
    return h;
}

int work_submit_batch(work_pool *p, const work_job *jobs, size_t count, work_handle *handles)
{
    if (count == 0) return 0;
    if (count > WORK_MAX_JOBS || jobs == NULL || handles == NULL ||
        p->efd < 0 || p->shutting_down)
        return -1;
    uint32_t needed[2] = {0};
    for (size_t j = 0; j < count; j++) {
        if (jobs[j].fn == NULL || (jobs[j].cls != WORK_BULK && jobs[j].cls != WORK_RASTER) ||
            (jobs[j].cls == WORK_RASTER && p->n_workers <= p->n_bulk))
            return -1;
        needed[(uint32_t)jobs[j].cls]++;
    }
    pthread_mutex_lock(&p->mu);
    if (needed[0] > WORK_MAX_JOBS - p->queue[0].count ||
        needed[1] > WORK_MAX_JOBS - p->queue[1].count) {
        pthread_mutex_unlock(&p->mu);
        return -1;
    }
    uint32_t shared = p->n_workers > p->n_bulk ? WORK_MAX_JOBS - WORK_RASTER_RESERVE : WORK_MAX_JOBS;
    uint32_t chosen[WORK_MAX_JOBS];
    bool reserved[WORK_MAX_JOBS] = {false};
    /* Bulk has the narrower eligible set. Choose it first so mixed batches
     * cannot consume shared capacity with raster while bulk still needs it. */
    for (uint32_t cls = 0; cls < 2; cls++) {
        uint32_t limit = cls == 0u ? shared : WORK_MAX_JOBS;
        uint32_t scan = 0;
        for (size_t j = 0; j < count; j++) {
            if ((uint32_t)jobs[j].cls != cls) continue;
            bool found = false;
            for (; scan < limit; scan++) {
                uint32_t i = cls == 0u ? scan : (shared + scan) % WORK_MAX_JOBS;
                work_slot *s = &p->slots[i];
                if (reserved[i] || atomic_load_explicit(&s->busy, memory_order_acquire) ||
                    atomic_load_explicit(&s->pending, memory_order_acquire) ||
                    atomic_load_explicit(&s->epoch, memory_order_relaxed) >= UINT32_MAX - 1u)
                    continue;
                chosen[j] = i;
                reserved[i] = true;
                scan++;
                found = true;
                break;
            }
            if (!found) {
                pthread_mutex_unlock(&p->mu);
                return -1;
            }
        }
    }
    /* No failure is possible after this point. Initialize the entire batch
     * before queue publication, with workers excluded by the same mutex. */
    for (size_t j = 0; j < count; j++) {
        uint32_t i = chosen[j];
        work_slot *s = &p->slots[i];
        s->job = jobs[j];
        uint32_t ep = atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel) + 1u;
        atomic_store_explicit(&s->cancel_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&s->busy, 1, memory_order_relaxed);
        s->receive_cb = NULL;
        s->receive_ud = NULL;
        s->receive_epoch = ep;
        s->receive_generation = jobs[j].generation;
        s->receive_scan_generation = jobs[j].generation;
        s->receive_next = 0;
        for (uint32_t w = 0; w < p->n_workers; w++)
            s->receive_pos[w] = atomic_load_explicit(&p->mb[w].head, memory_order_relaxed);
        handles[j] = (work_handle){i, ep};
    }
    for (size_t j = 0; j < count; j++) {
        uint32_t cls = (uint32_t)jobs[j].cls;
        work_queue *q = &p->queue[cls];
        uint32_t pos = (q->head + q->count) % WORK_MAX_JOBS;
        q->q[pos] = handles[j].slot;
        q->epochs[pos] = handles[j].epoch;
        q->count++;
    }
    for (uint32_t cls = 0; cls < 2; cls++)
        if (needed[cls]) pthread_cond_broadcast(&p->cv[cls]);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

bool work_handle_finished(const work_pool *p, work_handle h)
{
    if (h.epoch == 0 || h.epoch == UINT32_MAX || h.slot >= WORK_MAX_JOBS || p->efd < 0)
        return true;
    return atomic_load_explicit(&p->slots[h.slot].finished_epoch, memory_order_acquire) >= h.epoch;
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
        atomic_store_explicit(&s->finished_epoch, h.epoch, memory_order_release);
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

int work_mailbox_bind(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud)
{
    if (h.slot >= WORK_MAX_JOBS || h.epoch == 0 || h.epoch == UINT32_MAX) return -1;
    work_slot *s = &p->slots[h.slot];
    if (cb == NULL) {
        if (s->receive_epoch == h.epoch && s->receive_generation == generation) {
            s->receive_cb = NULL;
            s->receive_ud = NULL;
        }
        return 0;
    }
    if (p->efd < 0 || p->shutting_down ||
        atomic_load_explicit(&s->epoch, memory_order_acquire) != h.epoch ||
        s->job.generation != generation)
        return -1;
    s->receive_cb = cb;
    s->receive_ud = ud;
    return 0;
}

static void mailbox_clear_notification(const work_pool *p)
{
    uint64_t v;
    if (p->efd >= 0) {
        ssize_t r;
        do {
            r = read(p->efd, &v, sizeof v);
        } while (r < 0 && errno == EINTR);
    }
}

/* Only the UI touches received[]. Publish reclaimed capacity after clearing
 * its hole markers; the worker may immediately overwrite those message slots. */
static void mailbox_reclaim(work_pool *p, work_mailbox *mb)
{
    uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&mb->tail, memory_order_acquire);
    uint32_t next = h;
    while (next != t && mb->received[next % WORK_MAILBOX_CAP]) {
        mb->received[next % WORK_MAILBOX_CAP] = 0;
        next++;
    }
    if (next < h) {
        /* A dormant 32-bit selector could otherwise alias a new ring position
         * after a complete counter cycle. Invalidate every cursor at wrap;
         * retained messages start at next, so restarting preserves order. */
        uint32_t w = (uint32_t)(mb - p->mb);
        for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) p->slots[i].receive_pos[w] = next;
    }
    if (next != h) atomic_store_explicit(&mb->head, next, memory_order_release);
}

static bool mailbox_consume(work_pool *p, work_mailbox *mb, uint32_t pos,
    void (*cb)(const work_msg *, void *), void *ud)
{
    work_msg m = mb->msgs[pos % WORK_MAILBOX_CAP];
    mb->received[pos % WORK_MAILBOX_CAP] = 1;
    mailbox_reclaim(p, mb);
    work_slot *s = &p->slots[m.slot_];
    bool live = atomic_load_explicit(&s->epoch, memory_order_acquire) == m.epoch_;
    if (!live) {
        atomic_fetch_add_explicit(&p->dropped_stale, 1, memory_order_relaxed);
    } else if (s->receive_cb && s->receive_epoch == m.epoch_ &&
               s->receive_generation == m.generation) {
        s->receive_cb(&m, s->receive_ud);
    } else {
        cb(&m, ud);
    }
    /* Keep the result/slot lease throughout the callback, even if it submits. */
    atomic_fetch_sub_explicit(&s->pending, 1u, memory_order_release);
    return live;
}

static void mailbox_end_drain(work_pool *p)
{
    if (p->efd >= 0 && work_mailbox_pending(p)) mailbox_notify(p);
    p->draining = false;
}

size_t work_mailbox_drain_bounded(work_pool *p, void (*cb)(const work_msg *, void *),
                                  void *ud, size_t max_messages, uint64_t deadline_ns)
{
    if (p->draining || cb == NULL) return 0;
    p->draining = true;
    mailbox_clear_notification(p);
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
        if (mailbox_consume(p, mb, h, cb, ud)) n++;
        examined++;
    }
    mailbox_end_drain(p);
    return n;
}

size_t work_mailbox_drain(work_pool *p, void (*cb)(const work_msg *, void *), void *ud)
{
    return work_mailbox_drain_bounded(p, cb, ud, WORK_DRAIN_MAX_MESSAGES,
                                      now_ns() + WORK_DRAIN_BUDGET_NS);
}

size_t work_mailbox_receive_bounded(work_pool *p, work_handle handle, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud, size_t max_messages, uint64_t deadline_ns)
{
    if (p->draining || cb == NULL || handle.slot >= WORK_MAX_JOBS || !handle.epoch)
        return 0;
    work_slot *selected = &p->slots[handle.slot];
    /* A reused slot cannot have outstanding messages from its old lease. */
    if (selected->receive_epoch != handle.epoch) return 0;
    p->draining = true;
    mailbox_clear_notification(p);
    bool reset = selected->receive_scan_generation != generation;
    selected->receive_scan_generation = generation;
    if (reset) selected->receive_next = 0;
    uint32_t ends[WORK_MAX_WORKERS];
    for (uint32_t w = 0; w < p->n_workers; w++) {
        uint32_t h = atomic_load_explicit(&p->mb[w].head, memory_order_relaxed);
        uint32_t t = atomic_load_explicit(&p->mb[w].tail, memory_order_acquire);
        uint32_t pos = selected->receive_pos[w];
        if (reset || pos - h >= t - h) pos = h;
        selected->receive_pos[w] = pos;
        ends[w] = t;
    }
    size_t delivered = 0, examined = 0;
    uint32_t empty = 0;
    while (examined < max_messages && empty < p->n_workers) {
        if (deadline_ns && now_ns() >= deadline_ns) break;
        uint32_t w = selected->receive_next;
        selected->receive_next = (w + 1u) % p->n_workers;
        work_mailbox *mb = &p->mb[w];
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        uint32_t pos = selected->receive_pos[w];
        /* Consuming a head can also reclaim previously consumed holes. */
        if (pos - h > WORK_MAILBOX_CAP) {
            pos = h;
            selected->receive_pos[w] = pos;
        }
        if (pos == ends[w]) { empty++; continue; }
        empty = 0;
        selected->receive_pos[w] = pos + 1u;
        examined++;
        if (mb->received[pos % WORK_MAILBOX_CAP]) continue;
        const work_msg *m = &mb->msgs[pos % WORK_MAILBOX_CAP];
        bool match = m->slot_ == handle.slot && m->epoch_ == handle.epoch &&
                     m->generation == generation;
        bool stale = atomic_load_explicit(&p->slots[m->slot_].epoch, memory_order_acquire) != m->epoch_;
        if (match || stale) {
            if (mailbox_consume(p, mb, pos, cb, ud) && match) delivered++;
        }
    }
    mailbox_end_drain(p);
    return delivered;
}

size_t work_mailbox_receive(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud)
{
    return work_mailbox_receive_bounded(p, h, generation, cb, ud,
                                        WORK_DRAIN_MAX_MESSAGES, now_ns() + WORK_DRAIN_BUDGET_NS);
}
