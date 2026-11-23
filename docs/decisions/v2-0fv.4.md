# v2-0fv.4 — trace rings, dumps and replay

The trace module retains the native little-endian EDTRACE1 layout from v1 at
`6fec2e8`. Timing IDs and ingress kinds keep their numeric values and are
append-only. The v1 timing-only form and the form with the INPT section both
load. `trace/trace.h` is the single public header. `trace_format.h` contains
private wire layouts and compile-time size checks shared by the writer and
loader. Pairing and percentile calculations live out of line for G1 analysis.

## Ownership and dump semantics

A thread claims the first available slot with an acquire compare-exchange on
the ownership bitmap and caches its address in TLS. The bitmap gives the
full-pool result from a single snapshot. A scan of independent ownership
flags can miss a slot released earlier in the scan. The thread calls
`trace_thread_release()` on the same thread before returning. Release clears
TLS and clears the bit with a release read-modify-write. All bitmap mutations
are read-modify-writes, so even an intervening claim or release of another
slot preserves the release sequence observed by the next owner's acquire.
A process-lifetime thread may keep its slot. The full-pool result is `-1`
and callers that require coverage must check it. The pool worker call site belongs to L0-5.

A dump or reset runs with external synchronization, such as after worker
joins on the UI thread. Live parked producers require synchronization in both
directions before access and before resumption. A dump emits nonempty slots
in slot-index order, including released slots. It emits slot zero when there
are startup stamps even if slot zero has no ordinary records. Each successive
owner appends to the same bounded history. The wire `thread` field identifies
the physical slot, so several lifetimes can share that field. This preserves
all records in a post-join dump until ordinary ring wrap. Clearing a cursor
on claim would lose the previous owner's record on every sequential reuse.

| State | Invariant |
| --- | --- |
| `g_claimed` | One bit per physical slot. A set bit grants one writer. Only that writer clears the bit, and all out-of-range bits remain clear. |
| `tl_ring` | Null or the unique slot owned by this thread. The pointer also determines the registration index. |
| `g_rings[i].total` | Append cursor since reset across all owners of the slot. The owner or a synchronized reset changes it. |
| `g_rings[i].recs` | Circular suffix of slot appends. Each position stores the timestamp, event and frame. Serialization derives the thread field from the slot index. |
| `g_startup` | One atomic timestamp per v1 startup stage. Zero means absent and the first nonzero stamp survives until reset. |
| `g_in_total` | UI-owned sequence cursor since reset. The retained count and overwritten prefix length derive from the cursor and capacity. |
| `g_in` | Circular suffix of UI ingress, zero-filled before payload writes. Retained records have consecutive sequence numbers and nondecreasing timestamps. |
| `trace_loaded` | One base arena owns both array views until `trace_fmt_dump_free()`. Lengths describe initialized records. `has_input` distinguishes the legacy form from a present but empty INPT section. `in_dropped` is the retained input's starting sequence when there is input. A failed load leaves the object empty. |
| Reader cursor | Counts bytes transferred from offset zero. Clean EOF at the start of the INPT header identifies a timing-only dump. EOF after a partial header or payload is an error. |
| Writer batch | Holds the next initialized group of timing records in wire order. Each record derives its wire slot from the current ring. |
| Statistics scratch | One private base arena per pairing call holds two arrays sorted by frame and timestamp. Deduplication retains the earliest timestamp per frame. During merge, the caller's sample prefix contains only complete pairs with nonnegative gaps. Heapsort keeps the remaining prefix a max heap and the extracted suffix in final order. |

Record calls use plain writes for timing and ingress rings. Startup stamps
keep the v1 sequentially consistent atomics. `ring_put` skips the per-record
slot index store and the dump fills it from the slot. Dumps write through
`os_file_write()` at the caller's file position. The seekable loader uses
`os_file_pread()` from offset zero and leaves the caller's position alone.
Both complete short transfers through the OS API. The loader reserves lazy
arena storage bounded by the wire capacities, validates ordered ring indices
and matching record indices, and rejects malformed sections or I/O failures.
It accepts opaque timing event IDs for append-only readers. Replay uses
absolute monotonic deadlines. An unrepresentable deadline returns an error at that
event.

## Fixture provenance

The v1 fixture is 584 bytes (24-byte file header + 8-byte ring header +
three 16-byte timing records + 24-byte INPT header + ten 48-byte ingress
records). [Fixture](../../fuzz/corpus/trace/v1-6fec2e8.edtrace)

SHA-256

```text
a187ab845a675b2e3449b0d5b3e1d00d8260c289b472336a2d9b9539ff5581c9
```

