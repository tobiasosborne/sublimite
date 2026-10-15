# edit-34l — s7 worker report

Search engine implementation is complete within this bead. No editor keys,
panel or buffer wiring was added. No bd or git command was run. HANDOFF,
docs/worklog, CLAUDE, PLAN and PRD were not edited. The design and part-two
integration contract are in `docs/decisions/edit-34l.md` and `src/fif/fif.h`.

Files touched

- `src/fif/fif.h`, `src/fif/private.h`, `src/fif/fif.c`,
  `src/fif/filter.c`, `src/fif/worker.c`: copied request, owned/prepaid arena,
  bounded deterministic DFS, Where parser/globs, worker file reads, core search,
  stable result accessors, cancellation, independent completion and retry.
- `src/find/options.h`, `src/find/options.c`: extract the existing findui regex
  ASCII case-expansion adapter into a reusable option layer.
- `src/findui/worker.c`: call that shared adapter. The existing findui literal
  implementation and frozen find API/kernel were left alone.
- `tests/tree_fixture.h`, `tests/fif_test.c`: nested mkdtemp tree, supplied byte
  files, binary probe, .git, symlink loop, deterministic line/column/offset
  expectations, regex/folding/word/filter cases, file/entry/depth/hit limits,
  context truncation, invalid-init cleanup, mailbox retry, queued and mid-walk
  cancellation, and guarded allocation-free draining/accessors.
- `fuzz/fif_fuzz.c`: arbitrary Where parser and glob inputs, invariants and a
  POSIX fnmatch comparison on the common literal/star/question-mark subset.
- `bench/fif_bench.c`: walk+search through DONE, corpus/default fixture selection,
  power/load, descriptive percentiles and the tracked scaled aspiration.
- `Makefile`: document automatic fif discovery through the existing module,
  test, fuzz and bench globs. No extra libraries are required.
- `perf/01-perf-target.md`: G6f tracked aspiration with its derivation.
- `docs/decisions/edit-34l.md`, this report.

Red runs, before implementation/fix

The initial test suite and fixture helper were written before fif existed.
The following is the actual red compile output. Its missing-header diagnostic
was the expected initial failure. All terminal evidence here is (M)[AC]: BAT0
reported Full; toolchain version output identified gcc 13.3.0 and clang 18.1.3.
Version numbers identify the installed toolchain, rather than a selected budget.

```text
$ make -j4 build/san/tests/fif_test
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/fif_test.c -o build/san/tests/fif_test.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/base/base.c -o build/san/src/base/base.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/base/malloc_guard.c -o build/san/src/base/malloc_guard.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/config/parser.c -o build/san/src/config/parser.o
tests/fif_test.c:1:10: fatal error: 'fif/fif.h' file not found
    1 | #include "fif/fif.h"
      |          ^~~~~~~~~~~
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/config/runtime.c -o build/san/src/config/runtime.o
1 error generated.
make: *** [Makefile:152: build/san/tests/fif_test.o] Error 1
make: *** Waiting for unfinished jobs....
```

A later regression test supplied a filter exceeding its NUL-inclusive PATH_MAX
capacity. Before fixing failed-init cleanup, that test exposed an arena
self-unmap: edit_arena_free clears its descriptor after munmap, so the descriptor
must be copied to the stack before freeing memory that contains it. Actual red
output follows; addresses, source positions and process identities are raw
(M)[AC] diagnostics, not authored numeric limits.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/fif_test
AddressSanitizer:DEADLYSIGNAL
=================================================================
==36==ERROR: AddressSanitizer: SEGV on unknown address 0x7cbc5df4b000 (pc 0x7cbc73589880 bp 0x7ffe30434b80 sp 0x7ffe30434348 T0)
==36==The signal is caused by a WRITE memory access.
    #0 0x7cbc73589880 in __memset_avx2_unaligned_erms string/../sysdeps/x86_64/multiarch/memset-vec-unaligned-erms.S:378
    #1 0x5d8f40677e1e in __asan_memset (/home/tobias/Projects/sublimite/.wt/edit-34l-s7/build/san/tests/fif_test+0x16ce1e) (BuildId: 81a2e7e70ece775d16c25f84db163514636caf99)
    #2 0x5d8f406bebc8 in edit_arena_free /home/tobias/Projects/sublimite/.wt/edit-34l-s7/src/base/base.c:48:5
    #3 0x5d8f406c1318 in fif_job_init /home/tobias/Projects/sublimite/.wt/edit-34l-s7/src/fif/fif.c:58:27
    #4 0x5d8f406b9bbb in limits_and_errors /home/tobias/Projects/sublimite/.wt/edit-34l-s7/tests/fif_test.c:108:5
    #5 0x5d8f406b9bbb in main /home/tobias/Projects/sublimite/.wt/edit-34l-s7/tests/fif_test.c:181:28
    #6 0x7cbc7342a1c9 in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
    #7 0x7cbc7342a28a in __libc_start_main csu/../csu/libc-start.c:360:3
    #8 0x5d8f405defd4 in _start (/home/tobias/Projects/sublimite/.wt/edit-34l-s7/build/san/tests/fif_test+0xd3fd4) (BuildId: 81a2e7e70ece775d16c25f84db163514636caf99)

AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: SEGV string/../sysdeps/x86_64/multiarch/memset-vec-unaligned-erms.S:378 in __memset_avx2_unaligned_erms
==36==ABORTING
```

Green runs

The final release build and fif tests returned success:

```text
$ make -j4 all
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/fif_test.c -o build/rel/tests/fif_test.o
gcc -O2 -g -msse2 -pthread build/rel/tests/fif_test.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/fif_test
$ build/tests/fif_test
fif_test: filters, search, walker, cancellation, truncation, retry and allocation guard passed
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/fif_test
fif_test: filters, search, walker, cancellation, truncation, retry and allocation guard passed
```

The release drain test uses the real base counting allocator; the sanitizer
build's allocator hook is inert because ASan owns malloc. The test deliberately
stalls a mailbox before guarding the drain/accessor work, with only the already
acquired root directory left to process. Queued cancellation parks a prior bulk lease behind an atomic gate, so the
search cannot start before cancellation. The guard waits for a new full-mailbox
observation relative to its pre-submit counter baseline; earlier saturation
tests cannot accidentally satisfy it. The later cancellation case uses the
full walk and stops while the first text file's hit delivery is stalled.

The requested make check selection returned success, including its replay and
display-lock shell contracts. The latter deliberately exercises lock failures,
so its expected timeout messages are not a test failure. Verbatim output:

```text
$ ASAN_OPTIONS=detect_leaks=0 make -j4 check TESTS='tests/fif_test.c tests/findui_test.c tests/find_test.c'
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/fif_test.c -o build/san/tests/fif_test.o
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/fif_test.o build/san/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/fif_test
== build/san/tests/fif_test
fif_test: filters, search, walker, cancellation, truncation, retry and allocation guard passed
== build/san/tests/findui_test
edit-m15 find panel: clipboard/query chords isolated from document passed
P4-3 section 29: sparse window new source-search stages=0 (M)[AC]
P4-3 sparse replacement new source-search stages=0 (M)=scan-hook delta
P4-3 irregular folded literal/regex plan: protected source window and ordinal passed
P4-3 matcher yields before completion: pattern=ab unfinished=1 (M)=bulk marker
P4-3 matcher yields before completion: pattern=[a]b unfinished=1 (M)=bulk marker
P4-3 matcher yields before completion: pattern=x+ unfinished=1 (M)=bulk marker
S27 noop pages: complete=1 count=33 cached=8 total=33
S30 cancelled source: retained_storage_leases=2
S30: PASS full arena lease, cancelled eviction/reload, maintenance-only retirement acknowledgement
S29 host mutation: mode=0 revision=4 records=4 lines=1
S29 host mutation: mode=1 revision=1 records=1 lines=2
S29 host mutation: mode=2 revision=0 records=0 lines=3
S29: PASS host preflight, newline bookkeeping, partial edit journal/recovery and undo
S28 large match: code=1 replaced=0 remaining_bytes=1040384
S28: PASS bounded large-match deletion, admission, cancellation and exact undo/redo
S27 beyond cache: mode=0 begin=0 count=5000
S27 beyond cache: mode=1 begin=0 count=5000
S27 beyond cache: mode=2 begin=0 count=5000
S27: PASS paged replacement beyond cache, cancellation, fault and exact undo/redo
S26: PASS first 4096 offsets with initial, late and empty windows
S25 viewport: complete=1->1 count=512->512 selected=0->0
S25: PASS viewport retains exact count and selection
S24 saturated mailbox: next_bulk_started=1
S24: PASS full mailbox yields sole bulk lane without cancellation/draining
S23 word publication: regex=0 count=600 visible=600 ranges=8
S23 word publication: regex=1 count=600 visible=600 ranges=8
S23: PASS bounded word publication with exact visible overflow
S22 word worker: count=524288 scans=1
S22: PASS bounded literal whole-word visitor and rejected overlap
S22 regex word trace: pattern=a+ count=4096 stages=1 ranges=1
S22 regex word trace: pattern=[a] count=4096 stages=1 ranges=1
S22: PASS persistent regex whole-word visitor, boundaries, empty matches and rejected overlap
S21 maximum literal: binary=0 case=0 complete=1 error=0 count=2
S21 maximum literal: binary=0 case=1 complete=1 error=0 count=2
S21 maximum literal: binary=1 case=0 complete=1 error=0 count=2
S21 maximum literal: binary=1 case=1 complete=1 error=0 count=2
S21: PASS maximum ASCII/binary literals in both case modes
P1R9 filtered regex: complete=0 error=6 scans=1
P1R8 dense worker: count=1048576 cached=8 scans=1
P1R8: PASS bounded core count, visible overflow, late window, lazy ordinal, regex
findui_test: incremental cancellation, stale filtering, worker thread-id trace passed
findui_test: wrap-around, uncached navigation, late-window highlights and clipping passed
findui_test: regex/case/word toggles, malformed regex and byte queries passed
findui_test: replace-one/all, sliced single undo group, exact undo/redo and empty regex matches passed
findui_test: allocator faults and replacement cancellation preserve one exact undo group passed
findui_test: mailbox backpressure/cancel, source versions, alias edits and empty windows passed
findui_test: bottom-row rendering, damage validation and typing allocation guard passed
findui_test: all passed
== build/san/tests/find_test
find_test: ok (frozen P1.10a)
check: 3 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 118)
true: display lock timeout: display :99 (held by 118)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 118)
refwin_test: display lock timeout: display :99 (held by 118)
make[1]: warning: jobserver unavailable: using -j1.  Add '+' to parent make rule.
== build/san/tests/x11_identity_test
waiting for display :99 (held by 118)
x11_identity_test: display lock timeout: display :99 (held by 118)
make[1]: *** [Makefile:207: check] Error 1
waiting for display :99 (held by 118)
sh: display lock timeout: display :99 (held by 118)
waiting for display :99 (held by 118)
sh: display lock timeout: display :99 (held by 118)
waiting for display :99 (held by 118)
sh: display lock timeout: display :99 (held by 118)
waiting for display :99 (held by 118)
sh: display lock timeout: display :99 (held by 118)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The unchanged findui and find suites also passed in release:

