# W0.6j · R4: restore under real live-tab pressure

**Outcome.** R4 is green on the pinned, inspected v1 oracle. The pressure precondition held on the first fixture design. Live capacity C = 1000 was measured by a public capacity refusal (M, four runs). The relaunch is a genuine partial restore: 997 newer clean tabs, both unsaved tabs of the older session, and one of its three clean tabs. The other two are absent. The partial-restore notice is visible. The older journal is set aside byte for byte. A SIGKILL and second relaunch bring back the same 1000 tabs and the same dirty bytes. Fixture files are untouched. Both red controls fail at the intended assertion.

Case: `tests/contract/cases/capacity.py`, invocation `capacity`, case `R4` in `tests/contract/expectations.json`.

## Invariant

After a relaunch whose discoverable sessions together exceed the live-tab capacity C, every unsaved buffer comes back with its exact bytes. Clean tabs are dropped only to make room. The drop is announced. The dropped state remains recoverable. A second crash before any save loses nothing that the first restore brought back.

## Fixture design

Three phases on one private display (`1280x1024x24`), each wait on an event (inspector snapshot reply or pidfd):

1. **Capacity, in a sacrificial fresh profile** (its own sandbox; closed before phase 2). The editor starts with `p0000.txt`. Distinct tiny files `pNNNN.txt` are forwarded through public handoffs: a second launch with paths, `Sandbox.relay`. A v1 request is admitted or refused as a whole (`src/editor/buffers.c:editor_open_request`). The case therefore sends batches of 32 and halves the batch after each refusal. It stops at a refused **one-path** handoff. C is the live tab count at that refusal. The receipt must be exactly `sublimite: handoff failed: request or editor capacity limit exceeded` with exit 2 (P, `src/main.c`, `src/ipc/result.c`). `EDITOR_ERR_CAPACITY` is the only editor-side source of `IPC_LIMIT`, and a one-path request cannot hit the client's wire bound. Any other answer, including the EMFILE notice path (`IPC_REJECTED`) or a timeout, is ERROR. Control: forwarding an already-open path is still admitted and activates `p0000.txt`, so the editor is alive and the refusal concerned a new slot. The search is bounded by 2 × 1000 (P, the pin's configured `.tab_capacity`). Past that, no refusal is discoverable (ERROR).
2. **Producers, one shared DATA.** The filler F starts first as the IPC primary with `f0000.txt`. The older editor O then starts as a `--new-instance` peer with `c0.txt c1.txt m.txt c2.txt`. A peer has no endpoint, owns its own session journal and shares F's DATA. While F lives, O's startup discovery skips F's leased journal, and vice versa. In O the case types a random marker at the end of `m.txt` and opens Ctrl+N with a second marker. O's ring, read by snapshots, is `c0 c1 m* c2 untitled*`, so U = 2. The dirty named file comes after two clean candidates. O hot-exits (Ctrl+Q, exit 0, names its kept journal). Only then does F admit `f0001…f0996`, so F holds exactly C − U − 1 = 997 tabs. F was started with a path, so it has no launch tab. Both producers have an empty closed stack. F hot-exits last, so its journal is strictly newer by mtime. Both journals are far below `JOURNAL_STARTUP_SCAN_BYTES` = 105,000,000 (P): about 12 KB and about 690 KB (M). All of this is checked in `producers-admitted`.
3. **Restore.** An argument-free relaunch (inspected primary) restores F whole (newest first, `editor_session_restore`). It then restores O under pressure: room = C − 997 = U + 1, and `plan_partial` keeps every unsaved buffer plus one clean tab. The case reads the bottom row of the first frame. It then walks **every** live tab with Ctrl+PageDown until the ring closes, so an off-strip tab is visited, not inferred. It copies both dirty tabs (Select All, Copy, a snapshot to drain, then a check that the editor owns CLIPBOARD, then a read) and lists DATA. It then SIGKILLs the editor before any save, relaunches, walks and copies again, and closes it with SIGTERM to read the allocation count.

Tab identity is (title, first document row). Every fixture file's content is its stem with no newline, and every marker is unique, so the walk closes at the first repeat. A repeat elsewhere is ERROR.

### Deviation from the design paragraph, and the sandbox change

`wave0-contract-design.md` asks for "two simultaneously owned instances, separate private displays/runtime endpoints, same disposable DATA". The case keeps the substance and drops the second display. The two displays only kept two IPC endpoints apart. v1's public `--new-instance` launch has no endpoint (`src/main.c`: no `ipc_server_init_display`), so one display suffices. The manifest's at most one display per invocation stands. `run.py` is unchanged.

