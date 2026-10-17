# edit-ywd — command palette worker report

Worktree: `/home/tobias/Projects/sublimite/.wt/edit-ywd-s8`.
Only edit-ywd was worked. No `bd` or `git` commands were run. The protected
handoff, worklog, rules, plan and PRD files were not edited.

## Files touched

- `src/palette/palette.h`, `src/palette/palette.c`: table-derived labels,
  default-argument eligibility, bounded query state, minimum-gap fuzzy matching
  and stable index ordering.
- `src/editor/palettepanel.c`: panel lifecycle, key capture and painting.
- `src/editor/private.h`, `src/editor/open.c`: arena reservation and private
  palette state; shared expanded-find-panel row definition.
- `src/editor/input.c`: shared dispatcher factoring, palette capture and closure
  in mutation bookkeeping.
- `src/editor/editor.c`, `src/editor/findpanel.c`: geometry, focus/source
  lifecycle and panel paint routing.
- `src/keys/keys.h`, `src/keys/keys.c`: live command binding resolution, chord
  display formatting, implemented command-palette binding.
- `tests/palette_test.c`, `tests/editor_test.c`, `tests/keys_test.c`: pure and
  integration contracts and the updated binding expectation.
- `fuzz/palette_fuzz.c`, `bench/palette_bench.c`: bounded-input invariant harness
  and table-derived filter samples.
- `docs/decisions/edit-ywd.md`, this report.

Bead-local build logs, preserved red binaries and the verification runner are
under `build/edit-ywd/` in this worktree.

## Red runs

Tests were written before palette implementation. The first pure build failed
because its requested API did not exist:

```text
$ make -j3 build/san/tests/palette_test
tests/palette_test.c:1:10: fatal error: 'palette/palette.h' file not found
make: *** [Makefile:152: build/san/tests/palette_test.o] Error 1
exit=2
```

The pure runtime test then exposed named-keysym output instead of the literal
punctuation users type. This run used the sanitizer library and a directly
compiled palette module before the punctuation correction:

```text
$ ASAN_OPTIONS=detect_leaks=1 build/edit-ywd/palette_test-red
palette_test:64: FAIL keys_chord_format(&plus, text, sizeof text) == 0 && !strcmp(text, "Ctrl++")
palette_test:84: FAIL chords() == 0
exit=1
```

The editor test was built against the original editor before connecting the
palette hooks. The preserved binary reproduced the intended runtime failure
under the display lock with a private runtime directory and leaks enabled:

```text
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/edit-ywd/editor_test-red --ywd-palette
editor_test:3912: FAIL e->panel_rows == 3u
exit=1
```

Here the panel-height assertion derives from the existing query/extra-field/
hints rows (P). Exit codes are process results (M)[AC]; source line numbers are
compiler/assertion locations (P), not budgets or fixture counts.

An initial test-authoring compile used `findui_mutation_result.mutated` instead
of its actual `changed` member; corrected before the editor runtime red run.
The initial sandboxed runtime attempts also hit LeakSanitizer's ptrace
restriction. The clean runtime red runs and green verification use permitted
execution outside that restriction. Xvfb was unreachable from the sandbox but
reachable through that permitted path.

Release compilation also caught a narrowing conversion in the new grid-test
helper, corrected by accepting a `size_t` column, and the new `palette` typedef
shadowing existing raster-test locals, corrected by naming the new type
`palette_state`. No raster-test changes or warning relaxations were made.
The preliminary `make -j3 all` attempts each exited with the compiler-failure
status (M)[AC]: `2`; their logs are `build/edit-ywd/all-first.log` and
`build/edit-ywd/all-shadow.log`. The initial typo build of
`build/san/tests/editor_test` also exited `2` (M)[AC].

The mutation case then caught a frame error in the first implementation:

```text
editor_test:63: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:3957: FAIL settle(e) == 0
exit=1
```

The debugger confirmed the editor error came from the post-mutation turn.
Its batch diagnostic command exited `0` (M)[AC]; leak checking was disabled
only for that debugger run, not for sanitizer verification. Its transcript is
`build/edit-ywd/mutation-debug.log`.
Closing the palette before scroll mutation bookkeeping resized a configuration
that was then replaced by the prior displayed configuration. Moving closure
after `editor_scroll_edit` preserves the restored geometry. Both focused editor
builds passed after this correction.

## Green runs and verification

All required checks passed. Compiler identities match the brief (P: executable
version metadata): GCC 13.3.0 and Clang 18.1.3. Repository C11 warning flags,
including `-Werror -Wshadow -Wconversion`, were retained.

Every sanitizer execution used `ASAN_OPTIONS=detect_leaks=1`. Display executions
used `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99` and the shared display lock;
the private runtime directory was removed by the runner's exit trap. Config/data
roots for the full runner were inside this worktree's `build/edit-ywd/`.
No display-lock timeout occurred, so the brief's longer retry was not needed.
Exit results below are (M)[AC]; numeric command settings come from the brief
(P: enabled leak flag, prescribed display and build parallelism).

