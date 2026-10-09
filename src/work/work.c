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
    if (n_bulk != 1 || n_raster > WORK_MAX_RASTER)
        return -1;
    *p = (work_pool){0};
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        atomic_init(&p->slots[i].epoch, 1u);
    p->n_bulk = n_bulk;
    p->n_workers = n_bulk + n_raster;
    p->efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (p->efd < 0)
        return -1;
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv[0], NULL);
    pthread_cond_init(&p->cv[1], NULL);
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
}

void work_pool_shutdown(work_pool *p)
{
    if (p->efd < 0)
        return;
    pthread_mutex_lock(&p->mu);
    p->shutting_down = true;
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        atomic_fetch_add_explicit(&p->slots[i].epoch, 1u, memory_order_acq_rel);
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
    if (job.fn == NULL || (cls == 1u && p->n_workers <= p->n_bulk))
        return h;
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) {
        work_slot *s = &p->slots[i];
        if (atomic_load_explicit(&s->busy, memory_order_acquire) ||
            atomic_load_explicit(&s->pending, memory_order_acquire))
            continue;
        s->job = job;
        uint32_t ep = atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel) + 1u;
        if (ep == 0) /* wrapped: epoch 0 is the invalid handle */
            ep = atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel) + 1u;
        atomic_store_explicit(&s->cancel_ns, 0, memory_order_relaxed);
        pthread_mutex_lock(&p->mu);
        if (p->shutting_down) {
            pthread_mutex_unlock(&p->mu);
            return h;
        }
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
    return h;
}

void work_cancel(work_pool *p, work_handle h)
{
    if (h.epoch == 0 || h.slot >= WORK_MAX_JOBS)
        return;
    work_slot *s = &p->slots[h.slot];
    uint32_t expect = h.epoch;
    if (atomic_load_explicit(&s->epoch, memory_order_acquire) != expect)
        return;
    /* Record time first; the epoch bump (release) publishes it. */
    atomic_store_explicit(&s->cancel_ns, now_ns(), memory_order_relaxed);
    (void)atomic_compare_exchange_strong_explicit(&s->epoch, &expect, expect + 1u,
        memory_order_acq_rel, memory_order_relaxed);
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
    uint64_t one = 1;
    ssize_t r = write(p->efd, &one, sizeof one);
    (void)r;
    return true;
}

size_t work_mailbox_drain(work_pool *p, void (*cb)(const work_msg *, void *), void *ud)
{
    uint64_t v;
    ssize_t r = read(p->efd, &v, sizeof v);
    (void)r;
    size_t n = 0;
    for (uint32_t w = 0; w < p->n_workers; w++) {
        work_mailbox *mb = &p->mb[w];
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        uint32_t t = atomic_load_explicit(&mb->tail, memory_order_acquire);
        while (h != t) {
            work_msg m = mb->msgs[h % WORK_MAILBOX_CAP];
            h++;
            atomic_store_explicit(&mb->head, h, memory_order_release);
            work_slot *s = &p->slots[m.slot_];
            if (atomic_load_explicit(&s->epoch, memory_order_acquire) != m.epoch_) {
                atomic_fetch_add_explicit(&p->dropped_stale, 1, memory_order_relaxed);
            } else {
                cb(&m, ud);
                n++;
            }
            /* Release the slot for reuse only now: until here its epoch could
             * not have been bumped by a new submit (P1.8b). */
            atomic_fetch_sub_explicit(&s->pending, 1u, memory_order_release);
        }
    }
    return n;
}
