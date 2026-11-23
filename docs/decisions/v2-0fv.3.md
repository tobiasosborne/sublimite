# v2-0fv.3 · L0 base harvest

Source pin `6fec2e8`. The release archive supplies the Linux arena, pool,
CPU feature queries and Law-2 allocation hook. The first Law-2 suite
checks allocation through the real GCC `-O2` interposers and covers the
OS probes handed off by [v2-0fv.1](v2-0fv.1.md).

## Decisions and invariants

The arena uses the v1 bump and rewind model. Padding comes from the
absolute cursor address. With offset-only rounding, a mapping whose
origin is one page past a two-page boundary can give a misaligned
two-page allocation. Linux supplies writable, lazily backed
reservations through `os_vm_reserve`. Init rejects unrepresentable
extents and free derives the same page-rounded length.

The pool keeps the v1 intrusive LIFO free list. A free block can store
a pointer even when the requested payload is a byte. Integer addresses
permit range checks on pointers from unrelated objects. The caller must
return a currently checked-out block exactly once. Detecting duplicate
returns while other blocks are live would require a scan or dedicated
per-block metadata. The API keeps the fixed-time ownership contract.

| State | Invariant |
|---|---|
| Arena `base` | Null in an empty handle. Otherwise the start of the one page-aligned OS reservation owned by the handle. |
| Arena `size` | Exact byte capacity requested at init. The reservation extent is this capacity rounded up to the OS page size. |
| Arena `used` | First unclaimed byte offset, including alignment padding, with `used <= size`. Failed allocations leave it unchanged. |
| `base_arena_mark_t` value | A copy of an arena's cursor at mark time. Reset accepts a mark at or behind the current cursor. |
| Pool `base` | Null in an empty handle. Otherwise the start of the one page-aligned OS reservation owned by the handle. |
| Pool `stride` | Block width including link storage and padding. It holds a pointer and is a multiple of both the requested alignment and the link alignment. |
| Pool `capacity` | Total number of slots. The product with `stride` and its page-rounded extent are representable. |
| Pool `fresh` | Number of slots ever issued by the bump path. Issued slots form a contiguous prefix of length at most `capacity`. |
| Pool `live` | Number of issued slots currently checked out. Equal to `fresh` minus the free-list length and available in constant time. |
| Pool `free_head` and links | Null or a chain of every returned slot in reverse return order. The chain contains each returned slot once and shares no slots with live blocks. |

The CPU accessors share one `os_once`. The callback invokes the
compiler's initializer. Both queries then read the compiler runtime's
feature bits. This uses one publication mechanism and one source of
feature values. Concurrent first callers also share the same
initialization across AVX2 and POPCNT.

The guard counts `malloc`, `calloc` and `realloc` calls using glibc's
`__libc_*` entry points underneath. The release editor link selects the
hook even if its only allocation calls come from shared libraries.
The symbol checker permits the three libc ABI definitions only in
`src/base/malloc_guard.o`. The build selects interposition in that
release object. Sanitizer builds keep the guard API inert so their
runtime owns the allocator. The release lane requires the active flag,
an observed deliberate allocation, and a zero count for each Law-2
window. The exemption probe exercises both nested depth and simultaneous
allocation by another thread.

| Module global | Invariant and authorization |
|---|---|
| `cpu_once` | `os_once` publishes completion of the feature initializer to all callers. PLAN §2 names one-time CPU detection for base and §3 calls out the fix. The published bits reside in the compiler runtime. |
| `guard_count` | `SIZE_MAX` denotes a closed window. An open window holds the number of non-exempt allocator entries up to saturation at `SIZE_MAX - 1`. A successful increment and the closing exchange share one atomic modification order. The owner serializes begin/end and an overlapping allocation counts at its atomic increment, or observes the closed state. PLAN §2 names the counting guard, §5 requires it live at release, and CLAUDE.md authorizes its allocator hook. |
| `guard_exempt_depth` | Thread-local count of the caller's outstanding exemption brackets. Only balanced calls on that thread modify it. It belongs to the same PLAN §2 and §5 allocator hook. |

## v1 material retired or replaced

### `src/base/base.h`

