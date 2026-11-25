# Wave 2 render round-two review

**Return for revision.** A full submit to the production null factory accepts frame 1 and leaves the caller waiting with nobody to take its command. When a real owner is supplied for the test, loss and close can report physical completion while that owner still holds the packet. The common service has removed UI-side consume/poll, stages binds correctly, and maintains frame IDs across epochs. All 16 original mutants were caught.

Reviewed `b0d2e9d..7b492a0` against the nine rows of the round-one review, RV-1 through RV-5 and the round-two brief. All source and test line numbers below refer to `7b492a0`. The builds used a checkout-local copy of that revision. The performance measurement remains open. No benchmark result contributes to a verdict.

## Nine original MUST-FIX items

| Round-one Findings row | Verdict | Round-two evidence |
|---|---|---|
| 1 · Backend operations on UI | **OPEN for the delivered null driver.** The common dispatch defect is fixed. | `render_submit` release-publishes one command and signals the owner's wake. Neither it nor `render_event` calls consume or poll. In `lifecycle` a separate OS thread takes, binds, consumes and polls once. In `public_null_progress`, the unchanged factory reaches ACCEPTED and stays there. The setup UI wake stays unreadable, pending says waiting, and the next submit returns BUSY. The stock factory never installs an owner. See F1. |
| 2 · Bind destroys the active picture before acceptance | **FIXED.** | `staged_binding` rejects a bad sheet, stages a new geometry, then rejects a stale first full. The old ID, geometry, sampled ink and credit survive. An old-geometry patch succeeds before adoption. The accepted candidate full gets the new epoch and sheet. Bind writes candidate sheet/storage and a separate FIFO at render.c 219–236. Submit swaps banks only after prep at 329–344. |
| 3 · Idle device loss ignored | **FIXED for loss recognition and invalidation.** | `lifecycle` rejects loss for the retired flight, accepts frame-zero loss for its live idle epoch, clears credit and makes the next submit wait for bind. `staged_binding` rejects frame-zero loss from the previous epoch. `delivery` recovers the earlier glyph through a full plus capped repair. Physical stop of a busy null owner is a separate defect in F2. |
| 4 · NULL wake accepted at setup | **FIXED.** | `physical_retirement` checks INVALID and no handle on a NULL wake. OPENING and RETIRING return the supplied UI wake on WAIT. The driver contract assigns the signal to the driver. The delayed test changes its own hold flag and retries, so it verifies the wait descriptor and state. F1 separately tests progress with the public factory. |
| 5 · Native binding type | **FIXED.** | render.h 11 and 71 use the signed opaque mutable `plat_native_binding *`. The setup lifetime extends through close. The C11 type assertion at render_test.c 39 compiles. |
| 6 · Packed offsets and complete implementor header | **OPEN on the full header requirement. Packed layout is fixed.** | `bounds` checks two disjoint runs and a short overscan row at packed offsets 0, C, 2C. render_driver.h 8–16 specifies offsets, padding, RGB replacement stride, KEEP/REMOVE and tight R8. The headers still omit actionable evidence rules. In `uncorrelated_present_encoding`, an uncorrelated PRESENTED with clock UNTIMED and a nonzero raw native timestamp returns INVALID. The same notice with ns=0 returns OK. The required zero and other missing native rules are in F3. |
| 7 · Reject reason unavailable in stats | **FIXED for refused submits.** | `delivery` refuses a malformed second tile after copying the first, then reads INVALID/TILE with the prior FIFO intact. `staged_binding` reads STALE/STAMP alongside the old picture and geometry. Stats preserve this telemetry across a later accept. Bind refusals have a narrower contract in practice, see N1. |
| 8 · Trace IDs restart at the epoch | **FIXED.** | `staged_binding` accepts old full, old patch, new full with IDs 1, 2, 3. `delivery` also checks monotonicity through loss. render.c 333 increments the service's last accepted frame. T4/T5/T6 use that frame at 360–365. At UINT32_MAX retirement stops the epoch and bind refuses further admission at 217–218. Exhaustion was source-inspected. EDTRACE is unchanged. |
| 9 · Largest full-frame submit cost | **OPEN for measurement. RV-1's code change is fixed.** | `verify_identity_policy` expects release to accept an in-range stale version with zero per-cell identity reads and show zero ink on mismatch. Assertion builds reject the same row. `lifecycle` checks one live stamp read, and `bounds` checks structural refusals. There is no eligible quiet-box result for largest sheet and runtime submit. Neither a pass against 0.5 ms thread CPU nor the G3 exception has been established. |

