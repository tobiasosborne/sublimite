# edit-cxho.6 — session s24 worker report

Implemented editor file-drop opening in the assigned worktree. No `bd` or
`git` commands were run. No installed build/profile or other checkout was
changed. The brief's retained-find rule takes precedence over the older bead
text's find-close instruction.

Files touched:

- `src/editor/editor.c`: native drop accessor, reusable parser arena, hostname,
  stat/directory filtering, shared open requests, nonfatal combined notices,
  overlay/capture reset, and find-source retargeting.
- `src/editor/private.h`: arena, bounded notice text, and borrowed headless
  payload fields for injected drop events.
- `src/editor/open.c`: release the drop arena on teardown, including failed open.
- `tests/editor_test.c`: injected headless drop contracts and focused selectors;
  existing typing/allocator guards unchanged.
- `docs/decisions/edit-cxho.6.md`: scope decisions and numeric derivations.
- `docs/worker-reports/edit-cxho.6-s24.md`: this report.

The parser retains 128 paths (P: `IPC_MAX_PATHS`); the requested overflow case
contains 130 paths (P: that limit plus the brief's explicit pair). Overflow is
noticed. Transaction failures retry individual retained paths so capacity or a
failed file does not discard accepted opens or prevent a later existing tab
from being selected. Directories and directory symlinks are ignored with a
folder notice. Empty/fully rejected input cannot create an untitled tab.

Numeric values in copied outputs are observations (M)[AC], with power/load
stamps alongside timing output; exit statuses, source line locations and
identifiers are structural (P). No new duration/count/allocation budget was
introduced. Each fixture is a tiny literal file; no new GB fixture was added.

## Red run

Before implementing the handler, the tests and private headless payload
scaffolding compiled with `make -j4 build/san/tests/editor_test` (exit 0).
Each selector used:

```sh
run_dir=$(mktemp -d)
export XDG_RUNTIME_DIR="$run_dir" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1
cat /sys/class/power_supply/BAT0/status
uptime
sh tools/with_display_lock.sh build/san/tests/editor_test <selector>
# Record exit status, then remove run_dir.
```

Actual assertion failures and per-selector exit statuses:

```text
Not charging
 11:54:09 up 1 day, 13:57,  1 user,  load average: 1.53, 1.29, 1.40
COMMAND: build/san/tests/editor_test --drop-pair
editor_test:6905: FAIL tabs_count(&e->tabs) == f.count && tabs_active_index(&e->tabs) == f.count - 1u
exit=1
COMMAND: build/san/tests/editor_test --drop-directory
editor_test:6928: FAIL e->recovery_notice && strstr(e->recovery_notice, "folder")
exit=1
COMMAND: build/san/tests/editor_test --drop-overflow
editor_test:6938: FAIL tabs_count(&e->tabs) == IPC_MAX_PATHS && tabs_active_index(&e->tabs) == IPC_MAX_PATHS - 1u
exit=1
COMMAND: build/san/tests/editor_test --drop-palette
editor_test:69: FAIL false
editor_test:6951: FAIL drop_inject(e, f.uris, f.size) == 0
exit=1
COMMAND: build/san/tests/editor_test --drop-find
editor_test:6971: FAIL state.complete && state.match_count == sizeof "n" - 1u
exit=1
COMMAND: build/san/tests/editor_test --drop-failure
Not charging
 11:54:59 up 1 day, 13:58,  1 user,  load average: 1.05, 1.18, 1.36
editor_test:6980: FAIL tabs_count(&e->tabs) == sizeof "a" - 1u && !strcmp(e->buffer->path, f.paths[0])
exit=1
```

LeakSanitizer also reported that thread inspection under the execution
sandbox/ptrace was unsupported. These failures reached the assertions before
that diagnostic. Subsequent sanitizer verification ran outside that sandbox
with leak detection enabled. The sandbox also hid the display socket;
verification uses only the assigned display `:99`.

## Green run

Sanitizer targets were rebuilt via the Makefile's existing check-style binary
rules (`make -j4 all build/san/tests/editor_test build/san/tests/dnd_test`,
exit 0). The same focused selectors ran outside the sandbox, using the
same wrapper and `ASAN_OPTIONS=detect_leaks=1`:

```text
Not charging
 11:55:56 up 1 day, 13:59,  1 user,  load average: 2.68, 1.69, 1.52
COMMAND: build/san/tests/editor_test --drop-pair
edit-cxho.6 drop: pair opens last active; duplicate retains tab/text/view passed
exit=0
COMMAND: build/san/tests/editor_test --drop-directory
edit-cxho.6 drop: directory skipped with notice; next drop opens passed
exit=0
COMMAND: build/san/tests/editor_test --drop-overflow
edit-cxho.6 drop: IPC prefix opened and overflow noticed passed
exit=0
COMMAND: build/san/tests/editor_test --drop-palette
edit-cxho.6 drop: palette/menu and pointer captures reset passed
exit=0
COMMAND: build/san/tests/editor_test --drop-find
edit-cxho.6 drop: find panel/query/focus retained and source retargeted passed
exit=0
COMMAND: build/san/tests/editor_test --drop-failure
edit-cxho.6 drop: capacity/memory/stat/parse failures recover; arena reused passed
exit=0

```

The failure contract covers capacity refusal followed by typing, parser arena
exhaustion, rejected remote URI, missing-path stat failure alongside a folder
and good files, ignored comment/empty input, and reuse of the arena.

## Verification

- `make -j4 all`: exit 0, gcc with the required C11 warning flags and `-Werror`.
- `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dnd_test`: exit 0;
  `dnd_test: all parser cases passed`.
- Complete sanitizer editor suite: exit 0, `XDG_RUNTIME_DIR=$(mktemp -d)
  DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 EDIT_REFUSE_REAL_DISPLAY=1
  sh tools/with_display_lock.sh build/san/tests/editor_test`.
  `editor_test: all passed`.
- Full `make -j4 check`: exit 0, with fresh `XDG_RUNTIME_DIR`, `DISPLAY=:99`,
  `ASAN_OPTIONS=detect_leaks=1` and `EDIT_REFUSE_REAL_DISPLAY=1`. The Makefile
  uses `tools/with_display_lock.sh` for its display suites. This includes the
  requested editor, scroll, large-file, session, hot-exit, CLI and raster suites.
  Summary: `check: 112 test binaries passed` (M)[AC], counted by the Makefile
  from its sanitizer test list; the appended X11 identity check also passed.
- Existing G1 benchmark: exit 0, `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99
  EDIT_REFUSE_REAL_DISPLAY=1 sh tools/with_display_lock.sh
  build/bench/editor_bench $(cat bench/editor_bench.args)`; uses the checked-in
  arguments, with no benchmark/guard changes. This is TRACK output, not a
  latency certification; raster misses are recorded below.
- Existing release malloc guard: both backend runs exit 0, `EDITOR_ALLOC_ONLY=null`
  and `EDITOR_ALLOC_ONLY=raster` with fresh `XDG_RUNTIME_DIR`, `DISPLAY=:99`,
  `EDIT_REFUSE_REAL_DISPLAY=1`, and `sh tools/with_display_lock.sh
  build/tests/editor_test`. Guard is active, unlike the sanitizer binary.
- Source hash comparison of the preexisting script/allocation test section and
  `src/base/malloc_guard.c`: exit 0, unchanged. `bench/editor_bench.c` and its
  arguments were not edited.


Complete editor sanitizer evidence (observations (M)[AC]):

```text
Not charging
 11:56:57 up 1 day, 14:00,  1 user,  load average: 2.66, 1.89, 1.61
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  152G  1.6T   9% /
edit-ci6i: wrapped seek landed top 0 (from 1014, target 0)
edit-cxho.6 drop: pair opens last active; duplicate retains tab/text/view passed
edit-cxho.6 drop: directory skipped with notice; next drop opens passed
edit-cxho.6 drop: IPC prefix opened and overflow noticed passed
edit-cxho.6 drop: palette/menu and pointer captures reset passed
edit-cxho.6 drop: find panel/query/focus retained and source retargeted passed
edit-cxho.6 drop: capacity/memory/stat/parse failures recover; arena reused passed
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=ASan-inert
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=ASan-inert
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: null hl-on 10000 keys mallocs=0 guard=ASan-inert
editor_test: raster 10000 keys mallocs=0 guard=ASan-inert
editor_test: all passed
editor_test exit=0
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  154G  1.6T   9% /
```

Release malloc guard evidence (counts derive from the unchanged guard's
explicit key loop; zero allocations is the binding typing rule (P)):

```text
Not charging
 12:00:18 up 1 day, 14:03,  1 user,  load average: 1.02, 1.53, 1.52
COMMAND: EDITOR_ALLOC_ONLY=null build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
COMMAND: EDITOR_ALLOC_ONLY=raster build/tests/editor_test
editor_test: raster 10000 keys mallocs=0 guard=active
exit=0

```

Tracked G1 evidence (M)[AC]; timing is `TRACE_T4_PRESENT_SUBMITTED` minus
input ingress, nearest-rank percentiles from the unchanged benchmark. Sample
count comes from `bench/editor_bench.args`; limits come from its existing G1
constants and the perf document. No performance claim beyond these observations.
The benchmark read the preexisting corpus; this worker added no large fixture.

```text
Not charging
 12:00:03 up 1 day, 14:03,  1 user,  load average: 0.85, 1.53, 1.52
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  154G  1.6T   9% /
STAMP (M)[AC] BAT0=Not charging load1=0.85 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
BENCH name=editor_null_serial_injection_T4_G1 n=64 required_n=10000 p50=752383 p99=798198 ci95_p50=[750945,754387] ci95_p99=[785058,798198] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=0.85 verdict=TRACK
G1 null workload=serial keys=64 completed=64 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=0.85 shared box
BACKEND requested=raster actual=cpu-raster init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
BENCH name=editor_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=14280118 p99=19233705 ci95_p50=[13970140,14789101] ci95_p99=[18570222,19233705] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=0.85 verdict=TRACK
G1 raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=0.94 shared box
BACKEND requested=gl actual=cpu-raster init_error=-10 TARGET A=2880x1800 font_px=30 (M)[AC]
BENCH name=editor_gl_fallback_raster_serial_injection_T4_G1 n=64 required_n=10000 p50=14907767 p99=18384875 ci95_p50=[14231960,15186274] ci95_p99=[17857676,18384875] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=0.94 verdict=TRACK
G1 gl_fallback_raster workload=serial keys=64 completed=64 arrivals_active=64 coalesced=0 foreign_input=0 allocations=0 mutations=64 journal=64 structural=OK mode=TRACK (M)[AC]
editor_bench exit=0
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  154G  1.6T   9% /
```

Full logs are saved in `build/edit-cxho.6-evidence/` (build artifacts).


Full-check command and actual summary (exit 0):

```sh
run_dir=$(mktemp -d)
export XDG_RUNTIME_DIR="$run_dir" DISPLAY=:99
export ASAN_OPTIONS=detect_leaks=1 EDIT_REFUSE_REAL_DISPLAY=1
cat /sys/class/power_supply/BAT0/status
uptime
df -h /
make -j4 check
# Record exit status, df -h /, then remove run_dir.
```

```text
Not charging
 11:57:58 up 1 day, 14:01,  1 user,  load average: 1.21, 1.62, 1.53
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  152G  1.6T   9% /
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/base_test.c -o build/san/tests/base_test.o
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME, WM_NAME and _GTK_THEME_VARIANT readback passed on :99
check: 112 test binaries passed
x11_identity_test: display lock timeout: display :99 (held by 492329)
make check exit=0
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  155G  1.6T   9% /
```

The standalone parser suite's actual output:

```text
Not charging
 11:56:38 up 1 day, 13:59,  1 user,  load average: 2.94, 1.90, 1.61
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  152G  1.6T   9% /
dnd_test: all parser cases passed

```

## Manual Nemo recipe (not performed here)

Use the coordinator's test window for this worktree and disposable text files;
this worker did not launch the editor on the owner's desktop or use the
installed build/profile. From Nemo:

- Drag a text file into the window; its tab opens and becomes active.
- Select a pair of files and drag them together; both tabs open, the last
  listed path becomes active.
- Drag a folder; it creates no tab and the status row says folders are ignored.
- Drag an already-open file; its existing tab becomes active without a duplicate,
  preserving any unsaved text and cursor. Repeat with the palette or Find open:
  the palette closes; Find retains its query and follows the active file.

## Remaining / outside scope

No implementation or requested automated verification remains. Manual Nemo
validation remains as described above. Outside this bead: the requested GL backend
fell back to raster on this display; the unchanged tracked G1 raster and GL
fallback rows exceed their existing targets. No related fixes were attempted.
Live Nemo's actual offers/window-manager focus behavior remain a manual check. The
platform API has no bring-forward/focus operation, so none was added. Wayland
and Windows native drop producers remain in their separate beads. No other
code was changed.
