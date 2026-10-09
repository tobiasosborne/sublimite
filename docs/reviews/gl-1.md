15 findings: **7 BLOCKER, 8 MAJOR, 0 MINOR**.

Read-only review completed. Clang syntax checks passed for `src/gl/gl.c`, `tests/gl_test.c`, and `bench/gl_bench.c` with the project warning flags. No files changed. I did not run builds, sanitizer tests, or display tests because they create files; the runtime arguments below come from source inspection and the recorded measurements.

## 1. BLOCKER — Context binding is cached per backend, although current context is per thread

Location: [src/gl/gl.c:370](/home/tobias/Projects/editor/src/gl/gl.c:370), [src/gl/gl.c:382](/home/tobias/Projects/editor/src/gl/gl.c:382).

Once `s->bound` becomes true, `gl_bind()` never checks which context is actually current. Another backend can replace the current context on the same UI thread. EGL permits only one current OpenGL/OpenGL ES context per thread. [EGL specification, §3.7](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf).

**Repro:** Initialize two GL backends for separate windows. Present A, then B, then A. A skips `eglMakeCurrent`; its GL commands execute in B’s context. Its swap can fail because A’s surface is not current. Readback can return B’s pixels, and shutting down A can delete B’s objects where numeric object names coincide. The frozen interface imposes no singleton restriction.

**Fix:** Verify the current context and surfaces before native operations, or rebind unconditionally at those boundaries. Apply the same rule during cleanup. Add an alternating two-backend render/readback/shutdown test.

## 2. BLOCKER — Worker initialization reads mutable UI window state

Location: [src/gl/gl.c:426](/home/tobias/Projects/editor/src/gl/gl.c:426), [src/gl/gl.c:444](/home/tobias/Projects/editor/src/gl/gl.c:444), [src/x11/x11.c:420](/home/tobias/Projects/editor/src/x11/x11.c:420).

The startup worker calls `gl_draw()` during its three-swap probe. That function reads `s->platform->height`, while the UI writes the same non-atomic field when processing ConfigureNotify.

**Repro:** Keep the UI processing platform events while GL initializes on its worker, then resize the window during the probe. The worker read and UI write have no synchronization or mailbox handoff: this is a C data race. The test driver hides it by suspending platform pumping throughout initialization.

**Fix:** Give initialization an immutable snapshot of the native handles, visual, and geometry. The worker must not read mutable `plat` fields. Use current platform geometry only after the UI receives the initialization result. Add a concurrent startup/resize test under TSan.

## 3. BLOCKER — Present failures can duplicate swaps, leak fences, and lose atlas uploads

Location: [src/gl/gl.c:559](/home/tobias/Projects/editor/src/gl/gl.c:559), [src/gl/gl.c:615](/home/tobias/Projects/editor/src/gl/gl.c:615).

`eglSwapBuffers()` succeeds before fence creation and GL error checking. A later failure returns `RENDER_ERR_DEVICE`, so the adapter leaves `presented=false`. Retrying calls swap again. A non-null fence retained from the failed attempt is overwritten without deletion.

Upload bookkeeping is also committed prematurely: dirty atlas rows are cleared without checking whether `glTexSubImage2D()` succeeded.

**Repro:** Script a successful swap followed by a fence failure or GL error, then retry the same frame. Two native swaps occur for one adapter frame. Separately, script a failed texture upload, followed by successful retry: the dirty flags are already clear, so the retry can report success while rendering stale glyph coverage.

**Fix:** Check upload/draw errors before swapping; retain dirty flags until upload success. Create and validate the render fence before the irreversible swap where possible. Track whether the native swap already happened, and make retries resume that phase without another swap. Treat unrecoverable context failures as terminal until shutdown. Test every failure boundary and fence deletion count.

## 4. BLOCKER — Skipped presentations are reported as displayed frames

Location: [src/gl/gl.c:632](/home/tobias/Projects/editor/src/gl/gl.c:632), [src/gl/gl.c:455](/home/tobias/Projects/editor/src/gl/gl.c:455).

Both startup verification and runtime verification accept any matching PIXMAP completion with nonzero MSC. Neither checks `ce->mode`.

