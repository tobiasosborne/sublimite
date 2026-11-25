#include "work/work.h"
#include "trace/trace.h"

#include "os/os.h"

static uint64_t now_ns(void)
{
    return os_now_ns();
}

static void mailbox_notify(const work_pool *p)
{
    os_wake_signal(&p->wake);
}

static work_queue *class_queue(work_pool *p, uint32_t cls)
{
    return cls == (uint32_t)WORK_FOREGROUND ? &p->foreground_queue : &p->queue[cls];
}

static os_cond *class_condition(work_pool *p, uint32_t cls)
{
    return cls == (uint32_t)WORK_FOREGROUND ? &p->foreground_cv : &p->cv[cls];
}

static uint32_t mailbox_count(const work_pool *p)
{
    return p->n_workers + (p->foreground_enabled ? 1u : 0u);
}

/* UI cursors enumerate the foreground mailbox immediately after the legacy
 * mailboxes; the worker has a fixed identity outside the legacy array. */
static work_mailbox *mailbox_at(work_pool *p, uint32_t pos)
{
    return pos == p->n_workers ? &p->foreground_mb : &p->mb[pos];
}

static uint32_t *receive_cursor(work_pool *p, work_slot *s, uint32_t pos)
{
    return pos == p->n_workers ? &s->receive_foreground_pos : &s->receive_pos[pos];
}

static work_mailbox *worker_mailbox(work_pool *p, uint32_t index)
{
    return index == WORK_FOREGROUND_WORKER ? &p->foreground_mb : &p->mb[index];
}

static void queue_push(work_queue *q, uint32_t slot, uint32_t epoch)
{
    uint32_t pos = (q->head + q->count) % WORK_MAX_JOBS;
    q->q[pos] = slot;
    q->epochs[pos] = epoch;
    q->count++;
}

static void queue_remove(work_queue *q, uint32_t offset)
{
    if (offset == 0) {
        q->head = (q->head + 1u) % WORK_MAX_JOBS;
    } else {
        for (uint32_t next = offset + 1u; next < q->count; next++) {
            uint32_t src = (q->head + next) % WORK_MAX_JOBS;
            uint32_t dst = (q->head + next - 1u) % WORK_MAX_JOBS;
            q->q[dst] = q->q[src];
            q->epochs[dst] = q->epochs[src];
        }
    }
    q->count--;
}

static void wake_space_waiter(const work_worker *w)
{
    switch (w->wait_mode) {
    case WORK_WAIT_CLASS:
        break;
    case WORK_WAIT_SPACE:
        os_wake_signal(&w->wake);
        break;
    }
}

static void wake_class(work_pool *p, uint32_t cls, bool all)
{
    os_cond *cv = class_condition(p, cls);
    if (all) os_cond_broadcast(cv);
    else os_cond_signal(cv);

    if (cls == (uint32_t)WORK_FOREGROUND) {
        wake_space_waiter(&p->workers[WORK_FOREGROUND_WORKER]);
        return;
    }
    for (uint32_t i = 0; i < p->n_workers; i++) {
        work_worker *w = &p->workers[i];
        if (w->cls == cls) wake_space_waiter(w);
    }
}

static void retire_slot(work_slot *s, uint32_t epoch)
{
    s->space = WORK_SPACE_NONE;
    atomic_store_explicit(&s->busy, 0, memory_order_release);
    atomic_store_explicit(&s->finished_epoch, epoch, memory_order_release);
}

static bool mailbox_has_space(const work_mailbox *mb)
{
    uint32_t t = atomic_load_explicit(&mb->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&mb->head, memory_order_acquire);
    return t - h < WORK_MAILBOX_CAP;
}

static bool retry_has_space(work_mailbox *mb)
{
    if (mailbox_has_space(mb)) return true;
    /* The UI exchanges the same state after releasing head. If it already
     * reclaimed capacity, the recheck sees that release. Otherwise it takes
     * the arm and signals a wake that survives until the worker waits. */
    atomic_exchange_explicit(&mb->space_arm, WORK_MAILBOX_ARMED, memory_order_acq_rel);
    return mailbox_has_space(mb);
}