- `EDIT_*` and `edit_*` spellings — the v2 public surface uses `BASE_*` and `base_*`, with the build identity `EDIT_APP_ID` owned separately.
- Windows arena commit and failure documentation — the Linux VM contract gives writable pages at reservation.
- Offset-only alignment wording and the pool's literal 4096-byte cap — arena alignment uses the address and pool alignment is bounded by the native page size.
- `edit_pool.map_bytes` — stride and capacity already determine the release extent.
- The claim that pool validation could establish ownership from the address alone — rejection covers foreign, interior and unissued addresses and the caller owns the live-slot precondition.
- Guard instructions to skip counting when inert — the release lane requires live interposition and a counted positive control.

### `src/base/base.c`

- The `"0.0.1-dev"` v1 version literal — the retained version API returns `"0.0.1-v2-dev"`.
- Bytewise struct clearing and the resulting `<string.h>` import — typed zero initialization clears the handles and allocates no storage.
- Separate arena and pool page-rounding and reserve sequences — one checked helper implements the shared OS reservation contract.
- Arena offset-only rounding and wrap checks — unsigned absolute-address padding is bounded by remaining capacity before any cursor advance.
- The arena's Windows per-allocation `os_vm_commit` branch — pages are writable on return from reserve.
- The pool get Windows `os_vm_commit` branch — a fresh block in the Linux reservation is available on first touch.
- The redundant `map_bytes` assignment and load — pool release derives the page-rounded extent.
- Relational pointer comparisons and pointer subtraction across possibly unrelated objects — integer address range validation has defined behavior for rejected pointers.
- The free-list hit prediction in pool get — the relative rates of first issuance and reuse depend on the caller.
- Both function-local CPU integer caches, sentinel branches and separate initializer calls — one once control publishes the runtime's feature values.

### `src/base/malloc_guard.c`

- The Windows translation-unit condition and twin-file reference — this build uses the Linux hook.
- The `__has_feature` compatibility macro and compiler-specific sanitizer matrix — the Makefile selects the release object explicitly.
- The non-glibc inert branch — the release hook targets the supported glibc ABI. Sanitizer builds still expose inert API functions.
- Independent `guard_on` and `guard_count` state and the check-then-increment sequence — one atomic encodes window state and count, so end and increments cannot split across separate atomics.
- GNU `__atomic_*` calls — C11 atomics express the same relaxed ordering and the single-word close operation.
- Silent exemption-end underflow handling — exemption pairs are balanced by the caller.
- v1 migration references and the indirect counts-only-when-active test convention — the public header names the exemption contract and the release suite enforces guard liveness.

### `tests/base_test.c`

- Separate non-null and length checks and the constant branch-hint assertion — one check verifies the version string and compiler hinting needs no value test.
- Hard-coded arena sizes and one-page alignment examples — the revised fixture uses native pages, an odd capacity, overflow requests and alignment above a native page.
- The large sequential pool churn loop — targeted get/put windows prove Law 2 and the model-checked fuzzer explores operation sequences.
- The per-test `SKIP` branches and sanitizer messages — the release suite proves the guard explicitly and the sanitizer/TSan suite exercises the functional API.
- The v1 exemption helper and two independent worker probes — the release suite holds a nested exemption on the main thread while the worker allocates, then checks all three hooks after the brackets close.
- The sequential CPU self-comparison — two first callers released from one barrier compare both feature values with each other and with a later caller.

The handed-off OS window covers first use of initialized mutexes,
recursive locks, conditions and once controls, competing lock users,
a condition waiter, clocks, thread identity, yield, wake signal/drain
and positioned I/O. Thread creation and fixture setup are outside
the window. The test uses an unlinked fixture under `build/`. The
positive control is common to the release suite and replaces the v1
OS test's sanitizer-specific guard detection.

## Files