| Verification command | Exit |
| --- | --- |
| `make -j3 all` | 0 |
| `make -j3 build/san/tests/palette_test build/san/tests/keys_test build/san/tests/config_test build/san/tests/config_find_test build/san/tests/config_editor_test build/san/tests/config_worker_test build/san/tests/editor_test` | 0 |
| `build/san/tests/palette_test` | 0 |
| `build/san/tests/keys_test` | 0 |
| `build/san/tests/config_test` | 0 |
| `sh tools/with_display_lock.sh build/san/tests/config_find_test` | 0 |
| `sh tools/with_display_lock.sh build/san/tests/config_editor_test` | 0 |
| `build/san/tests/config_worker_test` | 0 |
| `sh tools/with_display_lock.sh build/san/tests/editor_test --ywd-palette` | 0 |
| `sh tools/with_display_lock.sh build/tests/editor_test --ywd-palette` | 0 |
| `sh tools/with_display_lock.sh build/san/tests/editor_test` | 0 |
| `sh tools/with_display_lock.sh build/tests/editor_test` | 0 |
| `build/bench/palette_bench` | 0 |
| `build/bench/keys_bench` | 0 |

The final standalone build confirmations, after the mutation-order correction:

```text
$ make -j3 all
make: Nothing to be done for 'all'.
exit=0
$ make -j3 build/san/tests/palette_test build/san/tests/keys_test build/san/tests/config_test build/san/tests/config_find_test build/san/tests/config_editor_test build/san/tests/config_worker_test build/san/tests/editor_test
(all named targets up to date)
exit=0
```

Pure green run:

```text
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/palette_test
palette_test: labels, tightness/order, exclusions, live chords and allocation guard passed
exit=0
```

Focused editor green runs (power/load stamps copied from their logs):

```text
Not charging
 10:17:38 up 6 days, 15:05,  1 user,  load average: 48.65, 47.29, 46.13
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test --ywd-palette
edit-ywd palette: paint/filter/chords, shared Save As, Escape/mutation/focus and malloc guard passed
exit=0
```

```text
Not charging
 10:17:45 up 6 days, 15:05,  1 user,  load average: 48.52, 47.28, 46.14
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test --ywd-palette
edit-ywd palette: paint/filter/chords, shared Save As, Escape/mutation/focus and malloc guard passed
exit=0
```

The release focused case asserts zero allocations (P: the typing-path law)
over opening, arrow navigation, query input, paint and Escape. It also verifies
original text/grid restoration, bounded result scrolling, Save As prompt parity
with its chord, mutation/focus closure, Backspace and live chord overrides.

Full editor green runs:

```text
Not charging
 10:17:51 up 6 days, 15:05,  1 user,  load average: 48.07, 47.21, 46.12
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=ASan-inert (M)[AC]
editor_test: all passed
exit=0
```

```text
Not charging
 10:23:16 up 6 days, 15:11,  1 user,  load average: 49.18, 51.67, 48.65
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
editor_test: all passed
exit=0
```

Requested-GL cases used the existing raster fallback under Xvfb and passed.
The native release allocation guards were active. No real-display drive was
part of this bead.

Fuzz smoke build and run both exited successfully (M)[AC]:

```sh
clang --gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13 -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -Isrc fuzz/palette_fuzz.c src/palette/palette.c src/keys/keys.c -lxkbcommon -o build/edit-ywd/palette_fuzz
ASAN_OPTIONS=detect_leaks=1 build/edit-ywd/palette_fuzz -runs="$corpus_count" build/edit-ywd/palette-corpus
```

The corpus and requested run count derive from one seed per name in
`config/actions.inc` (P). This was a seed smoke test, not a sustained fuzz run.
The full log is `build/edit-ywd/fuzz.log`.

Tracked benchmarks, with their actual power/load stamps. Palette sample count
is one filter per eligible action (P); the keys stream uses its existing harness.
No latency budget was introduced. These measurements reflect the shared load:

The unmodified keys harness's `(G)` label on zero allocations refers to the
required typing-path law (P), not an added timing budget.

```text
Not charging
 10:26:19 up 6 days, 15:14,  1 user,  load average: 49.05, 50.70, 48.81
POWER status=Not charging [AC]; load stamped by invoking shell
palette filter: n=149 p50=15364 ns p99=2538267 ns (M)[AC]; TRACK, no latency limit
exit=0
```

```text
Not charging
 10:26:19 up 6 days, 15:14,  1 user,  load average: 49.05, 50.70, 48.81
POWER status=Not charging [AC] load1=49.05; keys measurements TRACK only
BENCH keys_lookup TRACK n=1000000 p50=0.118 us p99=0.257 us (M)[AC] load1=49.05 no limit (no derivation for 1 us)
STREAM (M)[AC] load1=49.05 matches=436904 prefixes=250596 cancelled=125596 misses=186904 mallocs=0 guard=active gate_mallocs=0(G)
exit=0
```

Disk snapshots (M)[AC], from `df -h /` immediately before and after the full
editor suites. This bead introduced no GB-scale fixture; its new editor fixture
is the literal test buffer.

Before:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
```

After:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  388G   68G  86% /
```

Raw commands/results are retained in `build/edit-ywd/verify.sh`,
`build/edit-ywd/exits.log`, `build/edit-ywd/all-final.log`,
`build/edit-ywd/san-final-build.log` and the per-suite logs.

## Scope and remaining work

Mouse dispatch is omitted because the find panel has no row-click handler to
copy, as the brief permits. The menu bar, Goto Anything/file palette and new
actions are outside this bead. No work remains within this bead. No out-of-scope
bugs were found during the scoped verification, and no out-of-scope source
changes were made.