static uint32_t first_runnable(work_pool *p, const work_queue *q, uint32_t worker,
                               work_wait_mode *wait_mode)
{
    for (uint32_t offset = 0; offset < q->count; offset++) {
        uint32_t pos = (q->head + offset) % WORK_MAX_JOBS;
        work_slot *s = &p->slots[q->q[pos]];
        switch (s->space) {
        case WORK_SPACE_NONE:
            return offset;
        case WORK_SPACE_PARKED:
            if (worker != s->mailbox_worker || *wait_mode == WORK_WAIT_SPACE) break;
            if (retry_has_space(worker_mailbox(p, worker))) return offset;
            *wait_mode = WORK_WAIT_SPACE;
            break;
        case WORK_SPACE_REFUSED_FULL:
            ASSERT(false); /* Refusal belongs to a dequeued invocation. */
            break;
        }
    }
    return q->count;
}

static void wait_for_space(work_worker *w)
{
    work_pool *p = w->pool;
    w->wait_mode = WORK_WAIT_SPACE;
    os_mutex_unlock(&p->mu);
    os_wait_event event;
    size_t count;
    ASSERT(os_waitset_wait(&w->waitset, OS_DEADLINE_NEVER, &event, 1, &count) == OS_OK);
    os_wake_drain(&w->wake);
    os_mutex_lock(&p->mu);
    w->wait_mode = WORK_WAIT_CLASS;
}

static void worker_main(void *vp)
{
    work_worker *w = vp;
    work_pool *p = w->pool;
    work_queue *q = class_queue(p, w->cls);
    os_cond *cv = class_condition(p, w->cls);

    /* Trace exhaustion disables recording for this worker. Work still runs. */
    int trace_slot = trace_thread_register();
    os_mutex_lock(&p->mu);
    while (!p->shutting_down) {
        work_wait_mode wait_mode = WORK_WAIT_CLASS;
        uint32_t offset = first_runnable(p, q, w->index, &wait_mode);
        if (offset == q->count) {
            switch (wait_mode) {
            case WORK_WAIT_CLASS:
                os_cond_wait(cv, &p->mu);
                break;
            case WORK_WAIT_SPACE:
                wait_for_space(w);
                break;
            }
            continue;
        }
        uint32_t pos = (q->head + offset) % WORK_MAX_JOBS;
        uint32_t si = q->q[pos], ep = q->epochs[pos];
        queue_remove(q, offset);
        work_slot *s = &p->slots[si];
        s->space = WORK_SPACE_NONE;
        /* On migration, acquire the UI's last reservation release, including
         * the callback, before the next invocation can publish. */
        bool deferred = w->index != s->mailbox_worker &&
            atomic_load_explicit(&s->pending, memory_order_acquire) != 0;
        if (!deferred) s->mailbox_worker = w->index;
        os_mutex_unlock(&p->mu);

        work_ctx c = { p, s, ep, s->job.generation, w->index, s->job.arg, false };
        if (!deferred && !work_should_stop(&c))
            s->job.fn(&c);
        os_mutex_lock(&p->mu);
        /* Requeue and cancellation share the dequeue mutex. In particular,
         * cancellation during the final CPU slice cannot resurrect a lease. */
        if ((deferred || c.continue_) && !p->shutting_down && !work_should_stop(&c)) {
            queue_push(q, si, ep);
            switch (s->space) {
            case WORK_SPACE_NONE:
                if (!deferred) wake_class(p, w->cls, false);
                break;
            case WORK_SPACE_REFUSED_FULL:
                s->space = WORK_SPACE_PARKED;
                break;
            case WORK_SPACE_PARKED:
                ASSERT(false); /* A running invocation cannot be parked. */
                break;
            }
        } else {
            retire_slot(s, ep);
            mailbox_notify(p);
        }
    }
    os_mutex_unlock(&p->mu);
    if (trace_slot >= 0)
        trace_thread_release();
}

static int worker_start(work_pool *p, uint32_t index, uint32_t cls)
{
    work_worker *w = &p->workers[index];
    *w = (work_worker){.pool = p, .index = index, .cls = cls, .waitset = OS_WAITSET_INIT};
    if (os_wake_init(&w->wake) != OS_OK) return -1;
    if (os_waitset_init(&w->waitset) == OS_OK &&
        os_waitset_add(&w->waitset, os_wake_fd(&w->wake), OS_WAIT_IN, 1) == OS_OK &&
        os_thread_start(&w->thread, worker_main, w) == OS_OK)
        return 0;
    os_waitset_close(&w->waitset);
    os_wake_close(&w->wake);
    return -1;
}

