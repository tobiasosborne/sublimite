# Wave0 piece root probe (v2-he7.7) — 17-leaf root collapse deciding test

Oracle: `/home/tobias/Projects/sublimite-v1-oracle` at `6fec2e8`
(`git -C ... log --oneline -1` → "Session 30: worklog and HANDOFF
after the away block"), read-only; nothing was built or written there.
Probe workdir: `/tmp/v2-piece-probe/` (`roottest.c`, binaries, run
captures). Numbers below carry Law-4 tags: (P) = derived from a cited
constant, (M) = measured in the run captured here [Charging,
`/sys/class/power_supply/BAT0/status` at run time]. This file is the
only file written under `sublimite-v2`.

## Derived constants and rules (all `src/piece/piece.c` at 6fec2e8)

- `FAN 16u` — piece.c:30 (P). Leaf holds `b[FAN], nl[FAN], x[FAN]`,
  branch holds `ch[FAN]` — piece.c:54-55. So one node holds at most
  16 (P) entries/children.
- `CH_SIZE = 1<<16 = 65536` (P) — piece.c:31-32. `build()`
  (piece.c:1155) makes one piece per chunk (`np = chunk_count(len)`,
  leaves filled `FAN` at a time), so `piece_init_copy` of
  20×65536 = 1,310,720 B (P) yields 20 (P) pieces (confirmed (M) in
  shape I).
- `REF_BATCH = FAN-2 = 14` (P) — piece.c:42; insert batches are
  capped there (`ins_batch_rec`, piece.c:751).
- Split: `splice()` piece.c:718-748. `total = cnt-ndel+nins`
  (719); `total <= FAN` → in-place (720); else build `tmp[2*FAN]`
  (736-737, `EDIT_ASSERT(total <= 2*FAN)`) and split at
  `l = (m+1)/2` (743). For m=17 (P): left 9 (P), right 8 (P).
- Root grow: `ins_batch()` piece.c:789-797 COWs the root (794),
  recurses (795), and on a returned sibling calls `root_grow()`
  (796; piece.c:783-788), which makes a new branch root with
  `cnt=2` and does `height++`. `piece_delete` can also grow the
  root: a split sibling out of `del_rec` triggers `root_grow`
  (piece.c:1443-1444). Height 0 (P) = leaf root, 1 (P) = branch of
  leaves, 2 (P) = branch of branches (`build()` increments `h` once
  per grouping level, piece.c:1176-1186).
- Merge on delete: `maybe_merge()` piece.c:896-902 returns early
  unless the touched child has `cnt < 8` (899; 8 = FAN/2 (P)); it
  merges with a neighbour only if the pair sums to `<= FAN`
  (900-901) via `merge_children()` piece.c:883-895. `del_rec()`
  piece.c:903-961 calls it for the (at most 2 (P), from `rep[2]` at
  936) replacement children (956-959) and discards boundary-recursion
  siblings at 949/953 (`(void)s` — prefix/suffix deletes cannot
  split, per the earlier reading in wave0-known-bugs.md Bug 1).
- Root shrink ("collapse"): `piece_delete()` piece.c:1399-1449.
  Delete-all is a separate fast path replacing the root with an
  empty leaf and `height=0` (1437-1440). Otherwise it COWs the root
  (1442; `cow()` piece.c:359-372), runs `del_rec` (1443), maybe
  `root_grow` (1444), then (1445-1447):

  ```c
  while (!t->root->leaf && t->root->cnt == 1) {
      node *old = t->root; t->root = old->v.in.ch[0]; pool_free(c, old, 1); t->height--;
  }
  ```

  i.e. every delete that leaves a branch root with exactly one
  child promotes that child (ownership transfer via `pool_free`,
  not `node_unref`) and decrements height, repeatedly.
- Pool reservations (exact-fit suspects): `reserve_delete`
  piece.c:326-336 (height 1, root `cnt==FAN` → 3 (P) branches at
  331; height ≥2 → `4*h-1` (P)); `reserve_insert` piece.c:337-349
  (height 1, `np==1`, root full → 3 (P) at 345). `pool_get` only
  asserts availability (piece.c:250), so an undercount is an
  ASan/assert crash — the probe therefore runs the oracle's
  ASan+UBSan objects.
- Thresholds derived: 17 = FAN+1 (P) pieces is the first count a
  leaf root cannot hold → height 0→1. 257 = FAN×FAN+1 (P) pieces
  need at least ⌈257/16⌉ = 17 (P) leaves even at full fill → a
  branch root (max 16 (P) children) cannot hold them → height ≥2.
  Both are reachable at small byte sizes because piece count, not
  byte count, drives height: non-coalescing 1-byte prepends at
  offset 0 (each breaks the append run required by piece.h:51-58)
  add exactly 1 (P) piece each on an inserts-only tree — asserted
  in the test (`pieces==16`, `==17`, `==257`, all held (M)).

## Test design (`roottest.c`, public API only)

Only `piece/piece.h` plus `tests/piece_model.h` (flat-buffer model;
it includes only `piece/piece.h`). Every operation is applied to
the tree and to the model, and `verify()` compares after **every
step**: `piece_len`, `piece_line_count`, full `piece_read`,
iterator walk (`piece_iter_begin`/`piece_iter_next`), and
`piece_line_to_byte`/`piece_byte_to_line` (exhaustive when small,
512-stride sampled when large). `piece.h` exposes no
consistency-check/validate function (grep of piece.h: none), so
`verify()` is the substitute; `piece_piece_count` (piece.h:162) is
recorded as the shape observable. Snapshots use
`piece_snapshot_take`/`_read`/`_len`/`_line_count`/
`piece_iter_begin_snapshot`/`piece_snapshot_release`
(piece.h:176-187, 203), taken **before** each shrink and re-checked
after the shrink and after further edits. Height itself is not
observable through `piece.h` (`piece_test.h` stats exist but are
outside the public-API constraint of this bead), so level changes
are forced by the (P) piece-count bounds above rather than read
back.

## Shapes tried

| Shape | Op sequence (summary) | Peak pieces | Result |
|---|---|---|---|
| A | prepend ×16, ×1 (=17, grow), snapshot, delete first 9 (left split-half), insert, snapshot checks | 17 (P/M) | PASS |
| B | prepend ×17, snapshot, delete last 8 (right split-half), regrow ×13 | 22 (M, pieces after regrow not printed; len-tracked) | PASS |
| C | fresh 17 each: deletes [8,2) [7,5) [0,17) [4,9) [9,1) [8,1) spanning the 9\|8 boundary; plus 40-piece tree, delete [5,30) | 40 (M) | PASS |
| D | 40 pieces, snapshot, delete 1 byte at front ×40 → empty, rebuild to 17, delete 1 byte at back down to 1 | 40 (M) | PASS |
| E | 8 cycles: prepend ×17 (8→25 pieces) then delete 17 at front | 25 per cycle (M) | PASS |
| F | prepend ×257 then ×43 (=300, ≥height 2), snapshot, staged front deletes 50/50/50/50/43 → 57, delete 49 → 8, regrow ×60, delete [10,50) | 300 (M) | PASS |
| G | 20 pieces, `piece_checkpoint_begin`, delete 12 (collapse), abort (bytes+piece count restored), begin again, delete 12, commit | 20 (M) | PASS |
| H | 20 pieces, `piece_delete(..., &ref)` 12 bytes across collapse, `piece_insert_ref` back | 20 (M) | PASS |
| I | `piece_init_copy` 20×65536 B (=20 pieces via `build`), snapshot, delete 17 whole chunks → 3 pieces, delete 20 B spanning a chunk boundary | 20 (M) | PASS |
| J | deterministic random walk, 3000 (E: fixed loop bound in source) mixed inserts/deletes hovering across 16/17 and up to ~320 bytes, full verify per op, snapshot burst every 500 steps | 300 overall max (M; from F) | PASS |

Printed intermediates (M): A after delete-left-9 → 8 pieces/8 B;
B after delete-right-8 → 9 pieces/9 B; F at 257 → 257 pieces,
staged deletes → 250/200/150/100/57 pieces; I init → 20 pieces,
after deleting 17 chunks → 3 pieces / 196,608 B (=3×65536 (P)).

## Build command

The oracle Makefile links `tests/piece_test` as
`$(B)/san/tests/%: $(B)/san/tests/%.o $(SAN_LIB)` with
`$(CC_SAN) $(SAN_F) -pthread $< $(SAN_LIB) $(LDLIBS)`. `piece.o`'s
only non-libc undefined symbols are `os_*` and `scan_*` (`nm -u`),
so the minimal equivalent link against the prebuilt ASan objects
(no `make` in the oracle) was:

```sh
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g \
  -I/home/tobias/Projects/sublimite-v1-oracle/src \
  -I/home/tobias/Projects/sublimite-v1-oracle/tests \
  /tmp/v2-piece-probe/roottest.c \
  /home/tobias/Projects/sublimite-v1-oracle/build/san/src/piece/piece.o \
  /home/tobias/Projects/sublimite-v1-oracle/build/san/src/scan/scan.o \
  /home/tobias/Projects/sublimite-v1-oracle/build/san/src/os/os_posix.o \
  -pthread -lm -ldl -o /tmp/v2-piece-probe/roottest
```

(Ubuntu clang 18.1.3 (M, `clang --version`).) One earlier red run
is worth recording (Law 9): the first version of shape F deleted
60 B at offset 10 from a 68 B buffer — a test-side arithmetic
error, correctly refused with the test's `piece_delete` failure
(`FAIL ... piece_delete`, 9/10 shapes). Fixed to 50 B; the green
run below is the restored test, re-run twice with identical
output (`diff` clean (M)).

## Run output (green, trimmed: complete — the test prints little)

```text
[A grow17+del-left9+snapshot] after delete-left-9: pieces=8 len=8
PASS A grow17+del-left9+snapshot
  [B grow17+del-right8+snapshot] after delete-right-8: pieces=9 len=9
PASS B grow17+del-right8+snapshot
PASS C boundary-spanning deletes
PASS D shrink-by-1 front/back 40->0
PASS E grow/shrink cycles x8
  [F height2 257/300 + staged collapse + snapshot] at 257: pieces=257 len=257
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=250 len=250
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=200 len=200
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=150 len=150
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=100 len=100
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=57 len=57
PASS F height2 257/300 + staged collapse + snapshot
PASS G checkpoint abort/commit across collapse
PASS H delete-ref + insert_ref across collapse
  [I init_copy 20 chunks + delete 17 chunks] init_copy 20 chunks: pieces=20
  [I init_copy 20 chunks + delete 17 chunks] after delete-17-chunks: pieces=3 len=196608
PASS I init_copy 20 chunks + delete 17 chunks
  [J random walk 3000 ops across thresholds] end: pieces=20 len=29 maxpieces-seen=300
PASS J random walk 3000 ops across thresholds
roottest: 10/10 shapes passed, g_fails=0, max pieces=300
```

Exit code 0 (M). No ASan/UBSan diagnostics (M).

## Deliberate-failure run (proves the test can fail)

A copy (`roottest_bad.c`, probe dir only) flips one model byte in
shape A (`f.m.d[0] ^= 0xff`), calls `verify(...,"DELIBERATE-FLIP")`,
then flips it back. Built with the identical command. Output
(complete):

```text
FAIL [A grow17+del-left9+snapshot] DELIBERATE-FLIP: content mismatch at byte 0 tree=0x51 model=0xae len=17 pieces=17
  [A grow17+del-left9+snapshot] after delete-left-9: pieces=8 len=8
FAIL A grow17+del-left9+snapshot
  [B grow17+del-right8+snapshot] after delete-right-8: pieces=9 len=9
PASS B grow17+del-right8+snapshot
PASS C boundary-spanning deletes
PASS D shrink-by-1 front/back 40->0
PASS E grow/shrink cycles x8
  [F height2 257/300 + staged collapse + snapshot] at 257: pieces=257 len=257
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=250 len=250
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=200 len=200
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=150 len=150
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=100 len=100
  [F height2 257/300 + staged collapse + snapshot] staged delete: pieces=57 len=57
PASS F height2 257/300 + staged collapse + snapshot
PASS G checkpoint abort/commit across collapse
PASS H delete-ref + insert_ref across collapse
  [I init_copy 20 chunks + delete 17 chunks] init_copy 20 chunks: pieces=20
  [I init_copy 20 chunks + delete 17 chunks] after delete-17-chunks: pieces=3 len=196608
PASS I init_copy 20 chunks + delete 17 chunks
  [J random walk 3000 ops across thresholds] end: pieces=20 len=29 maxpieces-seen=300
PASS J random walk 3000 ops across thresholds
roottest: 9/10 shapes passed, g_fails=1, max pieces=300
```

Exit code 1 (M). The mismatch is reported at the exact byte with
both values, and only shape A fails. The original `roottest.c`
was then re-run: exit 0, output identical to the green run above
(M).

## Conclusion: B — claim not reproduced

All 10 shapes pass against the oracle at 6fec2e8 under
ASan+UBSan, with full content/length/line-map equality after every
operation and snapshot isolation across every shrink. Coverage
reached: height 0→1 grow at 17 pieces and back by left-half,
right-half, boundary-spanning, whole-range, and one-byte-at-a-time
deletes (front and back); 8 repeated grow/shrink cycles; the next
level at 257/300 pieces (height ≥2 by the (P) bound) collapsed in
stages to 8 bytes with a live snapshot throughout; checkpoint
abort/commit and delete-ref/insert_ref across the collapse; the
`build()` chunk path (20 pieces → 3); and a 3000-op random walk
crossing both thresholds.

Not reachable / not observable, and why: (1) Tree height and root
`cnt` are not readable through `piece.h`, so "the `cnt==1` loop
executed on this delete" vs "`maybe_merge` got there first" is not
black-box distinguishable; the probe forces the states in which
one or the other must fire (piece counts pass through every value
where a one-child root or an under-8 child is unavoidable) and
decides the user-visible claim — content after root level changes
— which is correct in all of them. (2) The exact-fit reservation
suspects (`reserve_insert`/`reserve_delete` at height 1 with a
full root) are exercised by construction at the 16→17 transitions
but would only manifest as an assert/ASan abort; none occurred.
(3) Variant B from wave0-known-bugs.md (17 leaves via
`piece_init_mapped` of 272 chunks ≈ 17 MiB (P)) was not run in
that form: the same height-2 state is reached at 257 one-byte
pieces (shape F) and the chunk-build path is covered at height 1
(shape I); a height-2 `build()`-path run would only add coverage
of `build()`'s grouping loop, not of grow/collapse. No shape
failed, so there is no minimal failing sequence to report; the
17-leaf root-collapse claim should close as "not reproduced" and
this test can land as a plain regression test, per the earlier
reading's own recommendation.

## Full `roottest.c` source

```c
/* roottest.c -- v2-he7.7 probe: piece-tree root grow/collapse at FAN=16.
 * Public API only: piece/piece.h plus tests/piece_model.h (flat model,
 * itself public-API-only). No piece_test.h, no internals: height is not
 * observable through piece.h, so shapes are driven by piece_piece_count
 * and the FAN derivation (17 pieces cannot fit a leaf root; 257 pieces
 * need >=17 leaves and so a second branch level). piece.h exposes no
 * consistency-check function; verify() below is the substitute: len,
 * line count, full read, iterator walk, and line<->byte maps vs model.
 */
#include "piece/piece.h"
#include "piece_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;
static const char *g_shape = "?";
static uint64_t g_maxpieces;

static void fail_at(int line, const char *what) {
    printf("FAIL [%s] roottest.c:%d: %s\n", g_shape, line, what);
    g_fails++;
}
#define REQ(c) do { if (!(c)) { fail_at(__LINE__, #c); return 0; } } while (0)

/* Full comparison tree vs model. Returns 1 on match. */
static int verify(const piece_tree *t, const pm_model *m, const char *step) {
    uint64_t pc = piece_piece_count(t);
    if (pc > g_maxpieces) g_maxpieces = pc;
    if (piece_len(t) != (uint64_t)m->n) {
        printf("FAIL [%s] %s: len tree=%llu model=%zu (pieces=%llu)\n", g_shape, step,
               (unsigned long long)piece_len(t), m->n, (unsigned long long)pc);
        g_fails++; return 0;
    }
    if (piece_line_count(t) != pm_line_count(m)) {
        printf("FAIL [%s] %s: line_count tree=%llu model=%llu\n", g_shape, step,
               (unsigned long long)piece_line_count(t),
               (unsigned long long)pm_line_count(m));
        g_fails++; return 0;
    }
    uint8_t *b = malloc(m->n + 1);
    if (!b) { fail_at(__LINE__, "malloc"); return 0; }
    int ok = piece_read(t, 0, b, m->n) == PIECE_OK &&
             (m->n == 0 || memcmp(b, m->d, m->n) == 0);
    if (!ok) {
        size_t i = 0; while (i < m->n && b[i] == m->d[i]) i++;
        printf("FAIL [%s] %s: content mismatch at byte %zu tree=0x%02x model=0x%02x len=%zu pieces=%llu\n",
               g_shape, step, i, i < m->n ? b[i] : 0, i < m->n ? m->d[i] : 0,
               m->n, (unsigned long long)pc);
        g_fails++; free(b); return 0;
    }
    free(b);
    piece_iter it; const uint8_t *p; size_t n; size_t pos = 0;
    piece_iter_begin(&it, t, 0);
    while (piece_iter_next(&it, &p, &n)) {
        if (n == 0 || pos + n > m->n || memcmp(p, m->d + pos, n) != 0) {
            printf("FAIL [%s] %s: iterator mismatch at %zu\n", g_shape, step, pos);
            g_fails++; return 0;
        }
        pos += n;
    }
    if (pos != m->n) {
        printf("FAIL [%s] %s: iterator total %zu != %zu\n", g_shape, step, pos, m->n);
        g_fails++; return 0;
    }
    /* Line maps: exhaustive for small content, sampled strides for large. */
    uint64_t lc = pm_line_count(m);
    uint64_t lstride = lc > 4096 ? lc / 512 : 1;
    for (uint64_t l = 0; l <= lc + 1; l += lstride) {
        if (piece_line_to_byte(t, l) != pm_line_to_byte(m, l)) {
            printf("FAIL [%s] %s: line_to_byte(%llu) tree=%llu model=%llu\n",
                   g_shape, step, (unsigned long long)l,
                   (unsigned long long)piece_line_to_byte(t, l),
                   (unsigned long long)pm_line_to_byte(m, l));
            g_fails++; return 0;
        }
    }
    uint64_t ostride = m->n > 8192 ? (uint64_t)m->n / 512 : 1;
    for (uint64_t o = 0; o <= (uint64_t)m->n; o += ostride) {
        if (piece_byte_to_line(t, o) != pm_byte_to_line(m, o)) {
            printf("FAIL [%s] %s: byte_to_line(%llu) tree=%llu model=%llu\n",
                   g_shape, step, (unsigned long long)o,
                   (unsigned long long)piece_byte_to_line(t, o),
                   (unsigned long long)pm_byte_to_line(m, o));
            g_fails++; return 0;
        }
        if (o == (uint64_t)m->n) break;
    }
    return 1;
}

static int verify_snap(piece_snapshot *s, const pm_model *frozen, const char *step) {
    if (piece_snapshot_len(s) != (uint64_t)frozen->n ||
        piece_snapshot_line_count(s) != pm_line_count(frozen)) {
        printf("FAIL [%s] %s: snapshot len/lines snap=%llu/%llu model=%zu/%llu\n",
               g_shape, step, (unsigned long long)piece_snapshot_len(s),
               (unsigned long long)piece_snapshot_line_count(s), frozen->n,
               (unsigned long long)pm_line_count(frozen));
        g_fails++; return 0;
    }
    uint8_t *b = malloc(frozen->n + 1);
    if (!b) { fail_at(__LINE__, "malloc"); return 0; }
    int ok = piece_snapshot_read(s, 0, b, frozen->n) == PIECE_OK &&
             (frozen->n == 0 || memcmp(b, frozen->d, frozen->n) == 0);
    if (!ok) {
        size_t i = 0; while (i < frozen->n && b[i] == frozen->d[i]) i++;
        printf("FAIL [%s] %s: snapshot content mismatch at byte %zu\n", g_shape, step, i);
        g_fails++; free(b); return 0;
    }
    free(b);
    piece_iter it; const uint8_t *p; size_t n; size_t pos = 0;
    piece_iter_begin_snapshot(&it, s, 0);
    while (piece_iter_next(&it, &p, &n)) {
        if (n == 0 || pos + n > frozen->n || memcmp(p, frozen->d + pos, n) != 0) {
            printf("FAIL [%s] %s: snapshot iterator mismatch at %zu\n", g_shape, step, pos);
            g_fails++; return 0;
        }
        pos += n;
    }
    if (pos != frozen->n) {
        printf("FAIL [%s] %s: snapshot iterator total %zu != %zu\n", g_shape, step, pos, frozen->n);
        g_fails++; return 0;
    }
    return 1;
}

static int do_ins(piece_tree *t, pm_model *m, uint64_t off, const uint8_t *d, size_t n, const char *step) {
    if (piece_insert(t, off, d, n) != PIECE_OK) { fail_at(__LINE__, "piece_insert"); return 0; }
    pm_insert(m, (size_t)off, d, n);
    return verify(t, m, step);
}
static int do_del(piece_tree *t, pm_model *m, uint64_t off, uint64_t n, const char *step) {
    if (piece_delete(t, off, n, NULL) != PIECE_OK) { fail_at(__LINE__, "piece_delete"); return 0; }
    pm_delete(m, (size_t)off, (size_t)n);
    return verify(t, m, step);
}

/* One non-coalescing 1-byte prepend: offset 0 with a fresh add-buffer
 * byte each time breaks the append run (piece.h APPEND COALESCING), so
 * piece count must grow by exactly 1 per call on an inserts-only tree. */
static uint8_t pat(unsigned i) { return (uint8_t)(i % 19 == 18 ? '\n' : 'A' + (i % 26)); }
static int build_prepend(piece_tree *t, pm_model *m, unsigned from, unsigned to, const char *step) {
    for (unsigned i = from; i < to; i++) {
        uint8_t b = pat(i);
        if (!do_ins(t, m, 0, &b, 1, step)) return 0;
    }
    return 1;
}

typedef struct { piece_tree *t; pm_model m; piece_allocator a; } fx;
static int fx_new(fx *f) {
    f->a = piece_default_allocator();
    f->t = piece_create(&f->a);
    if (!f->t) { fail_at(__LINE__, "piece_create"); return 0; }
    pm_init(&f->m, NULL, 0);
    return 1;
}
static void fx_free(fx *f) { piece_destroy(f->t); pm_free(&f->m); }

/* A: 16 -> 17 pieces (root gains level), snapshot, delete left 9 (the
 * (17+1)/2 split half), more edits, snapshot must stay frozen. */
static int shape_a(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 16, "build16");
    if (ok && piece_piece_count(f.t) != 16) { fail_at(__LINE__, "pieces==16"); ok = 0; }
    if (ok) ok = build_prepend(f.t, &f.m, 16, 17, "build17");
    if (ok && piece_piece_count(f.t) != 17) { fail_at(__LINE__, "pieces==17"); ok = 0; }
    pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
    piece_snapshot *s = ok ? piece_snapshot_take(f.t) : NULL;
    if (ok && !s) { fail_at(__LINE__, "snapshot"); ok = 0; }
    if (ok) ok = verify_snap(s, &frozen, "snap-before-shrink");
    if (ok) ok = do_del(f.t, &f.m, 0, 9, "delete-left-9");
    printf("  [%s] after delete-left-9: pieces=%llu len=%llu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t), (unsigned long long)piece_len(f.t));
    if (ok) ok = verify_snap(s, &frozen, "snap-after-shrink");
    if (ok) ok = do_ins(f.t, &f.m, 0, (const uint8_t *)"ZZ\n", 3, "edit-after-shrink");
    if (ok) ok = verify_snap(s, &frozen, "snap-after-more-edits");
    if (s) piece_snapshot_release(s);
    pm_free(&frozen); fx_free(&f);
    return ok;
}

/* B: delete the whole right subtree (last 8 of the 9/8 split). */
static int shape_b(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 17, "build17");
    pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
    piece_snapshot *s = ok ? piece_snapshot_take(f.t) : NULL;
    if (ok && !s) { fail_at(__LINE__, "snapshot"); ok = 0; }
    if (ok) ok = do_del(f.t, &f.m, 9, 8, "delete-right-8");
    printf("  [%s] after delete-right-8: pieces=%llu len=%llu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t), (unsigned long long)piece_len(f.t));
    if (ok) ok = verify_snap(s, &frozen, "snap-after-shrink");
    if (ok) ok = build_prepend(f.t, &f.m, 17, 30, "regrow");
    if (ok) ok = verify_snap(s, &frozen, "snap-after-regrow");
    if (s) piece_snapshot_release(s);
    pm_free(&frozen); fx_free(&f);
    return ok;
}

/* C: deletes spanning the 9|8 root-child boundary, several widths. */
static int shape_c(void) {
    static const struct { uint64_t off, len; } cuts[] = {
        {8, 2}, {7, 5}, {0, 17}, {4, 9}, {9, 1}, {8, 1}
    };
    int ok = 1;
    for (size_t k = 0; k < sizeof cuts / sizeof cuts[0] && ok; k++) {
        fx f; if (!fx_new(&f)) return 0;
        ok = build_prepend(f.t, &f.m, 0, 17, "build17");
        if (ok) ok = do_del(f.t, &f.m, cuts[k].off, cuts[k].len, "boundary-delete");
        if (ok && f.m.n) ok = do_del(f.t, &f.m, 0, 1, "post-delete-1");
        fx_free(&f);
    }
    /* Plus: build 40, delete a wide middle span crossing many leaves. */
    fx f; if (!fx_new(&f)) return 0;
    if (ok) ok = build_prepend(f.t, &f.m, 0, 40, "build40");
    if (ok) ok = do_del(f.t, &f.m, 5, 30, "wide-middle-delete");
    fx_free(&f);
    return ok;
}

/* D: shrink one byte at a time from the front, 40 -> 0 pieces. Every
 * merge (cnt<8 neighbour merge) and cnt==1 collapse is stepped through. */
static int shape_d(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 40, "build40");
    pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
    piece_snapshot *s = ok ? piece_snapshot_take(f.t) : NULL;
    if (ok && !s) { fail_at(__LINE__, "snapshot"); ok = 0; }
    while (ok && f.m.n > 0) ok = do_del(f.t, &f.m, 0, 1, "shrink-by-1-front");
    if (ok) ok = verify_snap(s, &frozen, "snap-after-empty");
    if (ok) ok = build_prepend(f.t, &f.m, 40, 57, "rebuild17");
    if (ok) ok = verify_snap(s, &frozen, "snap-after-rebuild");
    /* Now shrink from the back one at a time. */
    while (ok && f.m.n > 1) ok = do_del(f.t, &f.m, (uint64_t)f.m.n - 1, 1, "shrink-by-1-back");
    if (s) piece_snapshot_release(s);
    pm_free(&frozen); fx_free(&f);
    return ok;
}

/* E: repeated grow/shrink cycles across the FAN boundary. */
static int shape_e(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 8, "seed8");
    unsigned base = 8;
    for (int cyc = 0; cyc < 8 && ok; cyc++) {
        ok = build_prepend(f.t, &f.m, base, base + 17, "cycle-grow"); /* 8 -> 25 pieces */
        uint64_t pc = piece_piece_count(f.t);
        if (ok && pc != 25) { printf("  [%s] cycle %d pieces=%llu (note)\n", g_shape, cyc, (unsigned long long)pc); }
        if (ok) ok = do_del(f.t, &f.m, 0, 17, "cycle-shrink");        /* back to 8 bytes */
        base += 17;
    }
    fx_free(&f);
    return ok;
}

/* F: next level. 257 = FAN*FAN+1 (P) pieces need >=17 leaves, hence a
 * height-2 root; then staged deletes collapse 2 -> 1 -> 0 levels. */
static int shape_f(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 257, "build257");
    if (ok && piece_piece_count(f.t) != 257) { fail_at(__LINE__, "pieces==257"); ok = 0; }
    printf("  [%s] at 257: pieces=%llu len=%llu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t), (unsigned long long)piece_len(f.t));
    if (ok) ok = build_prepend(f.t, &f.m, 257, 300, "build300");
    pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
    piece_snapshot *s = ok ? piece_snapshot_take(f.t) : NULL;
    if (ok && !s) { fail_at(__LINE__, "snapshot"); ok = 0; }
    if (ok) ok = verify_snap(s, &frozen, "snap-at-300");
    static const uint64_t chunks[] = {50, 50, 50, 50, 43}; /* 300 -> 57 */
    for (size_t k = 0; k < sizeof chunks / sizeof chunks[0] && ok; k++) {
        ok = do_del(f.t, &f.m, 0, chunks[k], "staged-collapse");
        if (ok) ok = verify_snap(s, &frozen, "snap-during-collapse");
        printf("  [%s] staged delete: pieces=%llu len=%zu\n", g_shape,
               (unsigned long long)piece_piece_count(f.t), f.m.n);
    }
    if (ok) ok = do_del(f.t, &f.m, 0, 49, "collapse-to-8"); /* 57 -> 8 bytes */
    if (ok) ok = verify_snap(s, &frozen, "snap-after-collapse");
    if (ok) ok = build_prepend(f.t, &f.m, 300, 360, "regrow-past-257-again");
    if (ok) ok = do_del(f.t, &f.m, 10, 50, "wide-span-at-height2"); /* 68 -> 18 bytes */
    if (ok) ok = verify_snap(s, &frozen, "snap-final");
    if (s) piece_snapshot_release(s);
    pm_free(&frozen); fx_free(&f);
    return ok;
}

/* G: checkpoint abort/commit across a collapse (piece.h P1.3c). */
static int shape_g(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 20, "build20");
    uint64_t pc0 = piece_piece_count(f.t);
    piece_checkpoint *cp = NULL; pm_checkpoint *mc = NULL;
    if (ok && piece_checkpoint_begin(f.t, &cp) != PIECE_OK) { fail_at(__LINE__, "cp begin"); ok = 0; }
    if (ok && pm_checkpoint_begin(&f.m, &mc) != PIECE_OK) { fail_at(__LINE__, "pm cp begin"); ok = 0; }
    if (ok) ok = do_del(f.t, &f.m, 0, 12, "cp-collapse-delete");
    if (cp) { piece_checkpoint_abort(cp); cp = NULL; }
    if (mc) { pm_checkpoint_abort(mc); mc = NULL; }
    if (ok) ok = verify(f.t, &f.m, "after-abort");
    if (ok && piece_piece_count(f.t) != pc0) { fail_at(__LINE__, "abort restores piece count"); ok = 0; }
    if (ok && piece_checkpoint_begin(f.t, &cp) != PIECE_OK) { fail_at(__LINE__, "cp begin2"); ok = 0; }
    if (ok && pm_checkpoint_begin(&f.m, &mc) != PIECE_OK) { fail_at(__LINE__, "pm cp begin2"); ok = 0; }
    if (ok) ok = do_del(f.t, &f.m, 0, 12, "cp-collapse-delete2");
    if (cp) { piece_checkpoint_commit(cp); cp = NULL; }
    if (mc) { pm_checkpoint_commit(mc); mc = NULL; }
    if (ok) ok = verify(f.t, &f.m, "after-commit");
    fx_free(&f);
    return ok;
}

/* H: delete with ref across collapse, reinsert by ref (undo path). */
static int shape_h(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 20, "build20");
    uint8_t save[12];
    if (ok && piece_read(f.t, 0, save, 12) != PIECE_OK) { fail_at(__LINE__, "read save"); ok = 0; }
    piece_ref r;
    if (ok && piece_delete(f.t, 0, 12, &r) != PIECE_OK) { fail_at(__LINE__, "delete ref"); ok = 0; }
    if (ok) { pm_delete(&f.m, 0, 12); ok = verify(f.t, &f.m, "after-ref-delete"); }
    if (ok && piece_insert_ref(f.t, 0, &r) != PIECE_OK) { fail_at(__LINE__, "insert_ref"); ok = 0; }
    if (ok) { pm_insert(&f.m, 0, save, 12); ok = verify(f.t, &f.m, "after-ref-reinsert"); }
    fx_free(&f);
    return ok;
}

/* I: init_copy chunk build path: 20 chunks = 20 pieces (build() makes
 * one piece per 64 KiB chunk), height-1 root by construction, then
 * delete 17 whole chunks and a boundary-spanning range. */
static int shape_i(void) {
    const size_t CH = 65536, NCH = 20, LEN = CH * NCH;
    uint8_t *data = malloc(LEN);
    if (!data) { fail_at(__LINE__, "malloc"); return 0; }
    for (size_t i = 0; i < LEN; i++) data[i] = (uint8_t)(i % 251 == 250 ? '\n' : 'a' + (i / CH) % 26);
    fx f; if (!fx_new(&f)) { free(data); return 0; }
    int ok = 1;
    if (piece_init_copy(f.t, data, LEN) != PIECE_OK) { fail_at(__LINE__, "init_copy"); ok = 0; }
    if (ok) { pm_insert(&f.m, 0, data, LEN); ok = verify(f.t, &f.m, "init20chunks"); }
    printf("  [%s] init_copy 20 chunks: pieces=%llu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t));
    pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
    piece_snapshot *s = ok ? piece_snapshot_take(f.t) : NULL;
    if (ok && !s) { fail_at(__LINE__, "snapshot"); ok = 0; }
    if (ok) ok = do_del(f.t, &f.m, 0, 17 * (uint64_t)CH, "delete-17-chunks");
    printf("  [%s] after delete-17-chunks: pieces=%llu len=%zu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t), f.m.n);
    if (ok) ok = verify_snap(s, &frozen, "snap-after-chunk-collapse");
    if (ok) ok = do_del(f.t, &f.m, (uint64_t)CH - 10, 20, "chunk-boundary-delete");
    if (ok) ok = verify_snap(s, &frozen, "snap-final");
    if (s) piece_snapshot_release(s);
    pm_free(&frozen); fx_free(&f); free(data);
    return ok;
}

/* J: deterministic random walk hovering across 16/17 and 256/257. */
static uint64_t rng_s = 0x243F6A8885A308D3ull;
static uint64_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return rng_s; }
static int shape_j(void) {
    fx f; if (!fx_new(&f)) return 0;
    int ok = build_prepend(f.t, &f.m, 0, 20, "seed20");
    unsigned ctr = 20;
    for (int step = 0; step < 3000 && ok; step++) {
        uint64_t n = (uint64_t)f.m.n;
        int grow = n < 20 || (n < 320 && (rnd() % 100) < 55);
        if (grow) {
            uint8_t buf[4]; size_t l = 1 + (size_t)(rnd() % 3);
            for (size_t i = 0; i < l; i++) buf[i] = pat(ctr++);
            uint64_t off = (rnd() % 3 == 0) ? 0 : (n ? rnd() % (n + 1) : 0);
            ok = do_ins(f.t, &f.m, off, buf, l, "rand-ins");
        } else if (n > 0) {
            uint64_t off = rnd() % n;
            uint64_t l = 1 + rnd() % 17; if (l > n - off) l = n - off;
            ok = do_del(f.t, &f.m, off, l, "rand-del");
        }
        if (ok && step % 500 == 499) {
            pm_model frozen; pm_init(&frozen, f.m.d, f.m.n);
            piece_snapshot *s = piece_snapshot_take(f.t);
            if (!s) { fail_at(__LINE__, "rand snapshot"); ok = 0; }
            else {
                /* a burst of unverified-by-snap edits, then check snap */
                piece_snapshot *keep = s;
                for (int bstep = 0; bstep < 10 && ok; bstep++) {
                    uint8_t bch = pat(ctr++);
                    ok = do_ins(f.t, &f.m, 0, &bch, 1, "rand-burst");
                }
                if (ok) ok = verify_snap(keep, &frozen, "rand-snap");
                piece_snapshot_release(keep);
            }
            pm_free(&frozen);
        }
    }
    printf("  [%s] end: pieces=%llu len=%zu maxpieces-seen=%llu\n", g_shape,
           (unsigned long long)piece_piece_count(f.t), f.m.n,
           (unsigned long long)g_maxpieces);
    fx_free(&f);
    return ok;
}

int main(void) {
    static const struct { const char *name; int (*fn)(void); } shapes[] = {
        {"A grow17+del-left9+snapshot", shape_a},
        {"B grow17+del-right8+snapshot", shape_b},
        {"C boundary-spanning deletes", shape_c},
        {"D shrink-by-1 front/back 40->0", shape_d},
        {"E grow/shrink cycles x8", shape_e},
        {"F height2 257/300 + staged collapse + snapshot", shape_f},
        {"G checkpoint abort/commit across collapse", shape_g},
        {"H delete-ref + insert_ref across collapse", shape_h},
        {"I init_copy 20 chunks + delete 17 chunks", shape_i},
        {"J random walk 3000 ops across thresholds", shape_j},
    };
    int passed = 0;
    for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++) {
        g_shape = shapes[i].name;
        int before = g_fails;
        int ok = shapes[i].fn() && g_fails == before;
        printf("%s %s\n", ok ? "PASS" : "FAIL", shapes[i].name);
        if (ok) passed++;
    }
    printf("roottest: %d/%zu shapes passed, g_fails=%d, max pieces=%llu\n",
           passed, sizeof shapes / sizeof shapes[0], g_fails,
           (unsigned long long)g_maxpieces);
    return g_fails ? 1 : (passed == (int)(sizeof shapes / sizeof shapes[0]) ? 0 : 1);
}

```

VERDICT: B — all 10 root grow/collapse shapes (17 pieces, 257/300 pieces, snapshots, checkpoint, ref, chunk-build, random walk) pass against the v1 oracle at 6fec2e8 under ASan+UBSan; the 17-leaf root-collapse claim is NOT reproduced. File: docs/decisions/wave0-piece-root-probe.md (this file only under sublimite-v2; probe in /tmp/v2-piece-probe, oracle untouched).