- `src/base/base.h`, `src/base/base.c`, `src/base/malloc_guard.c` implement the public API and hook.
- `tests/base_test.c`, `tests/law2_base_test.c`, `tests/lanes.mk`, `tests/lanes/run.py` add functional, threaded and release coverage and exercise an inert guard even when it claims to be active.
- `fuzz/base_fuzz.c`, `fuzz/corpus/base/arena`, `fuzz/corpus/base/pool` check a byte-driven allocator model against public operations.
- `bench/base_bench.c`, `Makefile`, `tools/check_symbols.py`, `tools/system-symbols.txt` build the bench and lanes and recognize the allocator ABI and compiler runtime imports.
- `CLAUDE.md`, `docs/PLAN_V2.md`, `docs/decisions/v2-0fv.3.md` record the release guard name and decisions.

Source count (M, `wc -l src/base/*`).

```text
 132 src/base/base.c
  83 src/base/base.h
  65 src/base/malloc_guard.c
 280 total
```

## Red and green

### CPU first-call race

The first revision of `tests/base_test.c` ran only `test_cpu()`
and printed `base_test: PASS`. The harness was written before the
initializer changed. The renamed v1 body preserved both racy caches.

```sh
mkdir -p build/base-red
sed 's/edit_/base_/g; s/EDIT_/BASE_/g' /home/tobias/Projects/sublimite-v1-oracle/src/base/base.c > build/base-red/base_v1.c
make -s build/tsan/src/os/os_posix.o build/rel/src/os/os_posix.o
clang -std=c11 -D_GNU_SOURCE -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread -Isrc tests/base_test.c build/base-red/base_v1.c build/tsan/src/os/os_posix.o -o build/base-red/cpu
build/base-red/cpu > build/base-red/cpu.log 2>&1
```

That run used the mechanically renamed v1 header at `src/base/base.h`.
To reconstruct the old layout now, write the renamed v1 header to
`build/base-red/include/base/base.h` and put
`-Ibuild/base-red/include` ahead of `-Isrc`. The CPU-only harness
is the current test through `test_cpu()`, with
`int main(void) { test_cpu(); puts("base_test: PASS"); return 0; }`.

Red exited 66. The complete diagnostic is in `build/base-evidence/cpu-red.log`.

```text
==================
WARNING: ThreadSanitizer: data race (pid=154475)
  Read of size 4 at 0x55555567ac48 by thread T1:
    #0 base_cpu_has_avx2 /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/build/base-red/base_v1.c:128:9 (cpu+0xe07ea) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #1 probe_cpu /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:17:19 (cpu+0xe013e) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #2 thread_main /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/src/os/os_posix.c:128:5 (cpu+0xe0cbf) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)

  Previous write of size 4 at 0x55555567ac48 by thread T2:
    #0 base_cpu_has_avx2 /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/build/base-red/base_v1.c:130:16 (cpu+0xe081c) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #1 probe_cpu /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:17:19 (cpu+0xe013e) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #2 thread_main /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/src/os/os_posix.c:128:5 (cpu+0xe0cbf) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)

  Location is global 'base_cpu_has_avx2.cached' of size 4 at 0x55555567ac48 (cpu+0x126c48)

  Thread T1 (tid=154477, running) created by main thread at:
    #0 pthread_create <null> (cpu+0x6186f) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #1 os_thread_start /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/src/os/os_posix.c:136:14 (cpu+0xe0c36) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #2 test_cpu /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:28:5 (cpu+0xdfeca) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #3 main /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:41:5 (cpu+0xdfeca)

  Thread T2 (tid=154478, finished) created by main thread at:
    #0 pthread_create <null> (cpu+0x6186f) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #1 os_thread_start /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/src/os/os_posix.c:136:14 (cpu+0xe0c36) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #2 test_cpu /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:29:5 (cpu+0xdfee6) (BuildId: 9f4148a5a614a37fc9241b24d0fc2430be89671d)
    #3 main /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/tests/base_test.c:41:5 (cpu+0xdfee6)

SUMMARY: ThreadSanitizer: data race /home/tobias/.t3/worktrees/sublimite-v2/t3-5a0274c3-5acf26c19d5d0094/build/base-red/base_v1.c:128:9 in base_cpu_has_avx2
==================
ThreadSanitizer: reported 1 warnings
cpu: avx2=1 popcnt=1
base_test: PASS
```

Green used the final archive and full test through `make check-tsan`.
Exit 0, with no TSan diagnostics. The base portion of the output follows.

