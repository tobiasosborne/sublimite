Implemented standalone tabs: open/close/reopen, drag index moves, held MRU cycling, modified markers, persistent view/undo handles, and Unicode strip rendering. Switching uses cached title widths and performs no allocation, I/O, or layout.

Changed: `src/tabs/{tabs.h,tabs.c,STATUS.md}`, `tests/tabs_test.c`, `bench/tabs_bench.c`, `fuzz/tabs_fuzz.c`, and [P4.4.md](/home/tobias/Projects/editor/.wt/edit-457.4/docs/decisions/P4.4.md). No editor, main, frozen-header, or Makefile changes.

Red runs:
```text
undefined reference to `tabs_init'
make: *** [Makefile:87: build/tests/tabs_test] Error 1
tests/tabs_test.c:273: assertion failed: strcmp(names->title,"path")==0 && strcmp(names->path,"title")==0
```

Final green runs:
```text
tabs_test: sequences vs independent model passed
tabs_test: closed buffer, undo history and view retention passed
tabs_test: 10000 switches + strip + MRU mallocs=0 guard=active
tabs_test: all passed
make all exit=0
check: 29 test binaries passed
test_replay_cli: all passed
make fuzz exit=0
fuzz: 16 fuzzers built
```

Fuzz: **63,917 runs in 301 seconds (M)[AC], Full, load1=12.05**, with no crashes, model failures, or ASan/UBSan findings.

Final benchmark, **(M)[AC], Full, load1=10.91**, dedicated bench exit 0:
```text
switch+viewport_strip: p50_ns=2506 p99_ns=5039 gate_p99_ns=500000(G) PASS/TRACK
switch+all100_strip:   p50_ns=57164 p99_ns=72933 gate_p99_ns=500000(G) PASS/TRACK
memory: tab_set_bytes=557184 caller_undo_bytes=426400
        combined_bytes=983584 gate_bytes=1000000(G) excluding_buffers=1 PASS/TRACK
```

Initial uncached timing missed p99; caching title segmentation produced the final results above. Both runs are retained in the decision document.

Open problems: repository-wide `make bench` exited 2 on unrelated layout gates before reaching tabs. Layout p99 was 395,932/758,097 ns (M)[AC], pre-run load1=12.62, against 150,000 ns (G). Layout was left unchanged. Coordinator quiet-box verdicts, leak-enabled sanitizers, and integrated full-frame G3/app-total G10 measurements remain deferred with loop integration.