static void worker_join(work_worker *w)
{
    (void)os_thread_join(&w->thread);
    os_waitset_close(&w->waitset);
    os_wake_close(&w->wake);
}

/* A handle is a submit result: {0, 0} when refused, otherwise an issued slot
 * and epoch. Submission never issues UINT32_MAX (a slot retires first), so
 * anything else is forged by the caller. */
static void assert_handle(work_handle h)
{
    ASSERT(h.slot < WORK_MAX_JOBS && h.epoch != UINT32_MAX);
}

static int pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster, bool foreground)
{
    ASSERT(n_bulk == 1 && n_raster <= WORK_MAX_RASTER);
    *p = (work_pool){0};
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++)
        atomic_init(&p->slots[i].epoch, 1u);
    p->n_bulk = n_bulk;
    if (os_wake_init(&p->wake) != OS_OK)
        return -1;
    if (os_mutex_init(&p->mu) != OS_OK)
        goto fail_wake;
    if (os_cond_init(&p->cv[0]) != OS_OK)
        goto fail_mutex;
    if (os_cond_init(&p->cv[1]) != OS_OK)
        goto fail_bulk_cond;
    if (foreground && os_cond_init(&p->foreground_cv) != OS_OK)
        goto fail_raster_cond;
    p->active = true;
    p->foreground_enabled = foreground;
    os_mutex_lock(&p->mu);
    for (uint32_t i = 0; i < n_bulk + n_raster; i++) {
        if (worker_start(p, i, i < n_bulk ? WORK_BULK : WORK_RASTER) != 0)
            goto fail_workers;
        p->n_workers++;
    }
    if (foreground) {
        if (worker_start(p, WORK_FOREGROUND_WORKER, WORK_FOREGROUND) != 0)
            goto fail_workers;
        p->foreground_started = true;
    }
    os_mutex_unlock(&p->mu);
    return 0;

fail_workers:
    os_mutex_unlock(&p->mu);
    work_pool_shutdown(p);
    return -1;
fail_raster_cond:
    os_cond_destroy(&p->cv[1]);
fail_bulk_cond:
    os_cond_destroy(&p->cv[0]);
fail_mutex:
    os_mutex_destroy(&p->mu);
fail_wake:
    os_wake_close(&p->wake);
    return -1;
}

int work_pool_init(work_pool *p, uint32_t n_bulk, uint32_t n_raster)
{
    return pool_init(p, n_bulk, n_raster, false);
}

int work_pool_init_foreground(work_pool *p, uint32_t n_bulk, uint32_t n_raster)
{
    return pool_init(p, n_bulk, n_raster, true);
}

void work_pool_shutdown(work_pool *p)
{
    if (!p->active)
        return;
    os_mutex_lock(&p->mu);
    p->shutting_down = true;
    for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) {
        work_slot *s = &p->slots[i];
        if (atomic_load_explicit(&s->epoch, memory_order_relaxed) == UINT32_MAX)
            continue;
        atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel);
    }
    /* A dequeued job is owned by its worker until join. A queued job has no
     * worker owner and must be retired here, without invoking or freeing args. */
    for (uint32_t cls = 0; cls < (p->foreground_enabled ? 3u : 2u); cls++) {
        work_queue *q = class_queue(p, cls);
        while (q->count) {
            retire_slot(&p->slots[q->q[q->head]], q->epochs[q->head]);
            queue_remove(q, 0);
        }
        q->head = 0;
        wake_class(p, cls, true);
    }
    os_mutex_unlock(&p->mu);
    for (uint32_t i = 0; i < p->n_workers; i++)
        worker_join(&p->workers[i]);
    if (p->foreground_started) worker_join(&p->workers[WORK_FOREGROUND_WORKER]);
    p->foreground_started = false;
    os_wake_close(&p->wake);
    p->active = false;
    os_cond_destroy(&p->cv[0]);
    os_cond_destroy(&p->cv[1]);
    if (p->foreground_enabled) os_cond_destroy(&p->foreground_cv);
    os_mutex_destroy(&p->mu);
}

