# edit-2cs — worker s6

Scoped changes are implemented; full acceptance is **not yet green** because
of missing corpus fixtures and sandbox socket restrictions. Do not close the
measurement requirement for editor-large or raster from this report.

## Files touched

* `Makefile`: separate default runtime list and explicit stale zygote target.
* `bench/editor_large_bench.args`: replace full TRACK run with find attribution.
* `bench/raster_bench.args`: new existing quick TRACK mode.
* `bench/savectl_bench.args`: new existing probe TRACK mode.
* `bench/scroll_bench.args`: new existing probe TRACK mode.
* `docs/decisions/P4.14.md`: append the port-versus-isolate decision.
* `docs/decisions/edit-2cs.md`: defaults, measured cost derivations, full commands,
  and outstanding measurement requirements.
* This report.

Local verification scripts, compiler output, and raw measurement logs are in
`build/edit-2cs/`; no production, bench C, test C, experiment C, forbidden
coordination document, or tracking metadata was edited. No `bd` or `git` ran.
No allocation path, compiler flag, or error-return convention changed.

## Red run

Wrote `build/edit-2cs/bench_contract.py` before implementing the dispatch or
args changes. `python3 build/edit-2cs/bench_contract.py` returned failure
(P: conventional shell failure status). It inspects `make -n bench`, requires
that its iteration list exclude zygote, requires an explicit target with a
stale diagnosis, and verifies each selected reduced args file.

```text
FAIL: default bench must exclude stale zygote runtime
FAIL: explicit bench-zygote target must explain stale API
FAIL: bench/editor_large_bench.args must select existing bounded mode: --attribute-find-floor
FAIL: bench/raster_bench.args must select existing bounded mode: --quick --track
FAIL: bench/savectl_bench.args must select existing bounded mode: --track --probe
FAIL: bench/scroll_bench.args must select existing bounded mode: --track --probe
```

The initially chosen editor single-sample probe was rejected during source
inspection because TRACK still rejects its short foreground population.
The expected mode above was updated and rerun red before implementation.
The contract oracle was subsequently corrected to inspect the iteration list
separately from the display-lock membership list; the latter intentionally
still contains the standalone zygote driver.

Independent reproduction, `sh tools/zygote_bench.sh --build`, also failed.
Representative unchanged compiler output follows; numeric positions and API
identifiers here refer to compiler-reported source locations, not budgets:

```text
variants/P4.14/a/open.c: In function ‘editor_close’:
variants/P4.14/a/open.c:29:10: error: ‘editor’ has no member named ‘index’
   29 |     if (e->index) lineidx_destroy(e->index);
      |          ^~
variants/P4.14/a/open.c:29:36: error: ‘editor’ has no member named ‘index’
   29 |     if (e->index) lineidx_destroy(e->index);
      |                                    ^~
variants/P4.14/a/open.c:30:10: error: ‘editor’ has no member named ‘file’
   30 |     if (e->file) file_close(e->file);
      |          ^~
variants/P4.14/a/open.c:30:30: error: ‘editor’ has no member named ‘file’
   30 |     if (e->file) file_close(e->file);
      |                              ^~
variants/P4.14/a/open.c:33:10: error: ‘editor’ has no member named ‘undo_ready’
   33 |     if (e->undo_ready) undo_destroy(&e->undo);
      |          ^~
variants/P4.14/a/open.c:33:37: error: passing argument 1 of ‘undo_destroy’ from incompatible pointer type [-Werror=incompatible-pointer-types]
   33 |     if (e->undo_ready) undo_destroy(&e->undo);
      |                                     ^~~~~~~~
      |                                     |
      |                                     undo_log **
In file included from src/tabs/tabs.h:5,
                 from src/editor/editor.h:7,
                 from src/editor/private.h:3,
                 from variants/P4.14/a/open.c:2:
src/undo/undo.h:119:29: note: expected ‘undo_log *’ but argument is of type ‘undo_log **’
  119 | void undo_destroy(undo_log *u);
      |                   ~~~~~~~~~~^
variants/P4.14/a/open.c: In function ‘editor_open’:
variants/P4.14/a/open.c:50:6: error: ‘editor’ has no member named ‘history_cap’
```

The failures extend beyond `lines` into old startup ownership and undo types.
See the appended P4.14 decision for why a port is not a bounded driver change.