To inspect O, the peer must inherit the inspector pair. `Sandbox.relay` gained `pass_fds`, the same parameter `start` and `restart` already have. The relay still verifies the recorded primary first. Red→green: `test_sandbox.py SandboxTests.test_private_primary_relay_restart` now also asserts that a named fd reaches the relayed child and a stray inheritable fd does not. Red: `TypeError: Sandbox.relay() got an unexpected keyword argument 'pass_fds'`. Green: 21/21. No test method was added, so the SANDBOX inventory is unchanged.

### Simplest alternatives, rejected

- **One producer over capacity.** Counter-example: v1 refuses an open before it enters any journal (`editor_open_request`), so one session never holds more than C tabs. Its relaunch always passes `session_fits`, and no partial restore can happen.
- **Sequential producers** (O exits, then F starts). Counter-example: F's own startup discovers O's orphan and restores it into F. One merged session results again.
- **Two displays and two sandboxes sharing DATA.** This works but needs a multi-root sandbox, a second Xvfb and a runner change, and it buys nothing over `--new-instance`.

## Assertions (all A+B unless noted; first required wave 3)

| ID | Evidence | Predicate |
|---|---|---|
| `capacity-measured` | A | one-path capacity refusal at C; editor alive; existing path re-admitted |
| `producers-admitted` | A+B | O's ring is `c0 c1 m* c2 untitled*` with exact rows; F admitted C − U − 1; both name their kept journal; F newer; both below the startup scan bound |
| `partial-restore-triggered` | A+B | live tabs = C, every filler present, the older session **not** restored whole, nothing unclassified |
| `notice-visible` | B | bottom row = v1's notice `restored 2 unsaved tabs; 2 unmodified tabs from the last session were not reopened; the old session journal was set aside`, clipped to the row width (96 columns at this geometry) |
| `dirty-restored` | A+B | CLIPBOARD bytes of `m.txt` and the untitled tab equal the typed bytes |
| `clean-survivor-tab-order` | A+B | by path: the surviving clean tab is `<fixtures>/c0.txt`, and `c1.txt` and `c2.txt` are dropped (rule below). The walk shows c0 and no c1 or c2 (B). At C live tabs, a forwarded open of c0's path is admitted and activates c0, while c1 and c2 are each refused for capacity (A) |
| `omitted-state-recoverable` | A | DATA holds exactly one `<older source>.set-aside-*` whose bytes equal O's journal copied before the relaunch (opaque comparison). F's source is gone, so the restore checkpoint is durable before the SIGKILL; otherwise ERROR |
| `second-recovery` | A+B | after SIGKILL and relaunch: the same tab set and the same dirty bytes by clipboard |
| `disk-intact` | A | every fixture file byte-identical after both recoveries |
| `law2-count` | B | relaunched editor's passive count is 0 |

On v1, the notice wording is v1 policy. When W3 converts R4, the v2 notice text must be bound in the case.

### Which clean tab survives: v1's rule is deterministic

Round 2 (advisor: "some clean tab" is no rule). The first version asserted only that the number of surviving older clean tabs equalled the room. That check is replaced by `clean-survivor-tab-order`, which names the survivor and each dropped tab by path.