**Repro:** Deliver a matching PIXMAP completion with `XCB_PRESENT_COMPLETE_MODE_SKIP`. The renderer accepts its MSC and emits T6, although the presentation was skipped. The protocol explicitly distinguishes this from a presentation that occurred. [Present protocol, §8](https://sources.debian.org/data/main/x/xorgproto/2025.1-1/presentproto.txt).

This can falsely acknowledge an edit and falsely pass cadence accounting.

**Fix:** Inspect completion mode in both paths. A skipped frame must not become a displayed-frame acknowledgement or cadence success. Handle it through an explicit failure/recovery policy, and test Skip alongside Copy, Flip, and supported SuboptimalCopy modes.

## 5. BLOCKER — An unrelated MSC notification can permanently poison a valid completion

Location: [src/gl/gl.c:669](/home/tobias/Projects/editor/src/gl/gl.c:669), [src/gl/gl.c:647](/home/tobias/Projects/editor/src/gl/gl.c:647).

The platform callback lacks Present kind, but `gl_present_complete()` unconditionally overwrites `notice_msc` for the pending serial. Private PIXMAP verification does not prevent this overwrite.

**Repro:**

1. The genuine PIXMAP completion verifies MSC 123, but the fence poll returns `GL_TIMEOUT_EXPIRED`.
2. A NotifyMSC completion using the same serial arrives with MSC 124.
3. Its platform callback overwrites `notice_msc`.
4. The fence subsequently signals.

T6 now requires `124 == 123`, which never becomes true. Even the driver’s continuous polling cannot release the frame. NotifyMSC and PIXMAP completions are distinct valid protocol events. [Present protocol](https://sources.debian.org/data/main/x/xorgproto/2025.1-1/presentproto.txt).

**Fix:** Use the private, typed PIXMAP event as the authoritative completion. Do not require mutable confirmation from the untyped platform callback. Preserve the matching event’s timestamp/MSC once accepted, and test unrelated notifications arriving before and after it.

## 6. BLOCKER — Completion progress depends on a busy-looping caller

Location: [src/gl/gl.c:625](/home/tobias/Projects/editor/src/gl/gl.c:625), [src/gl/gl.c:628](/home/tobias/Projects/editor/src/gl/gl.c:628), [src/gl/gl_driver.h:105](/home/tobias/Projects/editor/src/gl/gl_driver.h:105).

The renderer neither schedules subsequent fence polls nor rearms a wakeup after exhausting its eight-event polling budget. Production code publishes no `GL_POLL_MESSAGE`; the driver manufactures these messages continuously.

**Repro:** Queue nine stale private events followed by the matching completion. Deliver the matching platform callback once. Its private poll consumes eight stale events and returns without verification. Remaining events can already be buffered inside XCB, leaving no readable socket or scheduled wakeup. An event-driven caller sleeps with the backend permanently active. A fence that is still unsignalled during the sole callback has the same problem.

There is also no runtime completion deadline or reset-status handling; the three-second timeout exists only in the test/bench driver.

**Fix:** Provide a pending-frame wake/continuation mechanism, routed through the supported event/mailbox path, until both acknowledgements resolve. Add a bounded device-failure policy for missing completions and GPU resets. Test completion using an event-driven driver, without continuous `pump()` calls.

## 7. BLOCKER — ELF size inspection has an overflowing bounds check

Location: [bench/gl_bench.c:72](/home/tobias/Projects/editor/bench/gl_bench.c:72).

`sh[i].sh_name + 5u` is computed in 32-bit unsigned arithmetic before comparison with the string-table size. It can wrap and admit an out-of-bounds pointer.

**Repro:** Supply an otherwise accepted object header with a 32-byte section-name table and `sh_name=0xfffffffc`. The addition wraps to 1, so the condition passes; `memcmp(names + sh_name, ".text", 5)` reads approximately 4 GiB beyond the stack array. The arithmetic was independently checked during this review.

**Fix:** Widen the offset first and use subtraction-based bounds checking: validate `offset <= table_size` and `table_size - offset >= 5`. Reject malformed ELF class/encoding as well. Add a malformed-header regression test.

## 8. MAJOR — GL submissions have no descriptor-work budget

Location: [src/gl/gl.c:573](/home/tobias/Projects/editor/src/gl/gl.c:573), [src/render/render.c:295](/home/tobias/Projects/editor/src/render/render.c:295).

Every submit validates the entire glyph table and clears `glyph_seen` across `glyph_count`, irrespective of damage. Only the CPU backend receives the adapter’s fixed descriptor-work limit.

**Concrete argument:** A valid one-cell grid can carry one million glyph descriptors, all referencing the same valid one-pixel rectangle, with only slot zero used. Initialization permits that capacity. Every key then scans roughly 24 MB of unused descriptors and clears another 1 MB synchronously. A zero-damage frame still scans the table. There is no input check or time budget.

**Fix:** Establish and enforce a GL descriptor/pixel-work limit before scanning, or introduce a contract-approved preparation mechanism outside the typing slice. Include large unused tables and runtime-atlas workloads in regression coverage; the current 95-glyph benchmark cannot expose this scaling.

## 9. MAJOR — Battery and unknown-power runs cannot fail G3 or G3z

Location: [bench/gl_bench.c:124](/home/tobias/Projects/editor/bench/gl_bench.c:124), [bench/gl_bench.c:167](/home/tobias/Projects/editor/bench/gl_bench.c:167).

Any power tag other than `[AC]` disables both G3 time limits and the aggregate cadence failure. This includes battery operation and machines without `BAT0`. Yet perf §4 specifies battery operation for Target A.

**Repro:** On battery, provide 100 ms full-frame samples and thousands of missed refreshes. With successful native calls and complete sample storage, these rows report `pass=1` and do not make the process fail. The recorded EGL battery runs demonstrate this behavior with actual misses. Typing and after-idle rows also always receive zero limits.

**Fix:** Separate evidence tags from gate enforcement. Required conditions must enforce their gates. Make exploratory/unsupported-condition runs explicitly non-qualifying, rather than returning a shipping pass. Report a T4-based typing regression metric separately from the current T5 row.

## 10. MAJOR — The G3 verdict excludes required work

Location: [bench/gl_bench.c:31](/home/tobias/Projects/editor/bench/gl_bench.c:31), [bench/gl_bench.c:104](/home/tobias/Projects/editor/bench/gl_bench.c:104), [bench/gl_bench.c:127](/home/tobias/Projects/editor/bench/gl_bench.c:127).

The timestamp named `ingress` is taken after frame begin, damage marking, and strip computation. Scroll mutation also occurs outside it. Minimap rendering is absent; the printed 180,000 ns allowance is never added or deducted from the available budget.

**Repro:** A renderer-only p50 of 4.90 ms passes the 5.00 ms check. Adding the benchmark’s own stated minimap allowance gives 5.08 ms before omitted ingress work, exceeding G3.

**Fix:** Start timing at the scenario’s actual ingress and include the required minimap operation. Until then, compare against an explicitly reduced renderer budget and label the result as partial-operation evidence, without claiming G3 compliance.

## 11. MAJOR — Gate runs omit required sample counts and bulk-worker conditions

Location: [bench/gl_bench.c:114](/home/tobias/Projects/editor/bench/gl_bench.c:114), [bench/gl_bench.c:146](/home/tobias/Projects/editor/bench/gl_bench.c:146), [src/gl/gl_driver.h:77](/home/tobias/Projects/editor/src/gl/gl_driver.h:77).

Normal warm/typing rows take 2,000 samples, below perf §4’s 10,000 per interaction scenario. Quick mode can report `scroll_10k status=GATE pass=1` after only 100 frames because its completeness check compares against the reduced count.

The driver’s bulk worker is idle after initialization. No scenario runs with index/find/save active or all three queued, as required by §0.2.

**Concrete argument:** A regression that appears only during bulk memory traffic is absent from every measured scenario. Quick mode also cannot justify the specified 10,000-refresh cadence verdict even when its 100 frames pass.

**Fix:** Enforce the required sample count in qualifying runs; mark quick runs non-qualifying. Add controlled active-bulk and queued-three scenarios and verify that the intended work overlaps measurement.

## 12. MAJOR — Raw swap serials corrupt original-frame T6 tracing

Location: [src/x11/x11.c:490](/home/tobias/Projects/editor/src/x11/x11.c:490), [src/gl/gl.c:649](/home/tobias/Projects/editor/src/gl/gl.c:649), [tests/gl_test.c:102](/home/tobias/Projects/editor/tests/gl_test.c:102).

The platform records T6 under the native serial before invoking GL’s callback. GL then records another T6 under the original frame ID. Startup consumes three swap serials, so these namespaces immediately differ.

**Repro:** Editor frame 1 produces native serial 4. Its platform event records T6 for editor frame 4 before frame 4 is rendered. `trace_fmt_firsts()` selects the earliest T6 per ID; subsequent latency analysis can drop frame 4 as negative or associate it with the wrong presentation. The GL trace unit bypasses platform dispatch and therefore misses this.

**Fix:** Remove raw-serial renderer tracing from platform dispatch. Record T6 only after backend correlation to the original frame ID. Test complete platform-to-backend dispatch, including startup swaps, for exactly one correctly correlated T6 per frame.

## 13. MAJOR — Tests cannot detect broken native resize handling

Location: [tests/render_test.c:288](/home/tobias/Projects/editor/tests/render_test.c:288), [tests/gl_test.c:183](/home/tobias/Projects/editor/tests/gl_test.c:183), [tests/gl_test.c:369](/home/tobias/Projects/editor/tests/gl_test.c:369).

The frozen suite’s only successful resize requests the existing dimensions. GL pixel tests use fixed dimensions and read the retained FBO. Shutdown testing keeps the native window alive.

**Concrete argument:** Replacing `gl_resize()` with an unconditional successful no-op would pass the existing resize coverage. FBO readback also cannot detect incorrect placement or clipping in a resized EGL window. EGL permits resizing to be deferred until swap, making native-surface coverage necessary. [EGL specification, §3.10.1.1](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf).

**Fix:** Test actual grow/shrink transitions within reserved capacity, changed cell dimensions, fractional-cell margins, and native-window output. Add close/destroy-with-frame-pending coverage followed by cleanup/reinitialization. Keep an executable native conformance lane; `fuzz/render_fuzz.c` covers dirty-strip construction only.

## 14. MAJOR — Recorded real-display results miss the requested ship gates

Location: [docs/decisions/P2.4.md:10](/home/tobias/Projects/editor/docs/decisions/P2.4.md:10), [docs/decisions/P2.4.md:13](/home/tobias/Projects/editor/docs/decisions/P2.4.md:13).

The selected EGL renderer’s recorded `(M)[AC, loaded]` 15 px runs have p50/p99 of **5.55/14.3 ms** and **5.04/12.3 ms**, above G3’s **5.0/(T/2)** limits. Both 30 px p99 values also exceed G3. Cadence records **43 and 8 misses**, against G3z’s hard maximum of zero.

**Concrete argument:** These recorded runs fail both requested contracts. They also omit the minimap cost identified above. Choosing EGL settles the variant; these measurements still leave the gates unmet.

**Fix:** Instrument submission, swap, fence observation, and MSC phase separately. Reduce/bound CPU preparation, prepare the next grid while the current frame is pending, and introduce explicit refresh-deadline scheduling for scroll. Validate the resulting EGL path against the complete G3/G3z scenarios under the required load conditions.

## 15. MAJOR — The benchmark enforces the rounded G3 threshold

Location: [bench/gl_bench.c:127](/home/tobias/Projects/editor/bench/gl_bench.c:127).

The code uses 5,560,000 ns. Perf §0 explicitly requires the exact formula: Target A’s p99 limit is `1,000,000,000 / 90 / 2`, approximately 5,555,555.56 ns.

**Repro:** A p99 of 5,558,000 ns passes this benchmark but fails the binding gate.

**Fix:** Compare integer nanoseconds against the exact rational limit, or its conservative integer floor. Add a boundary test between the exact limit and the displayed rounded value.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/gl/gl.c:370` | Per-instance binding cache can use or destroy another context’s resources. |
| 2 | BLOCKER | `src/gl/gl.c:426` | Startup worker races UI updates to native window height. |
| 3 | BLOCKER | `src/gl/gl.c:615` | Failed presents duplicate swaps, overwrite fences, and lose upload state. |
| 4 | BLOCKER | `src/gl/gl.c:632` | Skip-mode completions falsely acknowledge displayed frames. |
| 5 | BLOCKER | `src/gl/gl.c:669` | Unrelated MSC notification can permanently strand a valid frame. |
| 6 | BLOCKER | `src/gl/gl.c:628` | Event-driven callers lack completion continuation and failure detection. |
| 7 | BLOCKER | `bench/gl_bench.c:72` | Overflowing ELF bounds check permits an out-of-bounds read. |
| 8 | MAJOR | `src/gl/gl.c:573` | Whole-table validation/clearing has no typing-slice work budget. |
| 9 | MAJOR | `bench/gl_bench.c:124` | Battery/unknown-power gate misses return success. |
| 10 | MAJOR | `bench/gl_bench.c:104` | G3 verdict omits ingress work and minimap cost. |
| 11 | MAJOR | `bench/gl_bench.c:114` | Qualifying runs lack required samples and bulk-worker scenarios. |
| 12 | MAJOR | `src/x11/x11.c:490` | Native serial T6 records contaminate original-frame latency analysis. |
| 13 | MAJOR | `tests/render_test.c:288` | Actual native resize and close lifecycles lack contract tests. |
| 14 | MAJOR | `docs/decisions/P2.4.md:10` | Recorded EGL runs exceed G3 and G3z limits. |
| 15 | MAJOR | `bench/gl_bench.c:127` | Rounded p99 threshold permits failures of the exact gate. |