## Green scoped run

```text
$ python3 build/edit-2cs/bench_contract.py
PASS: default bench excludes zygote; explicit stale target retained; bounded modes selected
$ make all
make: Nothing to be done for 'all'.
```

These commands returned success (P: shell success convention). The full release
build used the repository's gcc release rule with strict C warnings; the first
complete build is retained in `build/edit-2cs/all-before.log`. Compiler versions
are those required in the bead, confirmed by the installed compilers' version
output. Sanitizer builds use the unchanged clang ASan/UBSan rules.

`make bench-zygote` prints the stale diagnosis and preserves the compiler
failure. It is intentionally an explicit, failing experiment until ported;
default `make bench` no longer dispatches or compiles its variant runtime.
`make all` still builds `build/bench/zygote_bench` itself successfully.

## Measurements and bounded default verification

All measurements checked `/sys/class/power_supply/BAT0/status` immediately
before the run. `Not charging` means [AC] under this tree's existing stamp
policy. Durations below are (M)[AC]: printed benchmark timers or monotonic
end minus start; counts and fixture sizes are printed by the existing workload
branches. The docs decision gives the sample-count sources and arithmetic.
Native output retains pre-existing gate numbers without redefining them.
No elapsed time from a rejected display/fixture run is a per-sample measurement.

Save probe (successful; actual durable replacements, warm and verified cold):

```text
STAMP (M)[AC] BAT0=Not charging load1=15.11 command=['build/bench/savectl_bench', '--track', '--probe']
savectl_bench: full 1MiB / 1GiB warm-source / 1GiB verified cold-source durable replacement; repeated populations; supervised waits/retirement/shutdown
PROGRESS bytes=1048576 samples=0 (M)[AC]
POPULATION bytes=1048576 n=1 workers=1 wall_ns=62326457 (M)[AC]
BENCH name=savectl_snapshot_enqueue_1MiB_regression n=1 required_n=10000 p50=24427 p99=24427 ci95_p50=[24427,24427] ci95_p99=[24427,24427] gate_p50=2000000 gate_p99=5000000 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
BENCH name=G8d_1MiB_durable n=1 required_n=10000 p50=22372329 p99=22372329 ci95_p50=[22372329,22372329] ci95_p99=[22372329,22372329] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
BENCH name=journal_saved_1MiB n=1 required_n=10000 p50=31371148 p99=31371148 ci95_p50=[31371148,31371148] ci95_p99=[31371148,31371148] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
PROGRESS bytes=1073741824 samples=0 (M)[AC]
POPULATION bytes=1073741824 n=1 workers=1 wall_ns=14107825981 (M)[AC]
BENCH name=savectl_snapshot_enqueue_1GiB_warm_source_regression n=1 required_n=10000 p50=36367 p99=36367 ci95_p50=[36367,36367] ci95_p99=[36367,36367] gate_p50=2000000 gate_p99=5000000 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
BENCH name=G8d_1GiB_warm_source_durable n=1 required_n=10000 p50=5706324523 p99=5706324523 ci95_p50=[5706324523,5706324523] ci95_p99=[5706324523,5706324523] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
BENCH name=journal_saved_1GiB_warm_source n=1 required_n=10000 p50=6111281091 p99=6111281091 ci95_p50=[6111281091,6111281091] ci95_p99=[6111281091,6111281091] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=15.11 verdict=TRACK
PROGRESS bytes=1073741824 samples=0 (M)[AC]
POPULATION bytes=1073741824 n=1 workers=1 wall_ns=15789506428 (M)[AC]
BENCH name=savectl_snapshot_enqueue_1GiB_cold_source_regression n=1 required_n=10000 p50=4580876 p99=4580876 ci95_p50=[4580876,4580876] ci95_p99=[4580876,4580876] gate_p50=2000000 gate_p99=5000000 dropped=0 (M)[AC] power=Not charging load1=16.20 verdict=TRACK
BENCH name=G8d_1GiB_cold_source_durable n=1 required_n=10000 p50=7694562616 p99=7694562616 ci95_p50=[7694562616,7694562616] ci95_p99=[7694562616,7694562616] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=16.20 verdict=TRACK
BENCH name=journal_saved_1GiB_cold_source n=1 required_n=10000 p50=8246381204 p99=8246381204 ci95_p50=[8246381204,8246381204] ci95_p99=[8246381204,8246381204] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Not charging load1=16.20 verdict=TRACK
G8s saving-frame-submitted endpoint UNAVAILABLE: snapshot/enqueue row is a regression guard only

WALL (M)[AC] ns=29973320726 exit=0
```