A small driver linked the unmodified stock `6fec2e8` oracle's
`build/libedit.a` with the v1 headers. Its event calls match
`record_fixture_events()`, with v1's `trace_init()` at startup and the
slot owned through `trace_dump(stdout)`. [Test source](../../tests/trace_test.c)

Compile the driver with the following command.

```sh
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread \
    -I/home/tobias/Projects/sublimite-v1-oracle/src \
    build/trace-evidence/v1_fixture.c \
    /home/tobias/Projects/sublimite-v1-oracle/build/libedit.a \
    -ldl -lm -o build/trace-evidence/v1_fixture
```

The driver ran through `tests/contract/sandbox.py` as a digest-pinned v1
`RunTarget` with `DISPLAY` unset, a private case under
`build/trace-evidence/sandbox`, and stdout captured as the fixture.
The test compares the full v2 writer output against those bytes and loads the
same timing prefix as the historical timing-only form. The fuzzer replays
accepted dumps in fast mode and has seeds for both valid forms plus truncated
and invalid sections.

## Harvest exclusions

- `trace_init` and its counter/TLS reset path yield to static zero initialization and the quiescent history-only `trace_reset`.
- `g_next_ring` and dense high-water enumeration yield to bitmap claims and enumeration from the live history cursors.
- `trace_ring.index` and the hot-path `thread` store duplicated the array position, which now supplies both registration results and wire indices.
- The v1 display-connect marker shim is dropped. Native callers can emit `TRACE_STARTUP_DISPLAY_CONNECT` directly.
- `trace_now_ns` was a passthrough. Clock callers use `os_now_ns`.
- The public `trace_startup_get` accessor is dropped. The recorder checks retained stamps internally and dumps expose them to readers.
- `trace_input_count`, `trace_input_dropped`, and `trace_input_copy` had test-only callers. The loaded snapshot exercises the public retention contract.
- `trace_fmt.h` as a second public header is folded into `trace.h` and the private wire definitions.
- `trace_fmt_load` was a timing-only adapter. The unified loader also handles historical timing-only dumps.
- `trace_fmt_firsts`, `trace_gate_pair`, and the sorting comparators are private implementation details of the pairing and percentile APIs.
- `trace_fmt_startup_report` formats an offline CLI report. The trace module provides the raw startup timing records.
- The v1 heap growth heuristic yields to one lazy base arena bounded by the validated wire capacities.
- The stdio dump and load surface yields to borrowed `os_file` handles under the scoped symbol policy.
- The C library sort is replaced by in-place heapsort because glibc can use process heap scratch.
- Replay's historical 3e18 ns clamp yields to a representability check on the scaled deadline.
- The v1 trace `STATUS.md`, editor-specific comments, and v1 relative include paths are replaced by the v2 contract and this decision.
- The 100-frame G1/G3/T6 test and accumulated test-failure global yield to separate ring handoff tests and a three-pair arithmetic test derived from gaps of 1, 2 and 3 ns. Editor latency gates require editor composition.
- The handcrafted legacy-byte test and direct input-copy comparisons yield to the v1-produced fixture, byte-exact v2 output, historical timing prefix, and loaded wrap checks.
- The v1 stdio tempfile and large fixed scratch arrays yield to unlinked files under `build/` and a fixed buffer only for the byte-exact fixture comparison.
- The v1 allocation test's guard-inactive pass yields to a release Law-2 suite with base's live interposer and a counted positive control.
- `trace_pairs_test.c` was a launcher for the offline refwin Python report suite. That tool and the launcher, including its Windows branch, are outside L0.
- The fuzzer's unused replay sum is replaced with a delivered-record count assertion.
- The v1 benchmark harness, Windows handling, raw clock reference and repeated battery probes yield to one Linux power/load stamp and the same OS-clock reference as the recorder. The inherited batch sizes amortize timer reads. The benchmark always exits zero.

## Red and green

The first run used the five v1 trace files copied into v2 and the joined
worker spec without release. The process ran `TRACE_MAX_THREADS + 1`
workers, the first count that exposes the v1 leak. The second run added
release and used the same build and execution command.

```sh
set -o pipefail
(make --no-print-directory -s build/rel/tests/trace_test && build/rel/tests/trace_test) | tee build/trace-evidence/red.log
rc=$?
printf 'exit=%d\n' "$rc"
```

```text
trace sequential reuse: registered 16/17; dumped 16/17
exit=1
```

```sh
set -o pipefail
(make --no-print-directory -s build/rel/tests/trace_test && build/rel/tests/trace_test) | tee build/trace-evidence/green.log
rc=$?
printf 'exit=%d\n' "$rc"
```

```text
trace sequential reuse: registered 17/17; dumped 17/17
exit=0
```

