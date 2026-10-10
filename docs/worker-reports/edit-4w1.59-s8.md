# edit-4w1.59 — worker snapshot retirement without typing locks

P1-1 §13. The parked-worker red regression proves the inherited synchronous
snapshot release holds a mutex required by typing. Use deferred physical
retirement, keeping the pool's compact representation and existing ownership
model. No other review finding is included.

## Ownership and synchronization

A snapshot's final worker owner atomically drops its reference, publishes the
header to a per-core Treiber stack, then drops its core reference. Publication
uses release ordering and collection uses acquire ordering. No producer reads
its header after publication, so owner cleanup may immediately reuse it. A
zero-reference header's former length word stores its queue link; its root,
original and ADD view remain retained. No header growth or queue allocation.

The tree or another active snapshot keeps the core alive after publication.
The live-owner flag is cleared before the tree drops its core owner; releases
after destruction never inspect an expired owning-thread ID, including when
that thread has exited and its identifier is reused. The flag fits existing
core padding. If the publishing worker drops the last core owner, no UI operation or other
producer remains; that worker exclusively drains the queue before destroying
storage. A publisher parked before publication still holds its core owner,
including when the tree is destroyed. This removes the worker-held UI mutex
rather than replacing it with a spinning or retrying foreground path.

Only the owner (or exclusive final-core thread) touches pool occupancy, slab
lists and the ADD snapshot list. Existing recursive pool locks may remain for
owner operations; worker release with a live tree acquires neither piece lock.
Pointer/reference atomics must be always lock-free at compile time.

Owner maintenance detaches pending headers and decrements roots incrementally.
Zero-reference nodes use their dead ADD-high word as an intrusive work stack.
A step handles one header or one node, with at most the fixed fanout of child
references. Automatic owner endpoints consume 64 steps (G); explicit
`piece_reclaim(t, budget)` consumes the requested graph budget and at most
64 completed slabs (G). Backing-view pruning and sized allocator frees retain
their existing costs; this graph-work bound is not an end-to-end CPU deadline.
Node retirement reads tree structure only, so backing stores may be returned
once no physical snapshot header owns them. The tree's current cached header
and direct owner releases preserve synchronous contracts. Destruction drains
all deferred work and trims unused reserves before transferring the remaining
snapshot-only core ownership.

A logical worker release transfers physical ownership, rather than destroying
it. Pending storage remains charged until maintenance finishes. The worker
memory regression explicitly waits for that completion before comparing its
unchanged G10f bound, without using a content query. An idle-service caller can
use `piece_reclaim` until it returns zero; automatic edit/query/take batches
also provide progress. General editor idle-service wiring is outside this
piece-only change. The new declaration is an additive maintenance extension
in the module's one public header; existing API signatures and layouts stay.

## Measurement choice

Keep the original frozen competition bench unchanged. Add
`piece_reclaim_bench --quick` as a separate memory-only TRACK row. Its fixture
retires one or three snapshots while sampling insert/delete; it records the
first edit separately so a single blocked edit cannot vanish in percentiles.
The deterministic parked-worker test establishes lock independence; timing
observations on this loaded box cannot establish G1 input-to-present passage.

Back-to-back before/after measurements, 2026-10-10T04:42:56Z (M)[AC], power
Not charging, load averages 18.16 / 14.63 / 10.51 (M)[AC]. The before library is
the inherited WIP before the fix; both variants use the same bench implementation.
This final pair supersedes an earlier pair collected before the owning-thread
lifetime guard was added; the additional pair follows that implementation change.
Quick fixture: 20,000 pieces, 16 repetitions (G fixture).

| TRACK cell | Before p50 / p99, us (M)[AC] | After p50 / p99, us (M)[AC] |
|---|---:|---:|
| One worker, first edit | 0.934 / 170.039 | 0.503 / 11.237 |
| One worker, insert + delete | 0.150 / 138.338 | 0.156 / 8.808 |
| Three workers, first edit | 17.196 / 230.095 | 1.090 / 10.100 |
| Three workers, insert + delete | 0.145 / 112.506 | 0.154 / 10.100 |

Ordinary typing row, also paired in the same minute,
2026-10-10T04:42:56Z (M)[AC], power Not charging, load averages
18.16 / 14.63 / 10.51 (M)[AC]:

| Quick typing cell | Before (M)[AC] | After (M)[AC] |
|---|---:|---:|
| Insert p50 / p99, us | 0.039 / 0.234 | 0.046 / 0.366 |
| Delete p50 / p99, us | 0.104 / 0.780 | 0.102 / 0.668 |
| Batch p50 / p99, ms | 0.013 / 0.016 | 0.013 / 0.017 |
| Peak owned bytes | 1,118,280 | 1,118,312 |