## Advisor rulings

| Ruling | Verdict |
|---|---|
| RV-1 | **Implemented. Timing undetermined.** The copy loop at render_submit_prep.h 53–84 checks colors, reserved/attribute bits, wide pairs, ID shape and slot range. Only the assertion path consults the directory for changed ink cells. Selected tile delivery still authenticates its own data. Prep checks current stamp and accepted base once per eligible offer, before payload access. Makefile 236 enables identity assertions for san, tsan and fuzz render objects. |
| RV-2 | **Replay fixed. Common owner handoff fixed. Stock null progress open.** No PUMP or tenth service function. A successful input-free submit inspects at most recovery_slots after crediting the copied prefix. Rejections and input-bearing offers leave the replay cursor alone. Pending reports repair work. The command and wake cross the owner boundary. F1 covers the missing receiver. |
| RV-3 | **FIXED.** Bind validates/copies the ASCII and marker sheet and stages a bank. The driver's first opportunity to see the new sheet is in the accepted candidate full packet. Until then the old active picture and hit stamp remain usable. A zero-sized configure separately suspends submission until a drawable bind. |
| RV-4 | **FIXED.** Monotone service-lifetime frame counter, epoch-qualified notices, no trace-format change. |
| RV-5 | **OPEN on null physical release.** RELEASED is absent. The common FSM waits for quiesce OK, and its delayed-driver test covers RETIRE_FRAME and STOP_EPOCH returning WAIT. The production null callback returns OK unconditionally. F2 reproduces early STOP completion with a live owner borrow. |
| Q4 and Q5 | **FIXED.** Bind requires the ordered 95 printable ASCII entries and matching settled marker identities/spans. A selected malformed page refuses the whole offer. The identical malformed page beyond the remaining byte budget leaves the head owed. Independent probe 27 sets the lookahead head's width to zero and gets INVALID/TILE before spending a frame. |

## New findings

**F1 · MUST-FIX · Provide a working owner for the public null factory.** `null_open` at render_null.c 19–25 allocates state, stores the port and returns. No production null code takes the slot, schedules a worker, forwards notices or signals the UI wake. The service owns the command wake, and the production front door exposes neither the port nor the null state. `public_null_progress` linked the production L3 and core archives, selected the unwrapped factory, opened, bound and submitted one valid full picture. It observed frame 1, ACCEPTED, waiting=1, runnable=0, no UI wake in a 100 ms observation and BUSY on the next offer. The missing receiver is established by the call sites independently of that observation window. The L3 link test stops after open/bind/close. The render unit fixture manually dispatches through `render_test_owner_get`. Exercise the shipped factory through acceptance, owner issue, UI evidence and a second accepted frame without a test getter or a caller replacing its open op.

**F2 · MUST-FIX · Join the null owner's packet lifetime to STOP_EPOCH.** Two production-archive probes wrapped only open to capture the documented driver port. A separate pthread took an accepted command and parked at a barrier while retaining its borrow. The UI performed the following operations before releasing the barrier. Neither probe dereferenced the packet after teardown.

| Probe | Required outcome while the owner holds the packet | Observed |
|---|---|---|
| `close_with_taken_packet` | Close reports WAIT with a usable wake and retains the arena interval. | Close returned OK and reset the arena mark to 0. |
| `loss_with_taken_packet` | Epoch loss keeps admission in QUIESCING until the owner stops. | The service reached UNBOUND and a fresh bind returned OK while the owner remained parked. |

Clearing the atomic command revokes an untaken command. Once the owner has exchanged it out, the atomic slot is already empty. `null_quiesce` at render_null.c 65–70 checks no owner state. Common releases storage at render.c 473–482 on its OK. Track physical owner completion across take, processing and final release, including the interval before consume. Recheck the stop condition on UI wake.

**F3 · MUST-FIX · Finish the backend evidence and scheduling contract in the headers.** render_driver.h now covers packet packing, owner dispatch, partial-open cleanup, physical leases, common scene apply, mailbox publication retries, backing restoration, R8 orientation/alignment and the integer Q8 formulas. A backend author still needs these signed rules beside the operations:

