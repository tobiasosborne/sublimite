# edit-a2g session 9 worker report

Bead remains **incomplete**. This worktree contains independent fixes and explicit remaining reproductions; the coordinator must not close the whole bead on this report. Git was read-only, bd was not run, and HANDOFF.md/docs/worklog were not touched. All added source changes are in the assigned large-file/paint/scroll paths and their private/public declaration seams. No new globals. No window was opened on display :0; all display commands used DISPLAY=:99 EDIT_DISPLAY=:99.

## Per-finding outcome

| P4-3 finding | Outcome | Remaining |
|---|---|---|
| §1 cancelled find lifetime | Fixed: unbind/cancel, separate active/retiring argument storage, physical completion before release; held COPY change → Keep → restart regression. | Retirement capacity can refuse another restart; teardown still physically waits, owned by the other lifecycle worker. |
| §15 Keep index/warm restart | Fixed: incomplete kept index is dirty; warm storage waits for retirement, gets a fresh snapshot and retries through maintenance. | Regression holds queued warm/index work; a separately held running warm callback schedule is not added. |
| §17 CLI coordinates | Partial: COPY off-prefix coordinates and misleading mapped density resolve through owned worker target seek; EOF alone clamps intent. | Existing synchronous request setup remains; editor_jump_line and fully deferred navigation in editor.c are untouched. |
| §18 byte jump proof/boundaries | Partial, correctness repaired: certified logical starts only; bounded nearby repair or owned worker seek; UTF-8, combining and CRLF interiors normalize. | Deep explicit helper waits for certification rather than publishing a pending approximate viewport. Worker column/horizontal-scroll metadata and wrapped target following remain absent. |
| §19 estimated newline underflow | Unfixed; opt-in red deletion regression retained. | Requires exactness-aware mutation accounting in input.c and layout; these are outside the supplied production file list. Undo coverage is also missing. |
| §20 cold paint/wrapped follow | Partial: unwrapped visibility uses prepared row ranges; deep cold bracket fault census is green. | Wrapped finish_edit/follow invokes global queries in input.c/layout and is untouched. |
| §21 lazy row repair/warm repaint | Partial: warm completion requests an idle correcting frame. | Lazy initial long-line first viewport still bypasses row repair in editor.c; opt-in red row regression retained. |
| §28 bulk monopolization | Fixed: warm/find progress and sealed publication yield with work_continue; literal windows retain dense/cross-boundary/nonoverlap semantics. | Lane regression uses FIFO markers; separate production identity/save/durability jobs are not individually held/tested. Loaded G6 timing remains unqualified. |
| §29 findui rescans | Unfixed; sparse-source window red regression retained. | Bounded/spooled match plan, replacement-page scan-work oracle and yielding matcher stages remain. |
| §31 resident mapped faults | Fixed for explicitly bound detected backing faults: retained token, checks around copy/publication/adoption/use; viewport preserved. | Faultable hosts must call scroll_resident_bind_snapshot. Generic unbound sources must be nonfaultable. No new identity I/O polling or editor integration. |

## Decisions and scope

See [the decision record](../decisions/edit-a2g.md). Allocation stays in explicit setup/admission or the existing prepaid snapshot recycler, outside typing. Source choice changes only the large-file index/warm state and does not touch journal reconciliation. No save/journal/startup or tab/eviction hunk was changed.

The supplied file list excludes input.c, editor.c and layout even though several review fixes require those seams. Those parts are reported rather than fixed. Declaration additions in editor private headers and scroll.h are the minimum seams for the assigned implementations. The explicit coordinate/jump wait is a compatibility choice and a latency limitation, not an asynchronous-navigation claim.

## Red and green evidence

Functional observations below are (M)[AC], BAT0 Not charging, on the shared loaded box. Bounds/constants are (G). No timing printed by a benchmark establishes an acceptance verdict.