Interpretation: the structural test proves the contention fix; the paired
TRACK samples are consistent with removal of worker-retirement tail stalls.
The ordinary row provides no evidence of a material hot-path regression.
Core ownership/queue fields add 32 bytes (M)[AC] in the release fixture;
leaf, branch, snapshot and slab geometry remain unchanged. These timing
comparisons are observations, not quiet-box or loaded-box gate verdicts.

Red/green output and final validation: `docs/worker-reports/edit-4w1.59-s8.md`.
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/piece_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/piece_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/raster_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/raster_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/refwin_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/refwin_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/render_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/render_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/replay_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/replay_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/runtime_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/runtime_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/savectl_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/savectl_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/scan_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/scan_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/scroll_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/scroll_test
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/tabs_test.o build/san/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/tabs_test

diff --git a/bench/piece_reclaim_bench.args b/bench/piece_reclaim_bench.args
new file mode 100644
index 0000000000000000000000000000000000000000..6d23864c3c65f4e29e9c13b900ff14853719bb63
--- /dev/null
+++ b/bench/piece_reclaim_bench.args
@@ -0,0 +1 @@
+--quick
diff --git a/bench/piece_reclaim_bench.c b/bench/piece_reclaim_bench.c
new file mode 100644
index 0000000000000000000000000000000000000000..66ac247c45d79603bd70abd851a68e27e4f4bcd0
--- /dev/null
+++ b/bench/piece_reclaim_bench.c
@@ -0,0 +1,75 @@
+/* P1-1 §13: back-to-back kernel TRACK row, including worker retirement.
+ * G1 includes input/render, so this memory-only row reports no timing verdict. */
+#include "piece/piece.h"
+#include "harness.h"
+#include <pthread.h>
+#include <sched.h>
+#include <stdatomic.h>
+#include <stdlib.h>
+
+typedef struct release_job {
+    piece_snapshot *snapshot;
+    atomic_uint *ready;
+    atomic_int *go;
+} release_job;
+static void *release_worker(void *ctx) {
+    release_job *j = ctx;
+    atomic_fetch_add_explicit(j->ready, 1, memory_order_release);
+    while (!atomic_load_explicit(j->go, memory_order_acquire)) sched_yield();
+    piece_snapshot_release(j->snapshot);
+    return NULL;
+}
+static void require(int ok) {
+    if (!ok) { fputs("piece_reclaim_bench: fixture failed\n", stderr); exit(2); }
+}
+static void report(const char *col, unsigned workers, bench_samples *s) {
+    printf("BENCH row=worker_reclamation_typing workers=%u col=%s p50=%.3f p99=%.3f unit=us n=%zu gate=none status=TRACK (M)%s\n",
+           workers, col, (double)bench_p50(s) / 1000.0, (double)bench_p99(s) / 1000.0,
+           s->n, bench_evidence_tag());
+}
+int main(int argc, char **argv) {
+    int quick = argc == 2 && !strcmp(argv[1], "--quick");
+    require(argc == 1 || quick);
+    size_t pieces = quick ? 20000u : 200000u, repeats = quick ? 16u : 32u;
+    char power[32]; bench_battery_status(power, sizeof power);
+    printf("# worker_reclamation_typing pieces=%zu repeats=%zu power=%s (M)%s; G1=1/2ms (G), kernel samples TRACK\n",
+           pieces, repeats, power, bench_evidence_tag());
+    for (unsigned jobs = 1; jobs <= 3; jobs += 2) {
+        uint64_t first_data[32], typing_data[32 * 64];
+        bench_samples first, typing;
+        bench_samples_init(&first, first_data, repeats);
+        bench_samples_init(&typing, typing_data, repeats * 64);
+        for (size_t r = 0; r < repeats; r++) {
+            piece_allocator a = piece_default_allocator();
+            piece_tree *t = piece_create(&a); require(t != NULL);
+            for (size_t i = 0; i < pieces; i++) require(!piece_insert(t, 0, (const uint8_t *)"x", 1));
+            require(piece_piece_count(t) == pieces);
+            atomic_uint ready; atomic_init(&ready, 0);
+            atomic_int go; atomic_init(&go, 0);
+            release_job j[3]; pthread_t th[3];
+            for (unsigned i = 0; i < jobs; i++) {
+                j[i] = (release_job){piece_snapshot_take(t), &ready, &go};
+                require(j[i].snapshot != NULL);
+                require(!piece_insert(t, 0, (const uint8_t *)"+", 1));
+            }
+            require(!piece_delete(t, 0, piece_len(t), NULL));
+            require(!piece_insert(t, 0, (const uint8_t *)"x", 1));
+            for (unsigned i = 0; i < jobs; i++) require(!pthread_create(&th[i], NULL, release_worker, &j[i]));
+            while (atomic_load_explicit(&ready, memory_order_acquire) != jobs) sched_yield();
+            atomic_store_explicit(&go, 1, memory_order_release);
+            for (unsigned k = 0; k < 64; k++) {
+                uint64_t began = bench_now_ns();
+                require(!piece_insert(t, 1, (const uint8_t *)"\n", 1));
+                require(!piece_delete(t, 1, 1, NULL));
+                uint64_t elapsed = bench_now_ns() - began;
+                require(!bench_add(&typing, elapsed));
+                if (!k) require(!bench_add(&first, elapsed));
+            }
+            for (unsigned i = 0; i < jobs; i++) require(!pthread_join(th[i], NULL));
+            require(piece_len(t) == 1 && piece_line_count(t) == 1);
+            piece_destroy(t);
+        }
+        report("first_edit", jobs, &first); report("insert_delete", jobs, &typing);
+    }
+    return 0;
+}
diff --git a/fuzz/piece_fuzz.c b/fuzz/piece_fuzz.c
index 92f3288b2800d7f00216861c9eb96012924a860e..f3facf370d024aa9c745a2aa8404c3f941fc14c6
--- a/fuzz/piece_fuzz.c
+++ b/fuzz/piece_fuzz.c
@@ -2,10 +2,14 @@
 #include "piece/piece.h"
 #include "piece/piece_test.h"
 #include "../tests/piece_model.h"
