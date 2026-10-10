# edit-5cv session 9 worker report

Scope: serialize shared-Xvfb test/tool ownership and diagnose display-lock
waits. Git was used only for read-only diffs/source inspection. No source
module, HANDOFF.md or worklog was changed; no `bd` was run. The bead names no
review-document sections, and there is no tooling STATUS.md. The existing
X11/editor status documents were read for context.

## Findings and implementation

**Concurrent display interference:** implemented. `make check` sends CLI,
editor, X11, reference-tool, GL and raster binaries through the common exclusive
display lock. Other module tests remain independent. The prewake display/idle scripts,
zygote launcher and optional-native GL script use the same lock. Existing
`refwin_pairs.py` users in `refwin_test` execute inside its inherited lock.

**Wait/timeout diagnostics and strict preflight:** implemented. Contention
prints `waiting for display :99 (held by <pid>)`; expiry names the command,
display and kernel flock owner. Direct `refwin_test` invocations acquire the
same lane. Inherited descriptors are checked against the lock inode to avoid
self-deadlock. The strict held-key check and all existing measurement/assertion
logic are unchanged. No new C globals or typing-path allocations were added.

The regression driver was written before implementation. Its original
concurrency run failed in every round; its initial contract failed because the
wrapper did not yet exist. A separately compiled original `refwin_test` also
fails the owner/timeout contract by continuing into its live tests while
another process owns the display lane. The final contract additionally verifies Makefile and
each updated script, nesting and exit-status propagation.

## Red evidence

Power was `Not charging` [AC]. Sampled load1 before the reproduction was 1.01
(M) [AC]. All display runs selected :99. The sandbox could not connect to the
existing server; the real reproduction below used approved execution outside
that isolation. The earlier connection-error attempt is not counted as the
concurrency reproduction.

Command before any lock implementation:

```sh
sh tools/test_display_lock.sh unlocked
```

Pasted round statuses and failure lines, exit 1 (M) [AC]; all five requested
rounds reproduced the issue:

```text
unlocked round 1: exits 1 0
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=refwin: window=0xffffffff keyboard is not idle: keycode=38 down (release keys before preflight)
unlocked round 2: exits 1 0
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=refwin: window=0xffffffff keyboard is not idle: keycode=38 down (release keys before preflight)
unlocked round 3: exits 1 0
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=keyinject: window=0xffffffff keyboard is not idle: keycode=38 down (release keys before preflight)
unlocked round 4: exits 1 0
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=keyinject: window=0x00400000 keyboard is not idle: keycode=38 down (release keys before preflight)
unlocked round 5: exits 1 0
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=keyinject: window=0x00400000 keyboard is not idle: keycode=38 down (release keys before preflight)
```

Initial contract command `sh tools/test_display_lock.sh contract`, exit 1:

```text
sh: 0: cannot open tools/with_display_lock.sh: No such file
```

The original source was retrieved with `git show HEAD:tests/refwin_test.c` and
compiled under the same strict clang sanitizer flags into
`build/edit-5cv-evidence/refwin-baseline`. The diagnostic-specific red command
was `sh tools/test_display_lock.sh contract build/edit-5cv-evidence/refwin-baseline`.
Pasted diagnostic red, exit 1 (M) [AC]:

```text
waiting for display :99 (held by 3273904)
true: display lock timeout: display :99 (held by 3273904)
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
display lock contract: FAIL refwin expected exit 1, actual 0
```

## Green evidence

Command `sh tools/test_display_lock.sh locked`, exit 0 (M) [AC]:

```text
locked round 1: exits 0 0
locked round 2: exits 0 0
locked round 3: exits 0 0
locked round 4: exits 0 0
locked round 5: exits 0 0
```

Command `sh tools/test_display_lock.sh direct`, exit 0 (M) [AC], with direct
binary calls rather than explicit wrapper calls:

```text
direct round 1: exits 0 0
direct round 2: exits 0 0
direct round 3: exits 0 0
direct round 4: exits 0 0
direct round 5: exits 0 0
```