Commands used DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0. The implemented findings have failing baseline regressions and green runs. Except for the corrected resident fixture noted below, the live red run preceded its fix. The commands below select individual schedules in build/san/tests/editor_large_test; scroll uses build/san/tests/scroll_test and findui uses build/san/tests/findui_test. Ordinary suites include the green cases. Deliberately remaining reproductions are opt-in.

### §1 — P4_FIND_RESTART=1

RED:

```text
=================================================================
==3==ERROR: AddressSanitizer: heap-use-after-free on address 0x52d000000400 at pc 0x64785e46b3a9 bp 0x777486ffebb0 sp 0x777486ffeba8
READ of size 8 at 0x52d000000400 thread T1
    #0 0x64785e46b3a8 in held_find_argument tests/editor_large_test.c:476:5
    #1 0x64785e6f8c2d in worker_main src/work/work.c:82:13
    #2 0x64785e417adc in asan_thread_start(void*) asan_interceptors.cpp.o
    #3 0x777489a9cb83 in start_thread nptl/pthread_create.c:447:8
    #4 0x777489b29ecb in clone3 misc/../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

0x52d000000400 is located 0 bytes inside of 33072-byte region [0x52d000000400,0x52d000008530)
freed by thread T0 here:
    #0 0x64785e419d5a in free (build/san/tests/editor_large_test+0x261d5a) (BuildId: 7dcb558fac6c34fe3333c1dd2cd1c84b1bb53470)
    #1 0x64785e4b0bf0 in editor_large_find_begin src/editor/large.c:271:9
    #2 0x64785e466756 in cancelled_find_restart tests/editor_large_test.c:505:14
    #3 0x64785e458e6a in main tests/editor_large_test.c
    #4 0x777489a2a1c9 in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
    #5 0x777489a2a28a in __libc_start_main csu/../csu/libc-start.c:360:3
    #6 0x64785e37f1a4 in _start (build/san/tests/editor_large_test+0x1c71a4) (BuildId: 7dcb558fac6c34fe3333c1dd2cd1c84b1bb53470)

previously allocated by thread T0 here:
```

GREEN:

```text
P4-3 section 1: held cancelled find survives Keep and restart passed
```

### §15 — P4_KEEP_BACKGROUND=1

RED:

```text
editor_large_test:548: FAIL dirty
```

GREEN:

```text
P4-3 section 15: Keep restarts incomplete index and cancelled warm lease passed
```

### §17 — P4_COORDINATES=1

RED:

```text
editor_large_test:574: FAIL editor_open_request(e, &request, 0) == 0
```

GREEN:

```text
P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed
```

### §18 — P4_BYTE_JUMP=1

RED:

```text
editor_large_test:598: FAIL editor_view(e).first_byte == 0
```

GREEN:

```text
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
```

### §19 — P4_ESTIMATED_DELETE=1 (still red)

RED:

```text
editor_large_test:671: FAIL editor_line_count(e) >= 1 && editor_line_count(e) <= editor_length(e) + 1u
P4-3 section 19: post-delete estimated lines=18446744073709551517 bytes=100 (M)[AC]
```

No green claimed; this finding remains open.

### §20 — P4_COLD_PAINT=1

RED:

```text
editor_large_test:650: FAIL faults < 128
P4-3 section 20: cold deep bracket paint minor faults=1536 (M)[AC], loose bound=128 (G)
```

GREEN:

```text
P4-3 section 20: cold deep bracket paint minor faults=2 (M)[AC], loose bound=128 (G)
```

### §21 — P4_WARM_REPAINT=1

RED:

```text
editor_large_test:692: FAIL e->full_pending
```

GREEN:

```text
P4-3 section 21: warm completion requests an idle correcting frame passed
```

### §21 — P4_LAZY_LONG_LINE=1 (still red)

RED:

```text
editor_large_test:708: FAIL e->row_byte[1] == n + 1u
```

No green claimed; this finding remains open.

### §28 — P4_COOPERATIVE_JOBS=1