- Give the accepted clock/ns combinations for each notice. In particular, an unconvertible display timestamp must become PRESENTED/UNTIMED with ns exactly zero. A real or synthetic ISSUED and DEVICE_DONE need a nonzero timestamp in their respective domains. Otherwise render_event can refuse evidence and leave the flight waiting. The concrete encoding probe is in row 6.
- Identify actual device completion on XShm. Raster job completion or source reuse alone cannot authorize T5. Identify a Wayland frame callback as scheduling only, with the original presentation feedback determining display/discard. The common event API trusts the adapter to make these distinctions.
- Specify cancellable native waits, bounded disconnect/missing-feedback handling and the 5 ms CPU stop check for pool jobs. For a retained command on MORE/WAIT, say what resumes the current phase, including a bind or consume that paused before issue. The current text covers retrying a full mailbox and preventing duplicate issue, but gives little guidance on resuming other waits.

**N1 · NOTE · Clarify stats after bind refusals.** render.h 203–205 says the fields describe the most recent refusal. `bind_reject_stats` accepted a frame and then rejected a bad marker with INVALID/SHEET. Stats still reported OK/NONE. Either qualify the comment as the last *submit* refusal or record the bind failure too. The original submit diagnostic requirement is met.

**N2 · NOTE · Four round-two mutants expose test gaps.** The committed suite permits a UI poll that returns WAIT without side effects, a malformed wide-right reserved field, a replay read during an input-bearing offer, and a missing owner wake. The pristine code behaves correctly in all four focused probes. See the matrix below.

## Size and structural changes

I stripped blank lines, C comments and RENDER_TEST_HOOKS blocks, excluded render_test.h and retained the production assertion path. By file the count is render.c 455, render.h 121, render_driver.h 67, render_internal.h 106, render_null.c 73, render_scene.h 93, render_submit_prep.h 239 and render_types.h 71. **Total 1,225**, a reduction of 130 from round one.

| Function group using the implementation report's allocation | Actual | Signed estimate | Difference |
|---|---:|---:|---:|
| Values, service and driver declarations | 259 | 140 | +119 |
| Prep and checked geometry/storage helpers | 294 | 210 | +84 |
| FIFO delivery and bounded replay | 64 | 80 | −16 |
| Common service and owner-side scene | 535 | 230 | +305 |
| Null and selection | 73 | 50 | +23 |
| **Total** | **1,225** | **710** | **+515** |

1. **One retained scene.** render_scene.h owns sheet/runtime install metadata, patch application, the R-A incidence scan and row damage. Null provides an atlas writer and calls port->apply once, at render_null.c 45. There is one retained cell array. The common scene header alone accounts for 93 production lines, and real backends can use the same port. The signed budget placed retained patch/install and scan/damage partly in a separate integration allowance. Moving that responsibility into common explains part of the +305 without deleting those lines from the project.
2. **Observers moved.** render_test.h and hook-only implementations expose the owner, borrowed samples/region and scan timing. Samples are computed on demand. The production driver view contains real retained cells, region RGB and damage required for drawing. The production L3 archive exports nine service functions and the null factory for render, with zero render test symbols.
3. **One evidence path and one packet-array description.** render_event records the two axes and render_time performs clock clamping/tracing. Stats mask early evidence until issue. No second recorder republishes the same events. The five packet arrays are declared once in render_packet and allocated through those fields in render_reserve. The retained scene/atlas have their own required lifetime.

The declaration excess includes the port, packet, scene view, directory/read adapters, typed fields and lifecycle/notice/counter contracts. The prep header includes 48 lines for region validation/copy and 68 for final offer checks and staging including its closing header guard. In render.c, measure takes 67 lines, reserve/open 70 combined, sheet validation/bind 50, and pending/stats/close 64. The 73-line null still has to install R8 bytes and issue typed synthetic events. Its production owner and the selection adapter are absent from that inventory.

**What could still go.** The region capacity checks and five repeated backend-cap comparisons could share the same field/required/supported helper. Extract identical active/staged backlog predicates in pending only if the resulting code is smaller. The four-phase null emitter duplicates notice ordering in its enum and switch and could use one small, explicit sequence with one idle state. Fix its ownership/lifetime first. Size these changes by diff. There is no demonstrated 515-line deletion. Cutting the second queue bank, physical stop, boundary checks or retained scene would remove behavior required by the rulings.