Command `sh tools/test_display_lock.sh contract`, exit 0 (M) [AC]. Selected
pasted diagnostics and final assertion:

```text
waiting for display :99 (held by 7)
true: display lock timeout: display :99 (held by 7)
refwin_test: display lock timeout: display :99 (held by 7)
x11_identity_test: display lock timeout: display :99 (held by 7)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The nested Makefile invocation is deliberately required to fail on lock
timeout; the containing contract passes. Both prewake scripts, the zygote
script and the optional GL script produced the expected holder/timeout lines
under the same fixture. Successful prewake display and optional GL shell
smokes also exited 0 (M) [AC]:

```text
prewake display: explicit opt-in, matching displays, safe default PASS (no windows)
gl_review: native diagnostics SKIP: Xvfb/EGL context unavailable
```

The protocol fuzzer ran with `ASAN_OPTIONS=detect_leaks=0` and
`-max_total_time=60`; 60 seconds is the required duration (G). Pasted successful
output (M) [AC], exit 0:

```text
#48498818 DONE   cov: 43 ft: 43 corp: 12/649b lim: 4096 exec/s: 795062 rss: 473Mb
Done 48498818 runs in 61 second(s)
```

`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all build/fuzz/refproto_fuzz` exited 0
with GCC 13 release / clang 18 fuzz compilation (M) [AC]. All C builds use
`-Wall -Wextra -Werror -Wshadow -Wconversion`. No performance gate was judged
from these functional results. The shared box remained loaded; sampled load1
during validation included 4.43 and 2.20 (M) [AC].

The first normal ASan/UBSan check exited 0 (M) [AC]:

```text
check: 60 test binaries passed
test_replay_cli: all passed
```

The final Makefile also includes the expanded lock contract in `make check`.
Final command:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
```

Pasted final green, exit 0 (M) [AC]:

```text
check: 60 test binaries passed
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 3289185)
refwin_test: display lock timeout: display :99 (held by 3289185)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The displayed timeout is the intentional contention fixture, verified by the
passing contract. Final `DISPLAY=:99 EDIT_DISPLAY=:99 make all` exited 0:

```text
make: Nothing to be done for 'all'.
```

Shell syntax checks and `git diff --check` also passed. Final sampled power was
`Not charging` [AC], load1 5.82 (M) [AC]. LeakSanitizer was disabled because it
cannot run inside the sandbox; the coordinator must rerun with leaks enabled.

## Decisions and remaining work

Design details are recorded in `docs/decisions/edit-5cv.md`: selective locking,
persistent lock inode, inherited descriptor, kernel holder diagnostics and
configurable operational timeout. All actual window/input runs used :99;
nothing was opened on :0. The lock only coordinates cooperating callers;
other worktrees need this Makefile/helper change, and manual display tooling
should be launched through the wrapper. Standalone binaries other than
`refwin_test`, and the general `make bench`/CI bench phase, do not automatically
enter this selective check lane; use the common wrapper when running them
alongside display tests. Those general bench drivers were outside this bead.

The full zygote self-check is unavailable because existing experiment sources
do not compile against the current editor private structure. The attempted
command was `sh tools/zygote_bench.sh --self-check --launches 1`, exit 1.
Representative existing errors:

```text
variants/P4.14/a/open.c:29:10: error: ‘editor’ has no member named ‘index’
variants/P4.14/a/open.c:30:10: error: ‘editor’ has no member named ‘file’
```

These variant sources were not changed. The lock-timeout contract validates
zygote shell participation before its compiler step. The separate CLI IPC
collision belongs to edit-og2; no IPC implementation was changed.

Raw local logs are under `build/edit-5cv-evidence/`. The report contains no
secrets or external private paths. Required release build, sanitizer check,
five-round protected concurrency and protocol fuzz validation are complete.
Remaining coordinator work is the leak-enabled rerun; optional full zygote
execution awaits a separate repair of the stale experiment sources.