const os_wake *work_pool_wake(const work_pool *p) { return p->active ? &p->wake : NULL; }
static int work_submit_batch(work_pool *p, const work_job *jobs, size_t count, work_handle *handles)
{
    if (count == 0) return 0;
    ASSERT(count <= WORK_MAX_JOBS && jobs != NULL && handles != NULL);
    /* An init that failed on resources leaves the pool inactive. */
    if (!p->active)
        return -1;
    uint32_t needed[3] = {0};
    for (size_t j = 0; j < count; j++) {
        /* A class the caller did not configure is a caller bug. */
        ASSERT(jobs[j].fn != NULL);
        ASSERT(jobs[j].cls == WORK_BULK || jobs[j].cls == WORK_RASTER ||
               jobs[j].cls == WORK_FOREGROUND);
        ASSERT(jobs[j].cls != WORK_RASTER || p->n_workers > p->n_bulk);
        ASSERT(jobs[j].cls != WORK_FOREGROUND || p->foreground_enabled);
        needed[(uint32_t)jobs[j].cls]++;
    }
    os_mutex_lock(&p->mu);
    if (needed[0] > WORK_MAX_JOBS - p->queue[0].count ||
        needed[1] > WORK_MAX_JOBS - p->queue[1].count ||
        needed[2] > WORK_MAX_JOBS - p->foreground_queue.count) {
        os_mutex_unlock(&p->mu);
        return -1;
    }
    uint32_t raster_reserve = p->n_workers > p->n_bulk ? WORK_RASTER_RESERVE : 0u;
    uint32_t foreground_reserve = p->foreground_enabled ? WORK_FOREGROUND_RESERVE : 0u;
    uint32_t shared = WORK_MAX_JOBS - raster_reserve - foreground_reserve;
    uint32_t chosen[WORK_MAX_JOBS];
    bool reserved[WORK_MAX_JOBS] = {false};
    /* Bulk has the narrower eligible set. Each other class then prefers its
     * own reserve and can share bulk slots, never another class's reserve. */
    for (uint32_t cls = 0; cls < 3; cls++) {
        uint32_t reserve = cls == (uint32_t)WORK_RASTER ? raster_reserve :
                           cls == (uint32_t)WORK_FOREGROUND ? foreground_reserve : 0u;
        uint32_t start = cls == (uint32_t)WORK_RASTER ? shared + foreground_reserve : shared;
        uint32_t limit = shared + reserve;
        uint32_t scan = 0;
        for (size_t j = 0; j < count; j++) {
            if ((uint32_t)jobs[j].cls != cls) continue;
            bool found = false;
            for (; scan < limit; scan++) {
                uint32_t i = scan < reserve ? start + scan : scan - reserve;
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
                os_mutex_unlock(&p->mu);
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
        s->mailbox_worker = WORK_MAX_MAILBOXES;
        s->space = WORK_SPACE_NONE;
        /* Only a free slot can advance here. The queue mutex publishes the
         * job and its new epoch together. */
        uint32_t ep = atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel) + 1u;
        atomic_store_explicit(&s->busy, 1, memory_order_relaxed);
        s->receive_epoch = ep;
        s->receive_scan_generation = jobs[j].generation;
        s->receive_next = 0;
        for (uint32_t w = 0; w < mailbox_count(p); w++)
            *receive_cursor(p, s, w) = atomic_load_explicit(&mailbox_at(p, w)->head, memory_order_relaxed);
        handles[j] = (work_handle){i, ep};
    }
    for (size_t j = 0; j < count; j++) {
        work_queue *q = class_queue(p, (uint32_t)jobs[j].cls);
        queue_push(q, handles[j].slot, handles[j].epoch);
    }
    for (uint32_t cls = 0; cls < 3; cls++)
        if (needed[cls]) wake_class(p, cls, true);
    os_mutex_unlock(&p->mu);
    return 0;
}

work_handle work_submit(work_pool *p, work_job job)
{
    work_handle h = {0, 0};
    (void)work_submit_batch(p, &job, 1, &h);
    return h;
}

bool work_handle_finished(const work_pool *p, work_handle h)
{
    assert_handle(h);
    if (h.epoch == 0 || !p->active)
        return true;
    return atomic_load_explicit(&p->slots[h.slot].finished_epoch, memory_order_acquire) >= h.epoch;
}

void work_cancel(work_pool *p, work_handle h)
{
    assert_handle(h);
    if (!p->active || h.epoch == 0)
        return;
    os_mutex_lock(&p->mu);
    work_slot *s = &p->slots[h.slot];
    uint32_t expect = h.epoch;
    if (atomic_load_explicit(&s->epoch, memory_order_acquire) != expect) {
        os_mutex_unlock(&p->mu);
        return;
    }
    /* Record time first; the epoch bump (release) publishes it. */
    (void)atomic_compare_exchange_strong_explicit(&s->epoch, &expect, expect + 1u,
        memory_order_acq_rel, memory_order_relaxed);
    /* Serialize removal with worker dequeue: only an entry still in the queue
     * may release busy here. Compact in place to preserve FIFO ordering. */
    uint32_t cls = (uint32_t)s->job.cls;
    work_queue *q = class_queue(p, cls);
    for (uint32_t offset = 0; offset < q->count; offset++) {
        uint32_t pos = (q->head + offset) % WORK_MAX_JOBS;
        if (q->q[pos] != h.slot || q->epochs[pos] != h.epoch)
            continue;
        queue_remove(q, offset);
        retire_slot(s, h.epoch);
        mailbox_notify(p);
        break;
    }
    os_mutex_unlock(&p->mu);
}

bool work_should_stop(const work_ctx *c)
{
    return atomic_load_explicit(&c->slot->epoch, memory_order_acquire) != c->epoch;
}

bool work_continue(work_ctx *c)
{
    c->continue_ = !work_should_stop(c);
    return c->continue_;
}

bool work_publish(work_ctx *c, const work_msg *m)
{
    work_pool *p = c->pool;
    c->slot->space = WORK_SPACE_NONE;
    if (work_should_stop(c)) {
        atomic_fetch_add_explicit(&p->dropped_stale, 1, memory_order_relaxed);
        return false;
    }
    work_mailbox *mb = worker_mailbox(p, c->worker);
    /* tail is single-producer. Acquire the UI's reclaimed head before
     * overwriting a ring entry that the consumer previously read. */
    uint32_t t = atomic_load_explicit(&mb->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&mb->head, memory_order_acquire);
    if (t - h >= WORK_MAILBOX_CAP) {
        c->slot->space = WORK_SPACE_REFUSED_FULL;
        atomic_fetch_add_explicit(&p->dropped_full, 1, memory_order_relaxed);
        return false;
    }
    work_msg *dst = &mb->msgs[t % WORK_MAILBOX_CAP];
    *dst = *m;
    dst->slot_ = (uint32_t)(c->slot - p->slots);
    dst->epoch_ = c->epoch;
    /* The release of tail publishes both the entry and its reservation. */
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
    if (p->foreground_enabled &&
        atomic_load_explicit(&p->foreground_mb.head, memory_order_relaxed) !=
        atomic_load_explicit(&p->foreground_mb.tail, memory_order_acquire))
        return true;
    return false;
}

static void mailbox_clear_notification(const work_pool *p)
{
    /* Clear before acquiring mailbox state, so a racing publisher either
     * leaves a wake or is observed by this drain (os.h wake protocol). */
    if (p->active) os_wake_drain(&p->wake);
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
        uint32_t w = mb == &p->foreground_mb ? p->n_workers : (uint32_t)(mb - p->mb);
        for (uint32_t i = 0; i < WORK_MAX_JOBS; i++) *receive_cursor(p, &p->slots[i], w) = next;
    }
    if (next == h) return;
    atomic_store_explicit(&mb->head, next, memory_order_release);
    if (!p->active) return;
    switch ((work_mailbox_arm)atomic_exchange_explicit(&mb->space_arm, WORK_MAILBOX_DISARMED,
                                                        memory_order_acq_rel)) {
    case WORK_MAILBOX_DISARMED:
        break;
    case WORK_MAILBOX_ARMED: {
        uint32_t worker = mb == &p->foreground_mb ? WORK_FOREGROUND_WORKER : (uint32_t)(mb - p->mb);
        os_wake_signal(&p->workers[worker].wake);
        break;
    }
    }
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
    } else {
        cb(&m, ud);
    }
    /* Keep the result/slot lease throughout the callback, even if it submits.
     * The release pairs with slot selection or a continuation on another worker. */
    atomic_fetch_sub_explicit(&s->pending, 1u, memory_order_release);
    return live;
}