+#include <pthread.h>
 #include <stdio.h>
 
 #define NEED(k) do { if (i + (k) > size) goto done; } while (0)
 #define FAIL() __builtin_trap()
+static void *retire_snapshot(void *ctx) {
+    piece_snapshot_release(ctx); return NULL;
+}
 
 static uint64_t wide_decode(const uint8_t *p) {
     uint64_t v = 0;
@@ -116,7 +120,7 @@
     piece_ref saved[4] = {0}; uint8_t saved_bytes[4][256];
     size_t saved_len[4] = {0}; unsigned save_slot = 0;
     while (i < size) {
-        uint8_t op = data[i++] % 11;
+        uint8_t op = data[i++] % 12;
         if (op == 0 || op == 1) {            /* insert */
             NEED(3); uint64_t off = ((uint64_t)data[i] << 8 | data[i + 1]) % (m.n + 1); size_t l = data[i + 2] % 40; i += 3;
             NEED(l ? 1 : 0); uint8_t b[40];
@@ -177,6 +181,16 @@
                 if (piece_insert_ref(t, off, &saved[slot])) FAIL();
                 pm_insert(&m, off, saved_bytes[slot], saved_len[slot]);
             }
+        } else if (op == 11) {                /* worker retirement / bounded owner drain */
+            NEED(1); size_t budget = data[i++] % 65u;
+            piece_snapshot *retire = piece_snapshot_take(t); if (!retire) FAIL();
+            if (piece_insert(t, 0, (const uint8_t *)"x", 1)) FAIL();
+            pm_insert(&m, 0, (const uint8_t *)"x", 1);
+            pthread_t worker;
+            if (pthread_create(&worker, NULL, retire_snapshot, retire)) FAIL();
+            (void)piece_reclaim(t, budget);
+            if (pthread_join(worker, NULL)) FAIL();
+            (void)piece_reclaim(t, budget);
         } else {                             /* out-of-range must fail */
             uint8_t c; if (piece_read(t, m.n, &c, 1) == 0) FAIL();
             if (piece_delete(t, m.n, 1, NULL) == 0) FAIL();
diff --git a/src/piece/piece.c b/src/piece/piece.c
index a0b8009743a1b8ac0beab061330b81e1289981ad..37d8752eda493efd6a931dc7b4dfc37e54d1265e
--- a/src/piece/piece.c
+++ b/src/piece/piece.c
@@ -41,6 +41,9 @@
 #define NL_BLOCKS (65536u / NL_BLOCK)
 #define REF_BATCH (FAN - 2u)
 #define TRIM_QUERY 3
+#define TRIM_MAINT 4
+_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2,
+               "snapshot release queue and owner counts must be lock-free");
 
 typedef struct node node;
 struct node {
@@ -101,9 +104,14 @@
     piece_allocator a;
     piece_map_hooks mh; int has_mh;
     const uint8_t *orig; size_t orig_len; int orig_owned;
+    atomic_int owner_live; /* do not inspect pthread_t after its tree dies */
     add_store *add; orig_store *original;
     pthread_mutex_t pool_mu;
     node_pool pool[3]; /* leaves, branches, compact snapshot headers */
+    pthread_t owner;
+    _Atomic(piece_snapshot *) released;
+    piece_snapshot *pending; /* owner-only detached release queue */
+    node *retiring; /* owner-only zero-reference nodes, linked in add_high */
 #ifdef PIECE_TESTING
     piece_test_stats stats;
     void (*reclaim_hook)(void *);
@@ -275,7 +283,7 @@
         atomic_load_explicit(&c->pool[1].returned, memory_order_relaxed) < 64) return;
     /* Queries/takes consume a fixed batch even if workers queued a large
      * retired version. Mutation/release cleanup follows its reclaimed work. */
-    size_t budget = force == TRIM_QUERY ? 64 : SIZE_MAX;
+    size_t budget = force == TRIM_QUERY || force == TRIM_MAINT ? 64 : SIZE_MAX;
     for (unsigned pi = 0; pi < 3; pi++) {
         node_pool *p = &c->pool[pi];
         size_t sz = 2 * slot_size(pi) + SLAB_OVERHEAD;
@@ -440,12 +448,12 @@
     add_chunk *old = tbl[i]; tbl[i] = ch; chunk_unref(c, old);
     cp->boundary_private = 1; return 0;
 }
+static void reclaim_drain(core *c, size_t budget);
 static void core_unref(core *c) {
-    pthread_mutex_lock(&c->pool_mu);
-    unsigned owners = atomic_fetch_sub_explicit(&c->rc, 1, memory_order_acq_rel);
-    if (owners == 2) pool_trim_locked(c, 1);
-    pthread_mutex_unlock(&c->pool_mu);
-    if (owners != 1) return;
+    if (atomic_fetch_sub_explicit(&c->rc, 1, memory_order_acq_rel) != 1) return;
+    /* Every producer publishes before dropping its core owner. At zero there
+     * is no UI or producer left, so the final thread owns all deferred work. */
+    reclaim_drain(c, SIZE_MAX);
     piece_allocator a = c->a;
     add_store_unref(c, c->add);
     orig_store_unref(c, c->original);
@@ -460,6 +468,49 @@
     pthread_mutex_destroy(&c->pool_mu); a.free(a.ctx, c, sizeof *c);
 }
 
+/* A dead snapshot has no readers: its length becomes an intrusive release
+ * link, without growing the frozen compact header or allocating queue cells. */
+static piece_snapshot *released_next(const piece_snapshot *s) {
+    return (piece_snapshot *)(uintptr_t)s->len;
+}
+static void retire_node(core *c, node *n) {
+    if (atomic_fetch_sub_explicit(&n->rc, 1, memory_order_acq_rel) != 1) return;
+    n->nextfree = c->retiring; c->retiring = n;
+}
+static void reclaim_drain(core *c, size_t budget) {
+    while (budget) {
+        if (c->retiring) {
+            node *n = c->retiring; c->retiring = n->nextfree;
+            if (!n->leaf) for (unsigned i = 0; i < n->cnt; i++) retire_node(c, n->v.in.ch[i]);
+            pool_free(c, n, n->leaf ? 0u : 1u);
+#ifdef PIECE_TESTING
+            c->stats.reclaim_steps++;
+#endif
+        } else {
+            if (!c->pending) c->pending = atomic_exchange_explicit(&c->released, NULL, memory_order_acquire);
+            piece_snapshot *s = c->pending;
+            if (!s) break;
+            c->pending = released_next(s);
+            add_store *v = s->add;
+            /* Workers never touch the view list or its lock. The owner (or
+             * exclusive final-core thread) alone removes released headers. */
+            pthread_mutex_lock(&v->mu);
+            if (s->prev) s->prev->next = s->next; else v->snapshots = s->next;
+            if (s->next) s->next->prev = s->prev;
+            add_store_prune(c, v);
+            pthread_mutex_unlock(&v->mu);
+            retire_node(c, s->root);
+            orig_store_unref(c, s->original);
+            pool_free(c, (node *)(void *)s, 2);
+            add_store_unref(c, v);
+#ifdef PIECE_TESTING
+            c->stats.reclaim_steps++;
+#endif
+        }
+        budget--;
+    }
+}
+
 /* An unchanged tree shares one immutable snapshot header. Each public owner
  * still gets its own mapping acquire/release; the tree's cache is covered by
  * the tree's mapping owner. This bounds repeated takes without any edits. */
@@ -467,8 +518,22 @@
     if (!s) return;
     core *c = s->c;
     if (external && s->has_mh) s->original->mh.release(s->original->mh.ctx);
+    int no_external = external && atomic_fetch_sub_explicit(&c->external, 1, memory_order_acq_rel) == 1;
+    if (!atomic_load_explicit(&c->owner_live, memory_order_acquire) ||
+        !pthread_equal(pthread_self(), c->owner)) {
+        if (atomic_fetch_sub_explicit(&s->rc, 1, memory_order_acq_rel) != 1) return;
+#ifdef PIECE_TESTING
+        if (c->reclaim_hook) c->reclaim_hook(c->reclaim_ctx);
+#endif
+        piece_snapshot *head = atomic_load_explicit(&c->released, memory_order_relaxed);
+        do { s->len = (uint64_t)(uintptr_t)head; }
+        while (!atomic_compare_exchange_weak_explicit(&c->released, &head, s,
+                                                      memory_order_release, memory_order_relaxed));
+        /* Deferred storage now belongs to the core, whose tree or remaining
+         * snapshot owners keep it alive. The final core owner drains it. */
+        core_unref(c); return;
+    }
     pthread_mutex_lock(&c->pool_mu);
-    int no_external = external && atomic_fetch_sub_explicit(&c->external, 1, memory_order_acq_rel) == 1;
     if (atomic_fetch_sub_explicit(&s->rc, 1, memory_order_acq_rel) != 1) {
         if (no_external) pool_trim_locked(c, 2);
         pthread_mutex_unlock(&c->pool_mu); return;
@@ -480,9 +545,6 @@
     add_store_prune(c, v);
     pthread_mutex_unlock(&v->mu);
     orig_store_unref(c, s->original);
-#ifdef PIECE_TESTING
-    if (c->reclaim_hook) c->reclaim_hook(c->reclaim_ctx);
-#endif
     node_unref(c, s->root); pool_free(c, (node *)(void *)s, 2);
     add_store_unref(c, v);
     if (no_external) pool_trim_locked(c, 2);
@@ -495,6 +557,7 @@
 
 static void owner_trim(piece_tree *t, int force) {
     core *c = t->c;
+    reclaim_drain(c, 64);
     if (!force && atomic_load_explicit(&c->pool[0].returned, memory_order_relaxed) < 64 &&
         atomic_load_explicit(&c->pool[1].returned, memory_order_relaxed) < 64) return;
     if (t->cached_snapshot && atomic_load_explicit(&t->cached_snapshot->rc, memory_order_acquire) == 1)
@@ -502,6 +565,14 @@
     pool_trim(c, TRIM_QUERY);
 }
 
+int piece_reclaim(piece_tree *t, size_t budget) {
+    core *c = t->c;
+    reclaim_drain(c, budget);
+    pool_trim(c, TRIM_MAINT);
+    return c->retiring || c->pending || atomic_load_explicit(&c->released, memory_order_acquire) ||
+           c->pool[0].empty || c->pool[1].empty || c->pool[2].empty;
+}
+
 static inline const uint8_t *view_data(const uint8_t *original, const add_store *v, uint64_t x) {
     if (x & TOP) {
         uint64_t a = x & ~TOP;
@@ -1030,6 +1101,8 @@
     core *c = a->alloc(a->ctx, sizeof *c);
     if (!c) return NULL;
     memset(c, 0, sizeof *c); atomic_init(&c->rc, 1); atomic_init(&c->external, 0); c->a = *a;
+    c->owner = pthread_self(); atomic_init(&c->released, NULL);
+    atomic_init(&c->owner_live, 1);
     pthread_mutexattr_t attr;
     if (pthread_mutexattr_init(&attr)) { a->free(a->ctx, c, sizeof *c); return NULL; }
     int lock_error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
@@ -1061,7 +1134,10 @@
     add_store *v = c->add; c->add = NULL;
     pthread_mutex_lock(&v->mu); v->sealed = 1; add_store_prune(c, v); pthread_mutex_unlock(&v->mu);
     add_store_unref(c, v);
+    reclaim_drain(c, SIZE_MAX);
+    pool_trim(c, 1);
     c->a.free(c->a.ctx, t, sizeof *t);
+    atomic_store_explicit(&c->owner_live, 0, memory_order_release);
     core_unref(c);
 }
 
@@ -1474,6 +1550,7 @@
     s.slabs_scanned = c->stats.slabs_scanned; s.pool_returns = c->stats.pool_returns;
     s.walk_nodes = __atomic_load_n(&c->stats.walk_nodes, __ATOMIC_RELAXED);
     s.ref_recount_bytes = c->stats.ref_recount_bytes;
+    s.reclaim_steps = c->stats.reclaim_steps;
     s.leaf_bytes = LEAF_SZ; s.branch_bytes = BRANCH_SZ; s.snapshot_bytes = SNAPSHOT_SZ;
     s.slab_overhead = SLAB_OVERHEAD; s.height = (unsigned)t->height;
     pthread_mutex_unlock(&c->pool_mu); return s;
diff --git a/src/piece/piece.h b/src/piece/piece.h
index 739c7cfce6f4d5ffc3eb92bcf863b34ffdb306ef..79f9224d6f1b2a46795fef25004a25e5aeab0709
--- a/src/piece/piece.h
+++ b/src/piece/piece.h
@@ -99,6 +99,14 @@
 piece_tree *piece_create(const piece_allocator *a);
 void piece_destroy(piece_tree *t);
 
+/* Owner-thread maintenance (P1-1 §13). Worker snapshot release transfers
+ * physical ownership to a deferred queue. Perform at most budget header/node
+ * retirement steps and trim at most 64 completed slabs; nonzero means more
+ * work remains. No allocation. Mutations, length/piece-count queries and
+ * snapshot takes also service fixed batches. Destroy/final snapshot release
+ * drain all remaining work. Call only on the tree's owning thread. */
+int piece_reclaim(piece_tree *t, size_t budget);
+
 /* Set initial content; only valid on an empty, never-edited tree.
  * init_copy: tree copies [data, data+len) (caller may free it after return).
  * init_mapped: tree references `mapped` without copying; caller guarantees it
diff --git a/src/piece/piece_test.h b/src/piece/piece_test.h
index f687cfb1ac2945ffdbd72f3f5eae7d5ff9bb4c42..9f93b079ccd3382edbadaf4a0f55c5c13626b54f
--- a/src/piece/piece_test.h
+++ b/src/piece/piece_test.h
@@ -17,7 +17,7 @@
     uint64_t root_descents, cursor_hits, pathcopy_bytes, gap_deletes;
     /* Completed slabs consumed, returned slots, range/iterator node entries,
      * and ADD bytes scanned by prefix queries (including ADD entry splits). */
-    uint64_t slabs_scanned, pool_returns, walk_nodes, ref_recount_bytes;
+    uint64_t slabs_scanned, pool_returns, walk_nodes, ref_recount_bytes, reclaim_steps;
     size_t leaf_bytes, branch_bytes, snapshot_bytes;
     size_t slab_overhead;
     unsigned height;
diff --git a/tests/piece_mem_test.c b/tests/piece_mem_test.c
index 23ae84e71a047930e95e5c93fc82e2cf9dc7a57f..5c04f544a21a4c78f70dd751c44272e80f13ac58
--- a/tests/piece_mem_test.c
+++ b/tests/piece_mem_test.c
@@ -142,6 +142,10 @@
     if (cached) { piece_snapshot *current = piece_snapshot_take(t); CHECK(current); piece_snapshot_release(current); }
     if (worker) {
         pthread_t th; CHECK(!pthread_create(&th, NULL, release_snapshots, snapshots)); CHECK(!pthread_join(th, NULL));
+        /* Worker return acknowledges logical release. Account physical
+         * retirement after bounded owner maintenance, before any query. */
+        unsigned batches = 0;
+        while (piece_reclaim(t, 64)) CHECK(++batches < 1000);
     } else (void)release_snapshots(snapshots);
     double limit = bound(0, 1, 33, 0);
     printf("final release (%s%s): live=%zu bound=%.2f\n", worker ? "worker" : "owner", cached ? ", current cache" : "", atomic_load(&m.live), limit);
diff --git a/tests/piece_mt_test.c b/tests/piece_mt_test.c
index 5aea3496014e3e31c38dca56af632f0fafcc7732..576f92e6a699436454d7b6d4876889b2677118b6
--- a/tests/piece_mt_test.c
+++ b/tests/piece_mt_test.c
@@ -39,6 +39,13 @@
 #endif
 
 #ifdef PIECE_TESTING
+static void *arena_hook_alloc(void *p, size_t n);
+static void arena_hook_free(void *p, void *q, size_t n);
+static uint64_t reclaim_now_ns(void) {
+    struct timespec ts;
+    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
+    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
+}
 typedef struct reclaim_gate {
     pthread_mutex_t mu;
     pthread_cond_t cv;
@@ -71,7 +78,9 @@
     const size_t sizes[] = {20000, 200000};
     for (size_t z = 0; z < sizeof sizes / sizeof sizes[0]; z++) {
         for (unsigned jobs = 1; jobs <= 3; jobs += 2) {
-            piece_allocator a = piece_default_allocator(); piece_tree *t = piece_create(&a); REQUIRE(t);
+            edit_arena arena; REQUIRE(!edit_arena_init(&arena, 128u << 20));
+            piece_allocator a = { &arena, arena_hook_alloc, arena_hook_free };
+            piece_tree *t = piece_create(&a); REQUIRE(t);
             for (size_t i = 0; i < sizes[z]; i++) {
                 uint8_t b = (uint8_t)(i % 251);
                 REQUIRE(!piece_insert(t, 0, &b, 1));
@@ -92,6 +101,8 @@
             pthread_mutex_lock(&g.mu);
             while (g.entered < g.goal && !g.resume) pthread_cond_wait(&g.cv, &g.mu);
             pthread_mutex_unlock(&g.mu);
+            uint64_t began = reclaim_now_ns();
+            edit_malloc_guard_begin();
             int rc = piece_insert(t, 1, (const uint8_t *)"\n", 1);
             rc |= piece_delete(t, 0, 1, NULL);
             piece_snapshot *now = piece_snapshot_take(t); if (!now) rc = 1;
@@ -99,19 +110,114 @@
             rc |= piece_read(t, 0, &b, 1);
             if (b != '\n' || piece_len(t) != 1 || piece_line_count(t) != 2) rc = 1;
             if (now) piece_snapshot_release(now);
+            size_t malloc_calls = edit_malloc_guard_end();
+            uint64_t typing_ns = reclaim_now_ns() - began;
             pthread_mutex_lock(&g.mu); g.typing_done = 1; pthread_cond_broadcast(&g.cv); pthread_mutex_unlock(&g.mu);
             REQUIRE(!pthread_join(watchdog, NULL));
             for (unsigned i = 0; i < jobs; i++) REQUIRE(!pthread_join(workers[i], NULL));
             piece_test_reclaim_hook(t, NULL, NULL);
             printf("reclamation typing: pieces=%zu workers=%u typing_before_worker_resume=%s\n",
                    sizes[z], jobs, g.blocked ? "FAIL" : "ok");
+            printf("reclamation typing latency (M)[AC]: %llu ns (TRACK, loaded box)\n",
+                   (unsigned long long)typing_ns);
+            printf("reclamation typing allocator: malloc_calls=%zu guard=%s\n", malloc_calls,
+                   edit_malloc_guard_active() ? "active" : "sanitizer-inert");
+            REQUIRE(!malloc_calls);
+            unsigned batches = 0;
+            uint64_t reclaimed = 0;
+            int more;
+            do {
+                piece_test_reset_stats(t);
+                more = piece_reclaim(t, 64);
+                piece_test_stats st = piece_test_get_stats(t);
+                REQUIRE(st.reclaim_steps <= 64 && st.slabs_scanned <= 64);
+                reclaimed += st.reclaim_steps;
+                REQUIRE(++batches < sizes[z]);
+            } while (more);
+            REQUIRE(reclaimed > sizes[z] / 16);
+            printf("reclamation maintenance: batches=%u bounded_steps=64 (G)\n", batches);
             piece_destroy(t);
+            edit_arena_free(&arena);
             REQUIRE(!pthread_cond_destroy(&g.cv) && !pthread_mutex_destroy(&g.mu));
             REQUIRE(!g.blocked && !rc);
         }
     }
     puts("reclamation typing: ok (parked retirement; insert/delete/snapshot/read; three queued owners)"); return 0;
 }
+typedef struct reclaim_allocator { atomic_size_t live; } reclaim_allocator;
+static void *reclaim_alloc(void *ctx, size_t n) {
+    reclaim_allocator *a = ctx;
+    void *p = malloc(n);
+    if (p) atomic_fetch_add_explicit(&a->live, n, memory_order_relaxed);
+    return p;
+}
+static void reclaim_free(void *ctx, void *p, size_t n) {
+    reclaim_allocator *a = ctx;
+    atomic_fetch_sub_explicit(&a->live, n, memory_order_relaxed); free(p);
+}
+typedef struct orphan_job {
+    piece_allocator allocator;
+    piece_snapshot *snapshot;
+    atomic_uint visits;
+} orphan_job;
+static void orphan_observe(void *ctx) {
+    orphan_job *j = ctx; atomic_fetch_add_explicit(&j->visits, 1, memory_order_relaxed);
+}
+static void *orphan_create(void *ctx) {
+    orphan_job *j = ctx; piece_tree *t = piece_create(&j->allocator);
+    if (!t) return NULL;
+    if (!piece_insert(t, 0, (const uint8_t *)"x", 1)) {
+        j->snapshot = piece_snapshot_take(t);
+        piece_test_reclaim_hook(t, orphan_observe, j);
+    }
+    piece_destroy(t); return NULL;
+}
+static int reclaim_lifetime_test(void) {
+    for (unsigned jobs = 1; jobs <= 3; jobs += 2) {
+        reclaim_allocator count; atomic_init(&count.live, 0);
+        piece_allocator a = { &count, reclaim_alloc, reclaim_free };
+        piece_tree *t = piece_create(&a); REQUIRE(t);
+        for (unsigned i = 0; i < 20000; i++) REQUIRE(!piece_insert(t, 0, (const uint8_t *)"x", 1));
+        piece_snapshot *s[3];
+        for (unsigned i = 0; i < jobs; i++) {
+            s[i] = piece_snapshot_take(t); REQUIRE(s[i]);
+            REQUIRE(!piece_insert(t, 0, (const uint8_t *)"+", 1));
+        }
+        reclaim_gate g = { .mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .goal = jobs };
+        piece_test_reclaim_hook(t, reclaim_park, &g);
+        pthread_t workers[3], watchdog;
+        REQUIRE(!pthread_create(&watchdog, NULL, reclaim_watchdog, &g));
+        for (unsigned i = 0; i < jobs; i++) REQUIRE(!pthread_create(&workers[i], NULL, reclaim_release, s[i]));
+        pthread_mutex_lock(&g.mu);
+        while (g.entered < g.goal && !g.resume) pthread_cond_wait(&g.cv, &g.mu);
+        pthread_mutex_unlock(&g.mu);
+        /* Producers still own their cores before queue publication. Destroy
+         * must return without waiting, then the final producer frees all. */
+        piece_destroy(t);
+        pthread_mutex_lock(&g.mu); g.typing_done = 1; pthread_cond_broadcast(&g.cv); pthread_mutex_unlock(&g.mu);
+        REQUIRE(!pthread_join(watchdog, NULL));
+        for (unsigned i = 0; i < jobs; i++) REQUIRE(!pthread_join(workers[i], NULL));
+        REQUIRE(!pthread_cond_destroy(&g.cv) && !pthread_mutex_destroy(&g.mu));
+        printf("reclamation lifetime: workers=%u destroy_before_worker_resume=%s live_bytes=%zu\n",
+               jobs, g.blocked ? "FAIL" : "ok", atomic_load_explicit(&count.live, memory_order_relaxed));
+        REQUIRE(!g.blocked && !atomic_load_explicit(&count.live, memory_order_relaxed));
+    }
+    /* The original owning thread may exit before its last snapshot. Do not
+     * compare an expired pthread_t, even if a new worker reuses that ID. */
+    reclaim_allocator count; atomic_init(&count.live, 0);
+    orphan_job j = {.allocator={ &count, reclaim_alloc, reclaim_free }};
+    atomic_init(&j.visits, 0);
+    pthread_t creator, releaser;
+    REQUIRE(!pthread_create(&creator, NULL, orphan_create, &j));
+    REQUIRE(!pthread_join(creator, NULL) && j.snapshot);
+    uint8_t b; REQUIRE(!piece_snapshot_read(j.snapshot, 0, &b, 1) && b == 'x');
+    REQUIRE(!pthread_create(&releaser, NULL, reclaim_release, j.snapshot));
+    REQUIRE(!pthread_join(releaser, NULL));
+    REQUIRE(atomic_load_explicit(&j.visits, memory_order_relaxed) == 1);
+    REQUIRE(!atomic_load_explicit(&count.live, memory_order_relaxed));
+    puts("reclamation lifetime: owner thread exited; final worker release; live_bytes=0");
+    return 0;
+}
 typedef struct {
     pthread_barrier_t *barrier;
     piece_snapshot *snapshot, *retained;
@@ -515,6 +621,7 @@
     (void)argc; (void)argv;
 #ifdef PIECE_TESTING
     if (argc == 2 && !strcmp(argv[1], "--reclaim-typing")) return reclaim_typing_test();
+    if (argc == 2 && !strcmp(argv[1], "--reclaim-lifetime")) return reclaim_lifetime_test();
 #endif
 #if defined(PIECE_TESTING) && defined(PIECE_GAP_TRIAL)
     /* Reproduce the rejected prototype's behavior independently of the
@@ -527,6 +634,7 @@
 #endif
 #ifdef PIECE_TESTING
     bad |= reclaim_typing_test();
+    bad |= reclaim_lifetime_test();
     bad |= owner_race_test(); bad |= cursor_test(); bad |= compact_test();
 #ifdef PIECE_GAP_TRIAL
     bad |= gap_test();

---

## Final message (codex -o)

Implemented deferred worker reclamation with bounded owner cleanup.

`make all`, `make check`, both fuzz campaigns and active zero-malloc tests pass. LeakSanitizer was disabled; coordinator must rerun with leaks enabled. No git writes.

[Worker report with red/green evidence](docs/worker-reports/edit-4w1.59-s8.md). `main` advanced during work; integration remains with the coordinator.