**Rule (P, v1 `src/editor/recovery.c:plan_partial`, comment "Unmodified tabs in recorded tab order (buffer order without TABS)").** `editor_session_restore` restores newest source first, and `restore_source` calls `plan_partial` once `session_fits` fails. Every unsaved buffer is picked first. The rest is `spare` = live room − unsaved buffers placed live (− 1 for the temporary slot when some go closed), capped by free buffer slots. `plan_partial` then walks `s->tabs`, the last `JOURNAL_TABS` record replayed by `restore_record` (the strip order at the source's last checkpoint), and marks the first `spare` buffers that are still `PICK_DROP` as `PICK_LIVE`. It skips the buffer named by `JOURNAL_PAINT` (the `edit-dzk1` first-paint guard) and any whose target `target_vanished` reports gone. Everything else is dropped. Without a valid TABS record it uses the buffer list order instead.

**Determinism.** The choice reads only the TABS array, the PAINT id, a stat of each target, and room counts. It reads no clock, hash or unordered container; `find` is a lookup by id. Given the same journal and the same files, the same tabs survive.

**Fixture inputs.**
- TABS = `c0 c1 m c2 untitled`. `producers-admitted` now also asserts O's strip, `older_strip`. All five tabs fit on screen, so the strip is the order the hot-exit checkpoint records.
- PAINT = 0. Each older tab presented during the producer walk, and `editor_journal_painted` appends PAINT 0 after a buffer's first present.
- All targets exist.
- spare = C − fill − U = 1.

Therefore `<fixtures>/c0.txt` survives, and `<fixtures>/c1.txt` and `<fixtures>/c2.txt` are dropped. In the fixture, c1 is a clean tab ordered before the dirty `m.txt`, so the result also shows a dirty tab outranking an earlier clean one.

**Observation by path.** The tab walk identifies each tab by title and unique contents; titles are basenames in the single fixtures directory. Path identity comes from public opens. Once `partial-restore-triggered` has asserted that live = C, forwarding a path that is still open is admitted (exit 0) and activates that tab, and forwarding an absent path can only be refused for capacity. The case forwards c0, c1 and c2 one at a time and then reads the active tab.

**Law-2 count rule.** The relaunched editor's windows (1007/1008 input-to-submit scopes: the 1000-tab walk, copies and moves) measured **0** allocations on v1. The count is therefore asserted on v1 as well. The older producer's typing window measured 4 allocations in 11 scopes. That is the known v1 typing-path allocation (`v2-he7.23.md`). It is recorded in `facts.law2_older_typing` and not asserted.

## Runs (power and load stamped at start; `/sys/class/power_supply/BAT0/status`)

Commands are run from the checkout root. Oracle: `/home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle/sublimite` (sha256 `ed75336c…f2195`, pin 6fec2e8, patch `54bcf05f…da1a`).

```
python3 -B tests/contract/cases/capacity.py --build-dir /home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle --display :195
python3 -B tests/contract/cases/capacity.py --build-dir … --display :196 --negative excess-clean-absent   # round 1; that assertion is now clean-survivor-tab-order
python3 -B tests/contract/cases/capacity.py --build-dir … --display :196 --negative no-pressure
```

| run | power, load | outcome |
|---|---|---|
| `contract-capacity-u9qozozn` (attempt 1, batch 128) | Full, 0.67 | ERROR `capacity-measured`: handoff 7 (paths 769–896) answered `running instance did not respond before the timeout`. A harness sizing fault, not the precondition; see the batch derivation |
| `contract-capacity-v8wtgv9p` (batch 32) | Full, 0.84 | PASS ×10 in 183 s; C = 1000 (refused `p1000.txt` after 40 handoffs) |
| `contract-capacity-14uukl3m` red | Full, 2.18 | FAIL `excess-clean-absent` (observed 1, wrong expectation 3); the five earlier assertions PASS |
| `contract-capacity-myp9w9z5` red, no pressure | Full, 1.94 | FAIL `partial-restore-triggered`: live 6 ≠ 1000, older session restored whole |
| `contract-capacity-27xwwcbz` final code | Full, 1.40 | PASS ×10 in 151 s; C = 1000 |

Every run measured C = 1000. The fixture design held deterministically at its first attempt, so the T5 stop condition was not reached.

**Round 2** (`clean-survivor-tab-order`; box on battery, so stamped [bat]; display `:195`):

```
python3 -B tests/contract/cases/capacity.py --build-dir /home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle --display :195 --negative clean-survivor-tab-order
python3 -B tests/contract/cases/capacity.py --build-dir … --display :195 --negative no-pressure
python3 -B tests/contract/cases/capacity.py --build-dir … --display :195
```

| run | power, load | outcome |
|---|---|---|
| `contract-capacity-pamg49eo` red, swapped survivor | [bat] Discharging, 0.64 | FAIL `clean-survivor-tab-order`. Observed: c0 kept; c1 and c2 dropped; handoffs c0 admitted, c1 and c2 refused; c0 active. Expected (swapped): c2 kept, c0 and c1 dropped. The five earlier assertions PASS |
| `contract-capacity-jk_oelwv` red, no pressure, fill = 1 | [bat] Discharging, 2.85 | FAIL `producers-admitted`: `filler_newer` False. A control defect: with fill = 1 the filler writes nothing after the older editor's hot exit, so its journal is not reliably newer (round 1's run passed this by chance). Fixed: the no-pressure control uses fill = 2 |
| `contract-capacity-3m_6n91x` green | [bat] Discharging, 2.59 | PASS ×10 in 175 s; C = 1000; `<fixtures>/c0.txt` kept; `c1.txt` and `c2.txt` dropped, each refused for capacity by path; law-2 0 over 1007 scopes |
| `contract-capacity-owkarpgp` red, no pressure, fill = 2 | [bat] Discharging, 2.31 | FAIL `partial-restore-triggered`: live 7 ≠ 1000, older session restored whole; `capacity-measured` and `producers-admitted` PASS |