```text
$ build/tests/findui_test
findui_test: regex/case/word toggles, malformed regex and byte queries passed
findui_test: replace-one/all, sliced single undo group, exact undo/redo and empty regex matches passed
findui_test: allocator faults and replacement cancellation preserve one exact undo group passed
findui_test: mailbox backpressure/cancel, source versions, alias edits and empty windows passed
findui_test: bottom-row rendering, damage validation and typing allocation guard passed
findui_test: all passed
$ build/tests/find_test
find_test: ok (frozen P1.10a)
```

Full-suite accounting and sandbox limits

`ASAN_OPTIONS=detect_leaks=0 make -j4 check` built every sanitizer suite, then
stopped at the first display suite, cli_test:

```text
== build/san/tests/cli_test
cli_test:484: FAIL ipc_server_init_display(&server, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:207: check] Error 1
```

A selection using the Makefile's non-display classification stopped later at
file_kill_test. The remaining suites were then run serially with return codes
recorded, without stopping at a failure. (M)[AC] Counts were computed by listing
`tests/*_test.c`, applying the Makefile's display-name exclusions and combining
recorded process return codes: 66 classified non-display suites = 64 passes +
two sandbox-blocked suites. No selected suite is unaccounted for. The 64 passes
include base/config/dataloss/fif/findui/find, plus:

```text
config_editor_test file_test find_bench_test find_bestof_test find_p1r_test find_simd_filter_verify_test find_timeout_test font_cli_test font_selection_test font_size_test font_test harness_test hl_parse_test hl_test hl_worker_test indent_test journal_group_test journal_kill_test journal_retire_test journal_test keyboard_guard_test keys_test layout_test lineidx_test minimap_test piece_bounds_test piece_mem_test piece_mt_test piece_test prewake_test raster_scroll_test render_test replay_test runtime_test savectl_reload_test savectl_test scan_test scroll_display_test scroll_test startup_test tabs_test trace_pairs_test trace_test undo_test unicode_render_test utf8_tables_test utf8_test view_test wayland_bench_test wayland_gl_test wayland_startup_test wayland_test window_size_test work_foreground_test work_test wrap_test
```

The blocked suites are outside this bead and were not fixed:

