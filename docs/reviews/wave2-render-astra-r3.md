# Wave 2 render round-three re-check

**F1, F2 and F3 are fixed in `48a3210`.** All four round-two survivors now fail at the tests Sol named. I compared `7b492a0..48a3210` against the round-three brief and RV-1 through RV-5.

## Findings carried from round two

| Finding | Verdict | Evidence |
|---|---|---|
| F1 · Public null owner | **FIXED** | `public_null_progress` uses the stock factory, accepts a full frame, drains its UI wake and typed notices, and reaches IDLE with synthetic issue, device and display timestamps. A patch repeats the cycle with the next frame ID in the same epoch. My `stock_atlas` probe also submitted a tile with coverage byte 63. After retirement the stock owner's retained scene resolved the accepted cell to byte 63 in the atlas. |
| F2 · Physical stop while a packet is held | **FIXED** | `close_with_taken_packet` and `loss_with_taken_packet` park the stock owner immediately after take and before consume. Close returns WAIT. The epoch-loss event acknowledges the loss with LOST, leaves the service QUIESCING, and a bind retry returns WAIT. In both cases pending names the setup UI wake, the arena mark is preserved, and the wake becomes readable after the owner releases the packet. Close then restores the original mark. After loss a new epoch accepts and retires a full frame. The claim changes from READY to TAKEN before the owner reads the packet. STOP can retract READY and waits on TAKEN. |
| F3 · Backend evidence and scheduling in headers | **FIXED** | `lifecycle` rejects PRESENTED/UNTIMED with ns=1 and accepts ns=0. `render.h` documents each notice's clock/ns encoding, epoch-scoped loss, early evidence and how UI drains feedback. `render_driver.h` identifies the post-upload XShm server fence, matched GL sync, and original Wayland presentation feedback. It defines phase retention and resumption on MORE/WAIT, cancellable waits, the absolute post-issue watchdog, the 5 ms CPU stop check, mailbox publication retry and physical lease release. Native evidence and scheduling rules were checked by reading the headers. |

## Round-two survivors

Each mutant started from a fresh copy of the round-three render sources. I rebuilt the affected service, driver and observer against the same headers and ran the complete committed release `render_test`. Pristine exited 0. Each mutant compiled and exited 1 at its named assertion. Lines refer to `48a3210`.

| Round-two mutant | Named test | Result |
|---|---|---|
| 18 · Call backend poll on UI during submit | `backend_poll_stays_off_submit` | **Caught**, line 602. The poll counter changed before owner dispatch. |
| 22 · Skip the wide-right reserved-field check | `wide_right_reserved_refuses` | **Caught**, line 616. The invalid pair bypassed the required INVALID/CELL refusal. |
| 25 · Run recovery on an input-bearing submit | `input_bearing_submit_skips_replay` | **Caught**, line 632. The required zero directory reads, recovery inspections and queued entries no longer held. |
| 26 · Publish without signalling the command wake | `command_owner_wake_is_signalled` | **Caught**, line 655. The owner fd had no readable event after acceptance. |

## New round-three mutants

All four edits change code introduced in round three. “Caught” refers to the committed tests. The three survivors also exited 0 in the ASan/UBSan and TSan render unit tests and the committed render seed replay. The three supplemental witnesses passed on pristine sources and failed on their respective mutants.

| Mutant | Committed result | Distinguishing observation |
|---|---|---|
| A · Skip the stock owner's first-packet bind in `null_run` | **Not caught** | The release unit, public L3 and Law 2 tests all retired frames. My `stock_atlas` probe read 0 at the accepted runtime slot, where the submitted byte was 63. Skipping bind leaves the atlas slot stride at zero. |
| B · Omit the setup UI wake in `render_driver_release` | **Caught** | `close_with_taken_packet` failed in `stop_with_taken_packet` at line 717, exit 1. The parked owner resumed and the wait for a release wake expired. |
| C · Leave READY published on epoch loss | **Not caught** | All three release tests passed. In `untaken_after_loss`, a manually scheduled owner could still claim the old command after a matching epoch loss reached UNBOUND. The probe released the claim without reading the packet. Pristine returned no command. |
| D · Leave the null feedback batch marked ready at successful STOP | **Not caught** | All three release tests passed. `partial_batch_after_loss` took and routed ISSUED from the real owner's three-notice batch, then delivered epoch loss. In UNBOUND the next mailbox take returned an old notice. Pristine returned an empty mailbox. The observation ends at take, before routing a stale notice. |

## New findings

- **NOTE · Stock atlas contents.** The committed stock-factory tests verify progress, timestamps and allocation counts. The unit suite checks atlas contents with the manual owner. A single post-retirement read of a known runtime texel distinguishes mutant A.
- **NOTE · Epoch-loss coverage.** The parked-owner test exercises a taken command before consume. Mutants C and D cover two other states. One command is still waiting to be claimed. The other has been released with feedback left in its mailbox. The pristine implementation handles both.

## Final render verification

The verdicts, mutation tables and findings above were recorded before the final lane run. All six executions exited 0 on pristine `48a3210`.

| Render lane | Result |
|---|---|
| Release, ASan/UBSan and TSan `render_test` | All three passed, including the claimed-packet close and loss tests. |
| Production `layer_L3_test` | Passed with the stock factory and the normal whole-archive L3 link against lower layers. |
| Release `law2_render_test` | Passed. The live guard counted zero allocations during both the full tile/region submit and the resident-row submit. |
| ASan/UBSan libFuzzer `render_fuzz` | Replayed the committed `sequence` seed once and passed. |

The final binaries came from the target Makefile. For ordinary render tests and the fuzzer I restricted the link dependencies to L3 and L0. The isolated L3 binary used its standard link rule. Local receipts and the three review-only probes are under `build/astra-r3/` in this checkout.