```text
== build/tsan/tests/base_test
cpu: avx2=1 popcnt=1
0.0.1-v2-dev
arena: ok
pool: ok
base_test: PASS
check-tsan: 2 threaded suites passed
```

### Guard liveness and allocation-free window

The guard's initial negative control used build-local inert
implementations of the public guard API. The fixture returns false
from `base_malloc_guard_active`, returns zero from
`base_malloc_guard_end`, and makes begin and both exemption calls
no-ops. The later lane fixtures also cover false liveness claims.

```sh
gcc -std=c11 -D_GNU_SOURCE -O2 -g -pthread -Isrc tests/law2_base_test.c src/base/base.c build/base-red/inert.c build/rel/src/os/os_posix.o -o build/base-red/law2-inert &&
    build/base-red/law2-inert > build/base-evidence/guard-red.log 2>&1
```

Red for guard liveness, exit 1.

```text
law2_base_test: FAIL (requires a live counting guard and a clean window)
guard positive control: active=0 count=0
arena/pool Law-2 window: active=0 count=0
```

The window's negative control kept the real release interposer and
added this link-time wrapper in `build/base-red/allocation-in-window.c`.

```c
#include "base/base.h"

void *__real_base_pool_get(base_pool *);
void *__wrap_base_pool_get(base_pool *pool)
{
    void *(*volatile allocate)(size_t) = malloc;
    free(allocate(1));
    return __real_base_pool_get(pool);
}
```

```sh
gcc -std=c11 -D_GNU_SOURCE -DBASE_MALLOC_GUARD_RELEASE -O2 -g -pthread -Isrc tests/law2_base_test.c src/base/base.c src/base/malloc_guard.c build/base-red/allocation-in-window.c build/rel/src/os/os_posix.o -Wl,--wrap=base_pool_get -o build/base-red/law2-allocating &&
    build/base-red/law2-allocating > build/base-evidence/window-red.log 2>&1
```

Red for the zero-allocation window, exit 1. The four calls match the
four pool gets, including the exhaustion probe.

```text
law2_base_test: FAIL (requires a live counting guard and a clean window)
guard positive control: active=1 count=1
arena/pool Law-2 window: active=1 count=4
```

Green for both, exit 0.

```sh
make -s check-law2 > build/base-evidence/law2-green.log 2>&1
```

```text
check-suites: ok
== build/rel/tests/law2_base_test
guard positive control: active=1 count=1
arena/pool Law-2 window: active=1 count=0
guard exemptions: active=1 worker_exempt=0 worker_counted=1 after_close=3
OS Law-2 window: active=1 count=0
law2_base_test: PASS
check-law2: 1 release suites passed
```

## Acceptance and measurement

These commands exited 0. The first fuzz smoke run also passed with
the harvested implementation before the CPU initializer changed.

```sh
make all > build/base-evidence/all.log 2>&1
make check > build/base-evidence/check.log 2>&1
make check-tsan > build/base-evidence/check-tsan.log 2>&1
make fuzz-smoke > build/base-evidence/fuzz-smoke.log 2>&1
```

`make check` ran the DAG, include, symbol, isolated-layer,
ASan/UBSan and release Law-2 checks. Both release guard mutations in
the lane fixture were rejected. `make fuzz-smoke` replayed the two
committed seeds. `nm -D --defined-only build/sublimite-v2` confirmed
the shipped editor defines `malloc`, `calloc` and `realloc`.

```sh
make build/rel/bench/base_bench > build/base-evidence/bench-build.log 2>&1
{
    cat /sys/class/power_supply/BAT0/status
    LC_ALL=C uptime
    build/rel/bench/base_bench
} > build/base-evidence/bench.log 2>&1
```

(M) [AC], BAT0 charging. Each sample brackets one pool get/put
pair with the monotonic clock. The bench uses the v1 sample count
and reports the nearest-rank percentiles of the sorted observations,
including clock read cost. Exit 0. The measurements carry no threshold.

```text
Charging
 12:07:38 up 13:22,  1 user,  load average: 7.77, 5.77, 5.84
BENCH name=base_pool n=1000000 p50=68 p99=77 unit=ns
```