Scroll probe (successful; exact unindexed jump and synthetic identity/work):

```text
STAMP (M)[AC] BAT0=Not charging load1=16.99 command=['build/bench/scroll_bench', '--track', '--probe']
G3z displayed_verdict=SYNTHETIC_PASS physical_verdict=UNSUPPORTED (M)[AC] load1=16.99 power=Not charging opportunities=10000 misses=0 repeated_stream_misses=5000 derivation=public_null_refresh_callbacks;alternate_previous_ID_injection=10000/2 physical_reason=native_completion_is_not_every_refresh_opportunity
WORKLOAD scroll bytes=1207959552 target_line=10000000 target_byte=1200000904 mapping=NEW indexed_guard=0 cold=0 samples=1 (G)
scroll_bench: TRACK=1 warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps
PROBE unindexed_owned_seek correct=1 ns=563320501 (M)[AC]
BENCH name=G7j_warm_unindexed_line_10000000_new_mapping_null_frame n=1 required_n=10000 p50=563320501 p99=563320501 ci95_p50=[563320501,563320501] ci95_p99=[563320501,563320501] gate_p50=30000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=17.85 verdict=TRACK
BENCH G3z_work_proxy (M)[AC] load1=17.85 power=Not charging n=10000 p50_ns=169259 p99_ns=6398784 max_ns=22274616 over_T_half=174 (G)max_ns<=4166666 (G)over_T_half=0 TRACK=1
BENCH name=G3z_null_work_proxy n=10000 required_n=10000 p50=169259 p99=6398784 ci95_p50=[167153,171264] ci95_p99=[6151167,7073565] gate_p50=4166666 gate_p99=4166666 dropped=0 (M)[AC] power=Not charging load1=17.85 verdict=TRACK
G3z displayed_verdict=UNAVAILABLE refreshes=0 required_refreshes=10000 misses=0 synthetic=no fractional=no derivation=(G)perf/01-perf-target.md:G3z;misses=sequence_gaps+invalid_displayed_opportunities;allowed_misses=zero
G3z work_proxy=CPU_REGRESSION_ONLY; displayed identity is checked separately through editor on_refresh; physical cadence requires an opportunity stream.

WALL (M)[AC] ns=16337686286 exit=0
```

Raster attempt (display unavailable; not measured):

```text
STAMP (M)[AC] BAT0=Not charging load1=17.94 command=['build/bench/raster_bench', '--quick', '--track']
TRACK name=A_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=17.94
TRACK name=A_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=17.94
TRACK name=B_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=17.94
TRACK name=B_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=17.94
SKIP G3/G3z/G3i: cannot open display; not measured

WALL (M)[AC] ns=3910144 exit=0
```

Editor-large attempt (mandatory sparse fixture absent; not measured):

```text
STAMP (M)[AC] BAT0=Not charging load1=17.94 command=['build/bench/editor_large_bench', '--attribute-find-floor']
editor_large_bench: UNAVAILABLE required fixtures

WALL (M)[AC] ns=3743093 exit=2
```

The args retain existing reduced counts. Save and scroll only expose their
singleton probe as a reduced population; the measured full-population
extrapolations exceed the supplied session threshold. Raster's existing quick
mode and editor-large's existing find attribution are **provisional** because
their required measured per-sample cost cannot be obtained in this sandbox.
The full matrix remains reachable by the commands in the decision document.

## Every Makefile change

The complete patch follows (P: hunk numbers are positions calculated from
original and final text). No other Makefile line was changed. In the final
file: added lines for the default list at source positions 63–64; extended
`.PHONY` at 75; changed `bench` prerequisite and recipe at 145–146; added blank
separation, explanatory comment, explicit target, diagnosis, and recipe at
147–151 (P: positions from `nl -ba Makefile`).