The fill = 2 change touches only the `--negative no-pressure` branch. The green run's code path (fill = C − U − 1) is identical before and after it.

**Batch size (Law 4).** The ceiling is `IPC_MAX_PATHS` = 128 (P). A forwarding launch gives up after 2000 ms (P, `ipc_client_send_display(…, 2000)`). v1's per-open cost grows with the live tab count. In attempt 1 (M, [AC, load 0.67]), 128-path handoffs took 0.3, 0.6, 0.9, 1.2, 1.5 and about 1.8 s at 1, 129, 257, 385, 513 and 641 tabs (handoff log mtimes), and timed out at 769. (E) a quarter of that batch, 32, stays near 0.6 s at 1000 tabs. A timeout leaves the admission unknown and stays ERROR.

**Runner timeout.** 1900 s = (M) 183 s green × 10 (E) headroom, rounded up. Each serial 15 s hang detector (P, `WATCHDOG`; about 2100 serial waits, dominated by the two 1000-tab walks) bounds one hung wait. Their sum is no meaningful run bound.

`make all` and `make check CONTRACT_CHECK_DISPLAYS=:197-:199` (HOME under `build/`) pass: harness gate PASS 52.

## Ledger note for v2

R4 assumes a **discoverable live-tab capacity refusal through a public open**. A forwarded open that would exceed the live pool must be refused as a capacity refusal the client can tell apart from resource exhaustion (exit status plus message), while an already-open path stays admissible. v2 must provide that, or R4 needs a plan amendment before W3. It also assumes a public way to run a second editor on the same profile alongside the primary (v1 `--new-instance`). `clean-survivor-tab-order` is required of v2 as written. Under pressure, clean tabs survive in recorded tab order up to the room left after every unsaved tab. Here that means `c0.txt` survives and `c1.txt` and `c2.txt` are dropped. A v2 restore rule that picks differently needs a plan amendment, not a quietly edited expectation.

## Observed, not fixed (report only)

- v1's handoff cost rises linearly with the live tab count (above), so filling 1000 tabs is quadratic. That is not a trial-use defect at Tobias's tab counts; it is noted for the v2 open path.
- The copy observation reads `GetSelectionOwner` after the snapshot reply. The editor flushed its `SetSelectionOwner` before submitting, but cross-client ordering at the X server is not a protocol guarantee. A mismatch is ERROR (fail closed), never a pass.

## W0.7-B4 · 2026-11-23: R4 defects from the Wave 0 review (v2-he7.30)

Findings 5, 6, 7, 14, 26 (capacity part) and 33 of `docs/reviews/wave0-code-astra.md`. Only `tests/contract/cases/capacity.py` changed. Assertion IDs are unchanged, so `expectations.json` is untouched.

**Superseded above.** The `producers-admitted` predicate no longer includes "F newer", and the last bullet of *Observed, not fixed* (the one-shot `GetSelectionOwner`) is fixed here. The rest of the section stands.