- `file_kill_test` is incorrectly classified as non-display by the existing
  Makefile. Its viewport test calls view_init and its queued-open/save child
  tests use a real platform/backend. They fail because the sandbox has no X
  socket. The observed assertions are at its rc check and child-exit check.
- `ipc_test` fails ipc_server_init with `errno=1 (Operation not permitted)`
  while creating its local server. This is the sandbox's socket restriction,
  not a fif failure. Its null rendering backend does not remove its need for
  a real local IPC socket.

The coordinator must rerun the display suites, including file_kill_test, on
Xvfb :99 and rerun ipc_test with local socket permission. A full unrestricted
`DISPLAY=:99 make -j4 check` is still required; full-check success is not claimed
inside this sandbox. ASan/UBSan remain enabled for every sanitizer run; only
LeakSanitizer is disabled because its exit scan fails under this sandbox's
ptrace restriction. The initial attempt with leak detection enabled reported:

```text
==3==LeakSanitizer has encountered a fatal error.
==3==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==3==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Fuzz and performance

The standard target `make -j4 build/fuzz/fif_fuzz` passed. The smoke run below
also returned success. Its count is (E) the existing default entry capacity,
FIF_DEFAULT_ENTRIES = twice FIND_MAX_OFFSETS = 8192; its max input length is (P)
three independently NUL-terminated PATH_MAX inputs, 3 × (4096−1) = 12285 bytes.
These are reused production/test bounds, not a new timing budget. Runtime,
coverage, feature and corpus counts below are raw (M)[AC] fuzzer observations.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/fif_fuzz -runs=8192 -max_len=12285 build/fif-fuzz-corpus
#8192	pulse  cov: 303 ft: 1298 corp: 170/200Kb lim: 12285 exec/s: 122 rss: 68Mb
#8192	DONE   cov: 303 ft: 1298 corp: 170/200Kb lim: 12285 exec/s: 122 rss: 68Mb
Done 8192 runs in 67 second(s)
```

Both bench paths returned success. The main corpus run was capped: larger files
are skipped under the documented default file-copy limit. Consequently its
reported bytes are bytes actually read, not the entire corpus size. The miss
is a tracked performance observation; no timing gate was added. (M)[AC] raw
bench results, with load/power stamps:

```text
$ build/bench/fif_bench
fif_bench: (M)[AC] power=Full load1=3.58 fixture=/tmp/edit-corpus
fif_bench: bytes=339738656 files=7 truncated=1 errors=0 code=0 targets=(E)G6_scaled_by_read_bytes
BENCH name=G6f_walk_search_tracked n=100 p50=492069773 p99=646605105 ci95=[479766481,501624988] gate_p50=27179092 gate_p99=42467332 pass=0 MISS power=[AC]
$ build/bench/fif_bench build/fif-bench-missing
fif_bench: (M)[AC] power=Full load1=4.07 fixture=/tmp/fif-tree-TIWr8J
fif_bench: bytes=62 files=3 truncated=0 errors=0 code=0 targets=(E)G6_scaled_by_read_bytes
BENCH name=G6f_walk_search_tracked n=100 p50=75970 p99=216487 ci95=[75255,76768] gate_p50=4 gate_p99=7 pass=0 MISS power=[AC]
```

The sample count and aspiration arithmetic are (E), documented beside the code
and in the new perf row: descriptive p99 needs 1/(1−0.99) = 100 observations to
have one tail order statistic; the G6 A 80/125 ms per decimal GB aspiration is
scaled by actual bytes read. Metadata/open/read work is unmodelled, so these
optimistic figures are never enforced.

Remaining work and limits

Part two, edit-n95, owns the UI and keys. It must preserve each job until both
leases physically finish and all mail is drained, route both identities for a
selective receiver, reject obsolete generations and render cancellation,
truncation and error states. Public docs explain one-based byte positions,
ASCII case/word semantics and original-line context truncation.

The required opendir/readdir API allocates a libc DIR object on worker directory
acquisition. Fif has no direct malloc/calloc/realloc calls and its file-search,
result-storage, sort, drain/accessor and retry loops use prepaid base memory.
Eliminating libc's directory-handle allocation would require changing the
mandated walker interface; this caveat is explicit in the decision document.

Other findings outside scope: the requested codemap file was absent; the old
regex option adapter was private and had to be factored for reuse; file_kill's
display classification and the IPC sandbox failure are reported above. No
unrelated fixes, tracking commands, handoff/worklog changes or editor wiring
were performed. No engine deliverable remains unimplemented.