```diff
--- Makefile before
+++ Makefile after
@@ -60,6 +60,8 @@
 # automatically. All callers share tools/with_display_lock.sh.
 DISPLAY_TEST_SAN := $(filter $(B)/san/tests/cli_test $(B)/san/tests/editor% $(B)/san/tests/x11% $(B)/san/tests/ref% $(B)/san/tests/gl% $(B)/san/tests/raster_test,$(TEST_SAN))
 BENCH_BIN := $(patsubst bench/%.c,$(B)/bench/%,$(BENCHS))
+# P4.14 copies stale editor internals; retain its driver build in all.
+DEFAULT_BENCH_BIN := $(filter-out $(B)/bench/zygote_bench,$(BENCH_BIN))
 # edit-cx8: display benches take the same lock. On a shared Xvfb another
 # worktree's suite (XTEST keys, setxkbmap) otherwise types into the bench
 # window mid-row: foreign input sequences, keymap-worker allocations.
@@ -70,7 +72,7 @@
 
 $(B)/tests/wayland_test $(B)/tests/wayland_gl_test $(B)/san/tests/wayland_test $(B)/san/tests/wayland_gl_test $(B)/fuzz/wayland_fuzz: LDLIBS += $(WAYLAND_TEST_LIBS)
 
-.PHONY: all lib check check-sh bench fuzz clean
+.PHONY: all lib check check-sh bench bench-zygote fuzz clean
 .SECONDARY:
 all: $(REL_LIB) $(EDIT_BIN) $(TOOL_BIN) $(TEST_REL) $(BENCH_BIN)
 lib: $(REL_LIB)
@@ -140,8 +142,13 @@
 # Benches: release build, each must exit 0 (gate met).
 # Per-bench default args: optional one-line file bench/<name>.args (e.g. piece_bench.args = --quick;
 # the full piece matrix is run by tools/bench_variant.sh on an idle box).
-bench: $(BENCH_BIN)
-	@set -e; $(SAFE_DISPLAY); for b in $(BENCH_BIN); do a=bench/$$(basename $$b).args; args=$$(cat $$a 2>/dev/null || true); echo "== $$b $$args"; case " $(DISPLAY_BENCH) " in *" $$b "*) DISPLAY=$$d sh tools/with_display_lock.sh $$b $$args;; *) DISPLAY=$$d $$b $$args;; esac; done; echo "bench: $(words $(BENCH_BIN)) benches passed"
+bench: $(DEFAULT_BENCH_BIN)
+	@set -e; $(SAFE_DISPLAY); for b in $(DEFAULT_BENCH_BIN); do a=bench/$$(basename $$b).args; args=$$(cat $$a 2>/dev/null || true); echo "== $$b $$args"; case " $(DISPLAY_BENCH) " in *" $$b "*) DISPLAY=$$d sh tools/with_display_lock.sh $$b $$args;; *) DISPLAY=$$d $$b $$args;; esac; done; echo "bench: $(words $(DEFAULT_BENCH_BIN)) benches passed"
+
+# Explicit experiment only: compile failures remain visible until it is ported.
+bench-zygote: $(REL_LIB)
+	@echo "P4.14 zygote experiment is stale: variants/P4.14/editor.c and */open.c copy an older src/editor API (editor.lines); see docs/decisions/P4.14.md"
+	@$(SAFE_DISPLAY); DISPLAY=$$d sh tools/zygote_bench.sh --run
 
 fuzz: $(FUZZ_BIN)
 	@echo "fuzz: $(words $(FUZZ_BIN)) fuzzers built"
```

## What remains / outside this bead

* Coordinator: restore required corpus fixtures, rerun editor-large attribution,
  measure its actual per-sample costs, and confirm the chosen reduced default.
* Coordinator: run raster quick mode on Xvfb :99 (display identifier supplied
  by the bead), measure its per-sample costs, and confirm or revise the args.
* Default `make all && make bench` built successfully and stopped at the live
  editor backend's `editor_open` failure. The sandbox cannot connect to X even
  though the socket pathname is visible. Display benches left for the
  coordinator: editor, editor-large (also fixture-blocked), file, GL, raster,
  and X11. Prewake's standalone mode only prints its protocol instructions;
  Wayland has no compositor configured. The explicit stale zygote target has
  its independent compile blocker.
