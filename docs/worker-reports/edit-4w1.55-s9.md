# edit-4w1.55 — worker report, session 9

Scope: P1-1 §§2 (detected mapping faults) and 3 (cancelled readiness delivery).
Both fixes are implemented. Final validation status is recorded below; this
report is self-contained. Design: [edit-4w1.55](../decisions/edit-4w1.55.md).

## Section 2 — detected mapping faults

Tests were written and run before the functional fix. The only production
addition before red was `file_test_recover`, an internal test seam invoking the
existing production SIGBUS recovery function without changing recovery logic.
The tests take retained file snapshots, inject recovery, and reject successful
find results and exact line-index publication. The baseline failed these
semantic assertions. Binding setup was then added with the new index API; the
fault/error/exactness oracles were preserved.

Done: retain a backing validity/lifetime token independently of the file UI
object; check it in find polling, panel publication/adoption/result use, index
worker scanning/publication, UI adoption and queries. Cancel affected leases
on UI detection and clear/mask already adopted metadata. Mapping ownership is
preserved until physical work completion and final token release. No mapping
fault can be revived by keep or an index restart.

Coverage includes faults during index scanning; during an incremental whole-word
find; after computation before mailbox adoption; after successful adoption;
with snapshots outliving file/tree; and with the index token as the last owner
after file/tree/snapshot destruction. Async seek is checked during scanning,
before receipt and after adoption. Literal count/next and regex count clear
faulted results. Panel exact count, caches, visible matches and navigation are
unusable after recovery. The real process-wide service performs every injection.

Red command (exit 1, (M)[AC]):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 FT_CASE=p1-fault TMPDIR="$PWD/build/tmp" build/tests/file_test
```

```text
tests/file_test.c:682: FAIL !lineidx_complete(index)
tests/file_test.c:683: FAIL !lineidx_line_count(index).exact
tests/file_test.c:684: FAIL !lineidx_byte_to_line(index, &src, src.len).exact
tests/file_test.c:685: FAIL !lineidx_seek_line(index, &src, 1, LINEIDX_CHUNK).exact
tests/file_test.c:689: FAIL find_literal(&fs, (const uint8_t *)"x", 1, NULL, &result) == FIND_CANCELLED
tests/file_test.c:682: FAIL !lineidx_complete(index)
tests/file_test.c:683: FAIL !lineidx_line_count(index).exact
tests/file_test.c:684: FAIL !lineidx_byte_to_line(index, &src, src.len).exact
tests/file_test.c:689: FAIL find_literal(&fs, (const uint8_t *)"x", 1, NULL, &result) == FIND_CANCELLED
tests/file_test.c:682: FAIL !lineidx_complete(index)
tests/file_test.c:683: FAIL !lineidx_line_count(index).exact
tests/file_test.c:684: FAIL !lineidx_byte_to_line(index, &src, src.len).exact
tests/file_test.c:689: FAIL find_literal(&fs, (const uint8_t *)"x", 1, NULL, &result) == FIND_CANCELLED
tests/file_test.c:682: FAIL !lineidx_complete(index)
tests/file_test.c:683: FAIL !lineidx_line_count(index).exact
tests/file_test.c:684: FAIL !lineidx_byte_to_line(index, &src, src.len).exact
tests/file_test.c:689: FAIL find_literal(&fs, (const uint8_t *)"x", 1, NULL, &result) == FIND_CANCELLED
tests/file_test.c:720: FAIL !state.complete && !state.searching && state.match_count == 0
tests/file_test.c:722: FAIL state.search_error == FIND_CANCELLED
tests/file_test.c:720: FAIL !state.complete && !state.searching && state.match_count == 0
tests/file_test.c:721: FAIL state.cached_matches == 0 && state.visible_matches == 0
tests/file_test.c:722: FAIL state.search_error == FIND_CANCELLED
tests/file_test.c:720: FAIL !state.complete && !state.searching && state.match_count == 0
tests/file_test.c:721: FAIL state.cached_matches == 0 && state.visible_matches == 0
tests/file_test.c:722: FAIL state.search_error == FIND_CANCELLED
tests/file_test.c:720: FAIL !state.complete && !state.searching && state.match_count == 0
tests/file_test.c:721: FAIL state.cached_matches == 0 && state.visible_matches == 0
tests/file_test.c:722: FAIL state.search_error == FIND_CANCELLED
P1-1 section 2: FAIL
file_test: 28 FAILED
```

Green, same selection (release and Clang ASan/UBSan each exit 0, (M)[AC]):

```text
P1-1 section 2: ok
file_test: ok
```

The focused sanitizer run uses `ASAN_OPTIONS=detect_leaks=0`.

## Section 3 — cancellation-filtered readiness

The supplied main already stopped adopting worker output from readiness/status
getters; the review's original direct-getter bypass predates this tree. Preserve
that mailbox-only installation. A red-first deferred-decode test captures a
legitimate small-copy or mapping readiness message, cancels its lease before
installation, and calls decode. Baseline decode still installed the cancelled
record; the fix rechecks the recorded slot, epoch and generation before adoption.

Done: stale/cancelled decode returns without readiness installation. Close owns
uninstalled immutable records and releases them after physical completion.
Additional file_kill tests cover queued cancellation, pending small-copy prefix
and pending map readiness, getters before ordinary drain, and ordinary mailbox
filtering. These latter schedules confirm the preexisting getter fix.

Red command (exit 1, (M)[AC]):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 FT_CASE=p1-cancel TMPDIR="$PWD/build/tmp" build/tests/file_test
```