- **5 · notice geometry.** `notice-visible` used to take its expected prefix length from the reported bottom rectangle. An empty rectangle, which is legal on the wire, therefore certified an empty notice, and a one-cell rectangle certified "r". The case now establishes the status row independently: columns = window width ÷ cell width (P, v1 `src/editor/editor.c`, below its `max_cols` clamp), and the row is the grid's last row (P, `src/editor/paint.c` `editor_status_overlay`: "one owner for the last row"). It requires the reported rectangle to equal `(0, rows − 1, columns, 1)` and the grid width to equal `columns`. It reads the text from that row and clips the notice to that width. On the pin (M, run `contract-capacity-ppr038av`) this is `(0, 43, 96, 1)`, 96 columns.
- **6 · admitted filler set.** `partial-restore-triggered` compares the sorted filler titles with `filler_name(i)` for `i < fill`, not with their count. The `second-recovery` comparison with the first restore is kept as an independent persistence check.
- **7 · Copy ordering.** `Editor.copy` is now recovery's `copied_text`. It sends one Select All and one Copy, then waits for the CLIPBOARD owner to move from the sentinel (or 0) to the editor's window, within the 15 s watchdog. It then reads from that window. A third owner is ERROR (`Refused`), and so is expiry (`TimeoutError`). The snapshot drain before the owner query is gone: the ownership transition is the completion event.
- **14 · reds reach their cause only.**
  - Discovery orders sources by `st_mtim` in ns with a path tie-break (P, `recovery.c` `newer_source`, `os_posix.c` `ts_ns`). Once both producers have exited, the case stamps O = ⌊max(both)⌋ to the whole second and F = O + 1 s. It reads both back as setup, and a mismatch is ERROR. Whole seconds survive any filesystem granularity up to 1 s, and a 1 s step orders them in ns. "F newer" is no longer scored as editor behavior.
  - The no-pressure control first requires the exact unpressured ring under ample C. That ring is both fillers plus all five older tabs, with their (title, first row) identities and dirty rows; C > 7 here. Anything else is ERROR at `partial-restore-triggered`, and only then does the pressure expectation get contradicted.
  - Generally (`Case.check`), a `--negative` assertion must first meet the ordinary expectation before its wrong expectation is credited. Otherwise it is ERROR at that ID. This covers the swapped-survivor red: c0 admitted and active, c1 and c2 refused for capacity.
- **26.** `law2-count` lost its unreachable `wrong=1`. `NEGATIVES` cannot select it.
- **33.** `Editor.ring` keeps the ordered list for indexing and tests membership in a set.

**Red → green.** The scratch harness fakes the editor, driver, sandbox and handoffs around the case's own `producers`, `restore`, `Case.check`, `Editor.copy` and `Editor.ring`. Against the pre-change file, 4 of 15 required outcomes held. After the change, 15 of 15 held:

| proof | before | after |
|---|---|---|
| 5 zero-width / one-cell / misplaced notice rectangle | all 10 PASS | FAIL `notice-visible` |
| 6 f0010 → f1999 on both restores, counts unchanged | all 10 PASS | FAIL `partial-restore-triggered` |
| 14 no-pressure, equal / reversed producer mtimes | FAIL `producers-admitted` | FAIL `partial-restore-triggered`, live 7, two PASS before it |
| 14 no-pressure, a dirty tab dropped / a wrong filler | FAIL `partial-restore-triggered` (credited) | ERROR `partial-restore-triggered` |
| 14 swapped-survivor on an editor keeping c1 | FAIL (credited) | ERROR `clean-survivor-tab-order` |
| 7 owner held at the sentinel for two queries, then transferred | ERROR after one query | PASS, one Copy, read bound to the window |
| 7 expiry | ERROR (`Refused`, one query) | ERROR (`TimeoutError` at the watchdog) |

Unchanged in both versions: a third owner is ERROR, the healthy swapped and no-pressure reds FAIL, and the ring traces for a full 1000-tab ring and a duplicate before the start match. 26 and 33 do not change behavior. Their proof is that the CLI cannot select `law2-count` and that the traces are identical.

**Oracle runs** (pin as above; displays :195–:197; [AC] Charging; load from other workers on the box):

| run | power, load | outcome |
|---|---|---|
| `contract-capacity-t65c2vxw` green, `contract-capacity-h6901idi` swapped (concurrent with the next) | Charging, 6.5–17 | ERROR `producers-admitted`: fill handoffs 017 and 012 answered `running instance did not respond before the timeout`. Three 1000-tab fills ran at once under a load of 12–17, and v1's 2000 ms forwarding bound (see *Batch size*) expired. Harness load, not the change; serial reruns below |
| `contract-capacity-g2jzwhze` red, no pressure | Charging, 6.5 | FAIL `partial-restore-triggered`: live 7 ≠ 1000, fillers `f0000 f0001`, older session whole. `capacity-measured` and `producers-admitted` PASS. The exact-ring precondition held. Stamps O = 1795461730 s, F = O + 1 s |
| `contract-capacity-ppr038av` green | Charging, 17.8 | PASS ×10 in 399 s; C = 1000; notice row `(0, 43, 96, 1)`; law-2 0 over 1006 scopes |
| `contract-capacity-m5taeqde` red, swapped survivor | Charging, 16.4 | FAIL `clean-survivor-tab-order` in 148 s, after the ordinary observation held: c0 kept and active, c0 admitted, c1 and c2 refused. The wrong expectation is c2 kept. The five earlier assertions PASS |