* A separate default non-display bench run stopped at `find_bench`: missing
  `/tmp/edit-corpus/all_a_1g.txt`. Editor-large independently requires missing
  `/tmp/edit-corpus/sparse_10g.bin`, even for its find-only attribution mode.
  These are observed environment/fixture issues; no corpus or bench source
  repair was made.
* Continued non-display benches exposed `ipc_bench` failing
  `ipc_server_init(&server,dir)==IPC_OK`. Unix sockets are restricted here;
  this is independent of the dispatch/args changes. Its failure was retained.
* TRACK timing misses are descriptive output, not defects changed here.

Final sanitizer and remaining-bench results are recorded below. Full checks did not pass; these findings were not repaired outside scope.

## Reproducible driver contract

The verification script is retained here so it is reviewable even when build
artifacts are removed. It introduces no sample count or runtime budget.

```python
"""Build-driver contract; counts are selected through existing probe flags."""
from pathlib import Path
import subprocess

makefile = Path('Makefile').read_text()
failures = []
dry = subprocess.run(['make', '-n', 'bench'], capture_output=True, text=True)
run_lines = [line for line in dry.stdout.splitlines() if 'for b in ' in line]
run_lists = []
for line in run_lines:
    before, separator, after = line.partition('for b in ')
    run_list, separator, after = after.partition('; do')
    run_lists.append(run_list)
if not run_lists or any('build/bench/zygote_bench' in run_list for run_list in run_lists):
    failures.append('default bench must exclude stale zygote runtime')
if 'bench-zygote:' not in makefile or 'stale' not in makefile.lower():
    failures.append('explicit bench-zygote target must explain stale API')
expected = {
    'editor_large_bench': '--attribute-find-floor',
    'raster_bench': '--quick --track',
    'savectl_bench': '--track --probe',
    'scroll_bench': '--track --probe',
}
for name, flags in expected.items():
    path = Path('bench') / (name + '.args')
    if not path.exists() or path.read_text().strip() != flags:
        failures.append(str(path) + ' must select existing bounded mode: ' + flags)
for failure in failures:
    print('FAIL:', failure)
if failures:
    raise SystemExit(True)
print('PASS: default bench excludes zygote; explicit stale target retained; bounded modes selected')
```

## Final validation results

All sanitizer binaries compiled with the unchanged clang ASan/UBSan rules.
Plain `make check` stopped in LeakSanitizer before any useful suite verdict:

```text
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/x11_xi2_test.o build/san/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/x11_xi2_test
== build/san/tests/base_test
==641==LeakSanitizer has encountered a fatal error.
==641==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==641==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:133: check] Error 1
```

Reran with `ASAN_OPTIONS=detect_leaks=0` (P: the sanitizer option disables leak
scanning only; ASan and UBSan instrumentation remain enabled). This avoids the
sandbox's ptrace limitation. Leak checking has not been certified here.

```
ASAN_OPTIONS=detect_leaks=0 make check
ASAN_OPTIONS=detect_leaks=0 python3 build/edit-2cs/run-nondisplay.py check
ASAN_OPTIONS=detect_leaks=0 python3 build/edit-2cs/check-remaining.py
ASAN_OPTIONS=detect_leaks=0 make check TEST_SAN=
```

The first command passed base and stopped in CLI's IPC initialization. The
non-display Makefile invocation passed base/config/config-editor/config-worker
and stopped at `file_kill_test`. The remaining driver then ran every remaining
suite in the Makefile's non-display list, retaining each failure and continuing.
The final command ran the shell contracts alone after binary-suite testing;
replay CLI and display-lock contracts passed. Their intentional negative cases
print errors, so success is determined by command exit and the final PASS line.

Remaining **non-display-list failures**, all outside this bead's edited C files:

| Suite | Observed result / source location |
| --- | --- |
| `file_kill_test` | Native viewport init failed; queued open/save children failed. This suite requires X despite not appearing in the existing display list. |
| `file_test` | `tests/file_test.c:308`: the many-watch fixture did not observe both changed targets. |
| `findui_test` | `tests/findui_test.c:81`: worker result wait exhausted and asserted. |
| `ipc_test` | Init failed with `Operation not permitted`; socket capability probe below confirms the environment restriction. |
| `journal_kill_test` | First durability/kill trial reported failure; no journal C changed. |
| `lineidx_test` | `tests/lineidx_test.c:339`: combined ownership fixture failed its ready/mmap assertion. |