RED:

```text
editor_large_test:748: FAIL atomic_load(&p.observed)
```

GREEN:

```text
P4-3 section 28: production find/warm yield bulk lane before completion passed
```

### §28 — P4_COOPERATIVE_JOBS=1 P4_WARM_LANE=1

RED:

```text
editor_large_test:748: FAIL atomic_load(&p.observed)
```

GREEN:

```text
P4-3 section 28: production find/warm yield bulk lane before completion passed
```

### §29 — P4_SPARSE_WINDOW=1 (still red)

RED:

```text
P4-3 section 29: sparse window new source-search stages=1 (M)[AC]
tests/findui_test.c:786: assertion failed: after == before
```

No green claimed; this finding remains open.

### §31 — P4_RESIDENT_FAULT=1

RED:

```text
scroll_test:955: FAIL r.windows[0].error == SCROLL_ERR_SOURCE
```

GREEN:

```text
P4-3 section 31: real mapped truncation during copy preserves viewport passed
P4-3 section 31: real mapped truncation after publication preserves viewport passed
```

The resident regression initially exposed fixture mistakes (NULL allocator and missing index), which were corrected. The accepted red above rebuilds the original resident.c from read-only git show against the extended header, with only a no-op binding shim, and runs the corrected real mapped-truncation fixture. It reproduces READY publication with error zero. The green binds the snapshot and also checks detected faults after publication. The corrected resident negative control was captured after the initial implementation because the original fixture errors prevented a valid defect red run; this is a strict red-first chronology limitation and is not hidden as acceptance. Both baseline sources and negative binaries remain under ignored build/negative; none is production.

### Supplemental §17 target viewport and §28 sealed publication

The coordinate test also proved that a correctly sought target could still be hidden by an underestimated line total. The bounded already resident target sample now establishes a line-count lower bound, including following prepared rows; it remains estimated until the full index publishes. This refinement had a live red before its fix.

RED (M)[AC]:

```text
editor_large_test:577: FAIL e->buffer->lines > editor_view(e).first_line
P4-3 section 28: production find/warm yield bulk lane before completion passed
P4-3 section 21: warm completion requests an idle correcting frame passed
P4-3 section 20: cold deep bracket paint minor faults=2 (M)[AC], loose bound=128 (G)
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
```

GREEN (M)[AC]:

```text
P4-3 section 28: production find/warm yield bulk lane before completion passed
P4-3 section 21: warm completion requests an idle correcting frame passed
P4-3 section 20: cold deep bracket paint minor faults=2 (M)[AC], loose bound=128 (G)
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed
```

A supplemental sealed-phase test enqueues a FIFO marker only after snapshot release, while leaving the mailbox saturated. Its preserved pre-yield baseline times out; the final implementation yields. This negative control was added after the core live red/green lane test.

RED (M)[AC]:

```text
editor_large_test:396: FAIL trace_now_ns() < deadline
editor_large_test:773: FAIL mailbox_pressure(e, (const uint8_t *)"NEEDLE") == 0
```

GREEN (M)[AC]:

```text
P4-3 section 28: sealed find publication yields behind a saturated mailbox passed
```

## Validation status

Initial GCC 13 make all exited zero (M)[AC]. Initial sandbox make check exited nonzero at CLI Unix-socket initialization, not a sanitizer assertion. Full check was restarted with sandbox escalation for local IPC and Xvfb :99 access; LeakSanitizer is disabled because it cannot run inside this sandbox. The coordinator must rerun with leak detection enabled.

Final GCC 13 `make -j4 all` exited zero (M)[AC], with the repository C11 strict warning flags (`-Wall -Wextra -Werror -Wshadow -Wconversion`). Final large-file sanitizer and release suites exited zero (M)[AC], including the saturated sealed publisher and target-viewport lower-bound cases. The release large-file suite's active counting guard remains zero (M)[AC] against zero (G). Full scroll sanitizer suite exited zero (M)[AC]. `git diff --check` is clean.