```text
tests/file_test.c:751: FAIL file_msg_decode(&delivery.message, &decoded) != 0
tests/file_test.c:752: FAIL !file_open_ready(f)
tests/file_test.c:753: FAIL file_attach(f, tree) == FILE_ERR_STATE
tests/file_test.c:751: FAIL file_msg_decode(&delivery.message, &decoded) != 0
tests/file_test.c:752: FAIL !file_open_ready(f)
tests/file_test.c:753: FAIL file_attach(f, tree) == FILE_ERR_STATE
P1-1 section 3: FAIL
file_test: 6 FAILED
```

Green, same selection (release and Clang ASan/UBSan each exit 0, (M)[AC]):

```text
P1-1 section 3: ok
file_test: ok
```

Additional release output (exit 0, (M)[AC]):

```text
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=active
file_kill_test: PASS visibility/cancellation; power-loss barriers unverified
```

## Required validation

All executions use DISPLAY=:99 and EDIT_DISPLAY=:99. No display 0 was used.
Release compiler: GCC 13.3.0 (M)[AC]; sanitizer/fuzz compiler: Clang 18.1.3
(M)[AC]. Make's normal C11 -Wall -Wextra -Werror -Wshadow -Wconversion flags
remain enabled. LSan cannot run in the sandbox: sanitizer/fuzz invocations use
`ASAN_OPTIONS=detect_leaks=0`; coordinator must rerun with leaks enabled.

- `make all`: exit 0 (M)[AC], including all release tests/benches/tools.
- Final `make check`: exit 0 (M)[AC], 59 sanitizer test binaries and replay CLI
  checks passed (M)[AC]. A private worktree runtime directory avoids concurrent
  CLI instance forwarding. Both the earlier full run and this final run passed.
- Full release file_test: exit 0 (M)[AC]. Release file_kill_test: exit 0 (M)[AC],
  including the active pooled fresh-save allocation guard.
- Focused sanitizer fault and cancelled-readiness selections: exit 0 (M)[AC].
- Final-library file fuzz: requested 60 s (G), completed 8410 runs in 61 s,
  exit 0 (M)[AC], no sanitizer finding; `-max_len=4096` (G).

Final full-suite command and pasted green output (exit 0, (M)[AC]):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
XDG_RUNTIME_DIR="$PWD/build/run" TMPDIR="$PWD/build/tmp" make -j3 check
```

```text
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

Final release build (exit 0, (M)[AC]):

```text
make: Nothing to be done for 'all'.
make all exit=0
```

Final-library fuzz command and pasted completion (exit 0, (M)[AC]):

```sh
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 \
build/fuzz/file_fuzz -max_total_time=60 -max_len=4096
```

```text
Done 8410 runs in 61 second(s)
```

SIGBUS symbol self-check: exit 0 (M)[AC]:

```text
finding10: ok mutable globals=1 file_bus
```

Typing allocation verification, release guards active (exit 0, (M)[AC]):

```text
lineidx typing: keys=10000 allocations=0 guard=active
lineidx_test: ok
editor_test: null 10000 keys mallocs=0 guard=active
```

Back-to-back module benchmarks passed correctness in TRACK mode. Loaded-box
measurements and their limitations are recorded in the decision file. Neither
single timings nor noisy gate labels are treated as acceptance verdicts.

Verification environment: sandbox local sockets initially prevented :99/IPC
access. The authorized socket-enabled suite works. A concurrent CLI invocation
caused a later rerun to forward to another editor and exit before its own
window; final verification uses a private XDG_RUNTIME_DIR in this worktree.
A transient Clang frontend crash during a concurrent build disappeared on
retry, with no source workaround. Neither observation caused an unrelated fix.

## Scope, decisions, and remaining work

Necessary publication/lifetime seams outside the named file implementation:
find core/panel, line index, editor index creation, and the tiny piece snapshot
lifetime-hook accessor. Piece mutation/storage algorithms are untouched. Three
private find tests only initialize the new meter field to NULL. No new globals,
no typing-path allocation, no git writes, no bd, no HANDOFF/worklog edits.

Missing acceptance: leak-enabled verification belongs to the coordinator.
Undetected backing changes and the last check/use race remain the documented
mapping limitation. Privately returned core-find results require the retained
snapshot's backing check before external adoption; the production panel checks
both worker publication and UI use. Cached piece-tree metadata/rebase and
unrelated review findings are outside this bead. Generic external lineidx sources must bind their
file snapshot before scanning; the production editor does so for both synchronous
and worker indexes. Decode requires the file object still alive; saved raw
notifications cannot be decoded after close. No cold/performance gate verdict,
full-source performance claim, or power-loss durability claim is made.

All design choices are in docs/decisions/edit-4w1.55.md. No scope was added after
the implementation/coverage was complete; remaining work was verification and
this report, within the worker wall-clock budget.