The concurrent reuse test schedules two writers with relaxed atomics. The
first releases slot zero while the second is still running. The second
claims it and appends. The claim/release path supplies the synchronization
between writes and the joins synchronize the subsequent dump.

## Owner-read revision

The landed base guard replaces the trace-local link wrappers. The release
suite performs one deliberate allocation, then requires the guard active
with count zero for the cold and wrapping record windows. The wrapping
window also exercises all typed ingress helpers.

`trace_stats.c` is a new translation unit for the v1 pairing and percentile
algorithms that lived in `trace_fmt.h`. It has no file-scope mutable state.
Its per-call invariants are listed above. Pairing uses base scratch
storage and writes samples into caller-owned storage.

The scoped symbol checker initially rejected the old stdio, heap and sort
imports. On this system `getconf GNU_LIBC_VERSION` returned `glibc 2.39`.
`objdump -d --disassemble=qsort` on libc shows a jump to `qsort_r`, whose
disassembly contains calls to `malloc@plt` and `free@plt`. The in-place
sort therefore avoids a libc grant. The trace layer now uses OS and base
APIs for I/O and storage. This comparison exited 0 with empty output.

```sh
diff -u /home/tobias/Projects/sublimite-v2/tools/system-symbols.txt tools/system-symbols.txt
```

## Final acceptance with base present

```text
$ make --no-print-directory -s build/rel/tests/trace_test build/rel/tests/law2_trace_test build/trace_bench
$ build/rel/tests/trace_test
trace sequential reuse: registered 17/17; dumped 17/17
trace_test: PASS

$ build/rel/tests/law2_trace_test
trace guard positive control: active=1 count=1
trace cold record Law-2 window: active=1 count=0
trace wrapped records and input Law-2 window: active=1 count=0
law2_trace_test: PASS

$ make all
exit=0

$ make check-law2
check-law2: 2 release suites passed
exit=0

$ make check
check-san: 9 test binaries passed
check-law2: 2 release suites passed
check: ok
exit=0

$ make check-tsan
check-tsan: 3 threaded suites passed
exit=0

$ make fuzz-smoke
fuzz-smoke: 2 fuzzers replayed their seeds
exit=0
```

Full command output is under `build/trace-evidence/owner-*.log`.

## Initial benchmark

The initial v1-shaped and v2 measurements used the same build method.

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum -O2 -pthread -Isrc bench/trace_bench.c build/rel/libcore.a -ldl -lm -o build/trace-evidence/bench-v1
build/trace-evidence/bench-v1

gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum -O2 -pthread -Isrc bench/trace_bench.c build/rel/libcore.a -ldl -lm -o build/trace-evidence/bench-v2
build/trace-evidence/bench-v2
```

```text
(M)[AC] BAT0=Charging load1=1.77, 1000 batches of 1000 calls, nearest-rank batch means
trace_record p50=48 ns p99=72 ns
os_now_ns p50=45 ns p99=56 ns
trace_input p50=8 ns p99=47 ns

(M)[AC] BAT0=Charging load1=2.96, 1000 batches of 1000 calls, nearest-rank batch means
trace_record p50=29 ns p99=61 ns
os_now_ns p50=27 ns p99=42 ns
trace_input p50=5 ns p99=43 ns
```

The clock reference varied between runs. These observations record costs
under the stamped conditions. The benchmark is a tracking target
(`make build/trace_bench`).

The owner-read build also ran `build/trace_bench` and exited 0.

```text
(M)[AC] BAT0=Charging load1=2.60, 1000 batches of 1000 calls, nearest-rank batch means
trace_record p50=30 ns p99=56 ns
os_now_ns p50=28 ns p99=42 ns
trace_input p50=5 ns p99=40 ns
```

## Files

- `src/trace/{trace.h,trace_format.h,trace.c,trace_load.c,replay.c,trace_stats.c}` replace the empty directory's `.gitkeep` placeholder.
- `tests/{trace_test.c,law2_trace_test.c,lanes.mk}` replace the standalone `trace_alloc_test.c` guard with the release lane.
- `fuzz/trace_fuzz.c` and `fuzz/corpus/trace/*.edtrace`
- `bench/trace_bench.c` and `Makefile` build rules
- The landed scoped checker and test fixtures, base sources and lane support were copied into this checkout for the owner-read verification. The landed `tools/system-symbols.txt` has an empty trace diff.
- `docs/decisions/v2-0fv.4.md`

```text
$ wc -l src/trace/*
   26 src/trace/replay.c
  219 src/trace/trace.c
  160 src/trace/trace.h
   35 src/trace/trace_format.h
  106 src/trace/trace_load.c
  113 src/trace/trace_stats.c
  659 total
```