## Mutation results

Each subject started from clean round-two render sources, with one targeted edit. I recompiled the service, null and test translation units so changes to inline driver helpers reached the observer. The release suite was the committed tests/render_test.c. The pristine build exited 0. Every listed catch exited 1 at the indicated assertion. Every survivor compiled and exited 0. Rows 3, 12 and 15 use the round-two equivalents of the removed second stamp check, late failure after copying a prefix, and PUMP recovery. Row 9 injects the observable bogus loss times at the public counter boundary.

| # | Independent mutation | Committed suite result, first assertion line |
|---:|---|---|
| 1 | Bypass sheet/marker validation at bind | **Caught**, 143. Previously survived. |
| 2 | Spend a frame number on refused prep | **Caught**, 80 |
| 3 | Skip the sole live stamp read | **Caught**, 82 |
| 4 | Sample by slot regardless of version | **Caught**, 264 |
| 5 | Skip R-A on a one-tile batch | **Caught**, 238 |
| 6 | Run R-A on a zero-tile packet | **Caught**, 104 |
| 7 | Clear both halves' background on a residency mismatch | **Caught**, 265 |
| 8 | Ignore STOP_EPOCH WAIT | **Caught**, 413 |
| 9 | Expose fabricated T5/T6 on pre-issue loss | **Caught**, 413 |
| 10 | Set accepted watermark one ordinal short | **Caught**, 150 |
| 11 | Damage only users expecting the newly installed version | **Caught**, 302 |
| 12 | Credit a copied prefix when a later selected tile fails prep | **Caught**, 219 |
| 13 | Ignore RETIRE_FRAME WAIT after the two axes finish | **Caught**, 402 |
| 14 | Drop wide-right coverage extent check | **Caught**, 313. Previously survived. |
| 15 | Scan the whole recovery directory in one repair | **Caught**, 172 |
| 16 | Accept a row whose cell validation failed | **Caught**, 473 |
| 17 | Call bind/consume on UI before the owner takes the packet | **Caught**, 83 |
| 18 | Call poll from UI on submit while null is ready | **Not caught** |
| 19 | Publish candidate hit geometry during bind | **Caught**, 150 |
| 20 | Ignore matching idle epoch loss | **Caught**, 119 |
| 21 | Restart frame numbering on adoption | **Caught**, 169 |
| 22 | Omit the wide-right reserved-field check | **Not caught** |
| 23 | Omit slot-range validation in the release copy loop | **Caught**, 485 |
| 24 | Accept a frame-zero disconnect with the wrong epoch | **Caught**, 170 |
| 25 | Advance replay on an input-bearing offer | **Not caught** |
| 26 | Publish a command without signalling its owner wake | **Not caught** |

**16/16 original and 6/10 new mutants caught by the committed suite.** The four supplemental witnesses each passed on the pristine build and failed on the corresponding survivor. Probe 18 recorded one UI-thread poll during submit. Probe 22 returned OK for an otherwise valid wide pair with right.reserved=1. Probe 25 recorded one directory read on an input-bearing full with no tiles selected. Probe 26 observed the command wake fd unreadable immediately after successful publication. On pristine code the corresponding results were zero UI polls, INVALID/CELL, zero directory reads, and a readable command wake.

## Verification receipts

The findings, size counts and mutation matrix above were written before final lane execution. Review-only source snapshots, probes and logs are under the checkout's ignored build/r2 directory. The final runs used the checked-in tests and build flags. I restricted archive dependencies to L3 and L0 for the targeted sanitizer and fuzzer links. The isolated L3 binary used its normal whole-archive link and all lower archives.

| Render-only execution | Result |
|---|---|
| Release render_test | Passed, exit 0 |
| ASan/UBSan render_test with identity assertions | Passed, exit 0 |
| TSan render_test with identity assertions | Passed, exit 0 |
| Release law2_render_test | Passed, exit 0. Live guard recorded zero allocations for the full tile/region offer and the resident row. |
| Isolated release layer_L3_test | Passed, exit 0 |
| ASan/UBSan libFuzzer replay of fuzz/corpus/render/sequence | Passed, exit 0. Replayed the 22-byte committed seed once. |

The verification scope was render only. Native backends, repository-wide contract lanes and timing certification remain outside these receipts.