static void mailbox_end_drain(work_pool *p)
{
    if (p->active && work_mailbox_pending(p)) mailbox_notify(p);
    p->draining = false;
}

static size_t work_mailbox_drain_bounded(work_pool *p, void (*cb)(const work_msg *, void *),
                                  void *ud, size_t max_messages, uint64_t deadline_ns)
{
    ASSERT(cb != NULL);
    if (p->draining) return 0;
    p->draining = true;
    mailbox_clear_notification(p);
    size_t n = 0, examined = 0;
    uint32_t empty = 0;
    uint32_t mailboxes = mailbox_count(p);
    while (examined < max_messages && empty < mailboxes) {
        if (deadline_ns && now_ns() >= deadline_ns)
            break;
        uint32_t w = p->drain_next;
        p->drain_next = (w + 1u) % mailboxes;
        work_mailbox *mb = mailbox_at(p, w);
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        /* Acquire the producer's payload and reservation before consuming. */
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

static size_t work_mailbox_receive_bounded(work_pool *p, work_handle handle, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud, size_t max_messages, uint64_t deadline_ns)
{
    ASSERT(cb != NULL);
    assert_handle(handle);
    if (p->draining || !handle.epoch)
        return 0;
    work_slot *selected = &p->slots[handle.slot];
    /* A reused slot cannot have outstanding messages from its old lease. */
    if (selected->receive_epoch != handle.epoch) return 0;
    p->draining = true;
    mailbox_clear_notification(p);
    bool reset = selected->receive_scan_generation != generation;
    selected->receive_scan_generation = generation;
    if (reset) selected->receive_next = 0;
    uint32_t ends[WORK_MAX_MAILBOXES];
    uint32_t mailboxes = mailbox_count(p);
    for (uint32_t w = 0; w < mailboxes; w++) {
        work_mailbox *mb = mailbox_at(p, w);
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        /* This acquire publishes the bounded snapshot's entries to the scan. */
        uint32_t t = atomic_load_explicit(&mb->tail, memory_order_acquire);
        uint32_t pos = *receive_cursor(p, selected, w);
        if (reset || pos - h >= t - h) pos = h;
        *receive_cursor(p, selected, w) = pos;
        ends[w] = t;
    }
    size_t delivered = 0, examined = 0;
    uint32_t empty = 0;
    while (examined < max_messages && empty < mailboxes) {
        if (deadline_ns && now_ns() >= deadline_ns) break;
        uint32_t w = selected->receive_next;
        selected->receive_next = (w + 1u) % mailboxes;
        work_mailbox *mb = mailbox_at(p, w);
        uint32_t h = atomic_load_explicit(&mb->head, memory_order_relaxed);
        uint32_t pos = *receive_cursor(p, selected, w);
        /* Consuming a head can also reclaim previously consumed holes. */
        if (pos - h > WORK_MAILBOX_CAP) {
            pos = h;
            *receive_cursor(p, selected, w) = pos;
        }
        if (pos == ends[w]) { empty++; continue; }
        empty = 0;
        *receive_cursor(p, selected, w) = pos + 1u;
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

#ifdef WORK_TEST_HOOKS
int work_test_submit_batch(work_pool *p, const work_job *jobs, size_t count, work_handle *handles)
{
    return work_submit_batch(p, jobs, count, handles);
}

size_t work_test_mailbox_drain_bounded(work_pool *p, void (*cb)(const work_msg *, void *),
                                       void *ud, size_t max_messages, uint64_t deadline_ns)
{
    return work_mailbox_drain_bounded(p, cb, ud, max_messages, deadline_ns);
}

size_t work_test_mailbox_receive_bounded(work_pool *p, work_handle h, uint32_t generation,
    void (*cb)(const work_msg *, void *), void *ud, size_t max_messages, uint64_t deadline_ns)
{
    return work_mailbox_receive_bounded(p, h, generation, cb, ud, max_messages, deadline_ns);
}
#endif