Final editor fuzz (M)[AC], 60 seconds requested (G), exit zero:

```text
Done 26428 runs in 61 second(s)
```

Final scroll fuzz (M)[AC], 60 seconds requested (G), exit zero:

```text
Done 844 runs in 62 second(s)
```

Final large-file sanitizer suite (M)[AC], exit zero, selected output:

```text
P4-3 section 28: sealed find publication yields behind a saturated mailbox passed
P4-3 section 28: production find/warm yield bulk lane before completion passed
P4-3 section 21: warm completion requests an idle correcting frame passed
P4-3 section 20: cold deep bracket paint minor faults=2 (M)[AC], loose bound=128 (G)
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed
P4-3 section 15: Keep restarts incomplete index and cancelled warm lease passed
P4-3 section 1: held cancelled find survives Keep and restart passed
editor_large_test: ok
```

Scope additions stopped before the requested cutoff. At the wall-clock handoff limit, neither full serial `make check` invocation had returned. The final-source invocation had passed through editor/findui/file/font/GL/IPC/journal/lineidx/piece and was waiting for the shared Xvfb :99 lane at raster. The earlier invocation had passed raster and reached X11 identity, also waiting for that lane. No shared display lock was bypassed or interrupted.

**Full `make check` exit zero is not observed; full-suite acceptance is incomplete.** The initial sandbox invocation exited nonzero at denied CLI socket initialization. Escalated invocations continued making progress without a reported sanitizer failure, but queued display checks consumed the remaining budget. Their logs are in ignored `build/worker-logs/make-check-final.log` and `make-check-unsandboxed.log`. The coordinator must inspect any later completion and rerun the full suite with leaks enabled. This report makes no full-suite green claim.

## Loaded paired benchmark

The release --attribute-find row ran the preserved pre-yield implementation followed immediately by the changed implementation on the same loaded [AC] box. These are TRACK diagnostics; the benchmark's pass bits do not certify G1/G6 and its known endpoint/population issues remain outside this bead.

First paired window implementation, all values (M)[AC], nanoseconds; BAT0 Not charging, load1 3.14 before both variants:

| Row | Before p50 / p99 | Copied-window p50 / p99 | Reference (G) |
|---|---:|---:|---:|
| log_1g literal find | 118019663 / 119321283 | 162296938 / 162520928 | 80000000 / 125000000 |
| typing line 1000 while find active | 231391 / 245973 | 263004 / 285154 | 1000000 / 2000000 |

Both variants reported the same exact find total, 298542 (M)[AC], and zero typing allocations (M)[AC] against zero (G). The copy cost motivated borrowing only individually certified adjacent immutable spans in the final bounded window implementation; fragmented windows still use reserved scratch. Final adjacent-span window pair, all values (M)[AC], nanoseconds; BAT0 Not charging, load1 2.09 before both variants:

| Row | Preserved before p50 / p99 | Final p50 / p99 | Reference (G) |
|---|---:|---:|---:|
| log_1g literal find | 117960760 / 142846718 | 128840050 / 208709634 | 80000000 / 125000000 |
| typing line 1000 while find active | 225629 / 258906 | 229505 / 257315 | 1000000 / 2000000 |

The source find total remains 298542 (M)[AC], and typing allocation counts remain zero (M)[AC]. The final window implementation still has overhead relative to the pre-yield kernel and does not meet or certify G6. Different paired campaigns cannot isolate copy overhead as an exact causal number. No quiet-box run or single-timing gate verdict is claimed.

## Handoff requirements

Keep the bead open for estimated line-total mutation accounting, wrapped follow, lazy first-viewport row repair, fully deferred coordinate/navigation integration and findui bounded/spooled retrieval. The coordinator must reconcile narrow declaration/lifecycle changes with the other editor workers, rerun leak-enabled checks, and decide how to split the remaining work. No out-of-scope issue was repaired.