Source locations are compiler/test-reported identifiers. Exit codes and signal
values in native output use (P) operating-system/program status conventions;
these are not counts or budgets. Every other suite in that non-display list
passed with ASan/UBSan enabled and leak scanning disabled. No claim is made
that `make check` as a whole is green.

Native failure evidence (all runs stamped (M)[AC], BAT0 `Not charging`):

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=ASan-inert (release required)
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
make: *** [Makefile:133: check] Error 1
tests/file_test.c:308: FAIL file_changed(files[targets[0]]) && file_changed(files[targets[1]])
EXIT build/san/tests/file_test: 1
tests/findui_test.c:81: assertion failed: 0
EXIT build/san/tests/findui_test: -6
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
EXIT build/san/tests/ipc_test: 1
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
EXIT build/san/tests/journal_kill_test: 1
tests/lineidx_test.c:339: FATAL file_open_ready(f) && file_open_mode(f) == FILE_MODE_MMAP
EXIT build/san/tests/lineidx_test: 1
```

Socket capability check (local worktree socket, no external service):

```text
STAMP (M)[AC] BAT0=Not charging
Unix socket capability: [Errno 1] Operation not permitted
```

The Makefile's display-list suites were compiled and are deferred to the
coordinator's Xvfb environment. CLI was attempted and failed as described;
remaining listed display suites were not dispatched here:

`cli_test`, `editor_close_test`, `editor_hot_exit_test`, `editor_large_test`, `editor_p4_test`, `editor_scroll_test`, `editor_session_test`, `editor_test`, `gl_bench_test`, `gl_gate_test`, `gl_scroll_test`, `gl_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_alignment_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_display_guard_test`, `x11_font_size_test`, `x11_identity_test`, `x11_input_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test`, `x11_xi2_test`.

Default bench failure, after successful release build:

```text
$ make all && make bench
make: Nothing to be done for 'all'.
G1 null workload=queued_index_find_save keys=10000 completed=10000 arrivals_active=0 coalesced=7500 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=18.89 shared box
editor_bench:555 failed: editor_open(&e, &cfg, &b) == 0
make: *** [Makefile:146: bench] Error 1
```

The separate non-display default run retained the missing-corpus failure:

```text
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
fixture missing/wrong size: /tmp/edit-corpus/all_a_1g.txt
find_bench: STRUCTURAL FAILURE (quick: limits off)
make: *** [Makefile:146: bench] Error 1
```

The remaining non-display bench driver completed. Only `ipc_bench` failed;
all other dispatched remaining benches succeeded. `wayland_bench` returned its
explicit unmeasured skip, rather than measuring without a compositor. Save and
scroll defaults succeeded again through their `.args` selections. Numeric
return values below are (P) program exit conventions, not performance counts:

```text
EXIT build/bench/findui_bench: 0
EXIT build/bench/font_bench: 0
EXIT build/bench/hl_bench: 0
EXIT build/bench/indent_bench: 0
ipc_bench: failed ipc_server_init(&server,dir)==IPC_OK
EXIT build/bench/ipc_bench: 2
EXIT build/bench/journal_bench: 0
EXIT build/bench/keys_bench: 0
EXIT build/bench/layout_bench: 0
EXIT build/bench/lineidx_bench: 0
EXIT build/bench/minimap_bench: 0
EXIT build/bench/piece_bench: 0
EXIT build/bench/piece_reclaim_bench: 0
EXIT build/bench/render_bench: 0
EXIT build/bench/savectl_bench: 0
EXIT build/bench/scan_bench: 0
EXIT build/bench/scroll_bench: 0
EXIT build/bench/tabs_bench: 0
EXIT build/bench/trace_bench: 0
EXIT build/bench/undo_bench: 0
EXIT build/bench/utf8_bench: 0
EXIT build/bench/view_bench: 0
EXIT build/bench/wayland_bench: 0
EXIT build/bench/work_bench: 0
```

Shell-contract green evidence:

```text
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Raw complete logs and local drivers: `build/edit-2cs/`. The unresolved corpus,
X measurement, socket, notification, worker-wait, and kill-test findings belong
to the coordinator/environment or separate beads; none was fixed here.
This worker stops after edit-2cs.
