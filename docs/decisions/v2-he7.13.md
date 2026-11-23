# W0.6f file contracts R3 and R5 on the inspected v1 oracle

Both cases now run on the inspected v1 build, with disk assertions and
screen assertions in the same run. The runner takes `--build-dir`, the
`build/contract-oracle` directory of the disposable v1 tree. It checks the
stamp's pin `6fec2e8c…` and patch SHA-256 `54bcf05f…fda1a`, and passes the
stamped executable digest `ed75336c…2195` to the sandbox, which checks it on
every launch. This is the same identity check as in
[W0.6g](v2-he7.14.md). Every editor launch is inspected (`--inspect-fds`).
The stock-oracle mode (`--oracle`) is gone. Its Tier A assertions run
unchanged on the inspected build. The stock-pin disk and clipboard control is
still the pre-ACK recovery case.

```sh
make check-contract-files ORACLE_TREE=/home/tobias/Projects/sublimite-v1-inspect CONTRACT_DISPLAY=:160
```

The run starts an authenticated Xvfb on the explicitly selected display
(`:160`–`:169`, `-noreset -nolisten tcp`, private cookie). The screen is
1280 × 1024 × 24, Xvfb(1)'s default. The runner stops Xvfb by its recorded
PID and waits for it on a pidfd. Each invocation keeps its manifest, JSON
Lines records, step logs, snapshot transcript, editor logs and final
fixture tree under `build/contract-files-*`.

**Mapping capacity.** The memfd uses the bound from
`test_oracle_inspector.CAPACITY` (one settled frame, one overscan row, a cell
per window pixel carrying at most U+FFFD, a tab per column). Here it is
evaluated at the screen size, 1280 × 1025 cells, which gives 30,201,704 bytes
(P). Every frame's window must fit the screen, or the run is `ERROR`. The
observed window was 768 × 704 pixels, a 96 × 44 grid of 8 × 16 cells (M).

**Exit statuses.** The runner uses the same codes as `recovery.py`: `PASS` 0,
`FAIL` 3 and `ERROR` 4. When both cases run, `ERROR` outranks `FAIL`. Make
reports a nonzero status as its own exit 2. The first failure in a case is
final, and the case's later steps do not run. The other case still runs.
After a green case, the runner checks the assertion inventory: each known ID
must run exactly once, in order.

## Waits

Every wait is on an event. A timer is only a hang detector, and its expiry
is `ERROR`, never `FAIL`. The operation and driver pipe watchdogs are 15 s
and 30 s, as in [W0.6b](v2-he7.9.md).

- **Snapshots.** A SNAP or NEXT reply wakes on the inspector socket and the
  editor's pidfd together. An editor exit is `ERROR`, and so is a missing
  reply at the watchdog.
- **Disk.** The directory watch is armed before the keys that cause the
  event: `IN_MOVED_TO` for a save, `IN_ACCESS` for a binary open,
  `IN_MOVED_TO` of the session journal. The editor's pidfd is watched as
  well.
- **Exit.** Ctrl+Q is reaped on a pidfd, and so is the Xvfb teardown.
  `Popen.wait(timeout)` is no longer used, because it polls.

## R3

The search tree uses the matching files and binary sentinel from v1's
`tree_fixture_init`. The editor starts with A open. Ctrl+Shift+F searches
for `cat` in the private tree.

**Search completion (review fix).** The old `search_copy` re-sent Select All
and Copy in a tight loop. It returned `FAIL` whenever a sampled intermediate
copy was not a prefix of the final text. Now the runner sends one SNAP, then
NEXT after each returned serial, until a submitted frame has `Find Results`
active and draws the search's end line (`N matches across M files`, or a
failed, truncated or cancelled search). That frame is compared once against a
model of the fixture results. No clipboard is involved. A watchdog expiry
here is `ERROR`.

**Find Results model.** v1 draws the results in a tab with soft wrapping. A
fixture line wider than the document breaks every `width` cells. Only the
absolute path lines are that wide, and they contain no spaces. The model
(`hard_wrapped`) chunks each line of `search_results(root)` at the frame's
document width. It pads to the document height and compares every row. If a
wide line contained a space, or the results did not fit the height, the
fixture would fall outside the model, and the runner reports `ERROR`.

In all 19 R3 runs that reached the search (the green series, the two first
attempts and the seven R3 controls that got past it), the first SNAP already
showed the completed results (M, the retained `snapshots.jsonl`). The
adapter's prefix fence waits while `find_replace_active` is set, so the NEXT
branch is a guard that this fixture never reached.

**Opening B from a match.** Go to Line 8 selects the `scatter` result (P, the
pin's zero-based row 7), and Return opens B at the match. The SNAP after
Return returns once the open is disposed, with no `pending_open` and no
placeholder.

- **results-between-a-and-b** (B). The tab strip reads
  `a.txt | Find Results | b.c`, with B active. Each title is read from its
  tab rect less v1's two trailing cells, which hold the modified dot.

**A9 (Ctrl+S overtaking typed keys).** Before typing `b`, the runner arms a
watch on the case's journal directory for the single `session-*` file. It
types `b` and waits for that file's `IN_MOVED_TO`, the same signal
`large_undo.py` uses. The SNAP reply is the second witness. The adapter
answers only when no input is queued, `op_count` is 0 and the buffer is
neither opening nor a placeholder. The frame must also draw `sbcatter`
exactly once in `b.c`. So the edit is applied before Ctrl+S is sent, whatever
the journal's rotation policy. A undergoes the same frame check
(`aAlpha cat`) before its save.

- **save-b-bytes** and **save-a-bytes** (A). The armed `IN_MOVED_TO` on the
  destination acknowledges each save, and the bytes are compared exactly.
- **results-text-still-visible** (B). After both saves, Alt+2 selects the
  second tab. The frame must show the unchanged strip, with `Find Results`
  active, and every document row must equal the results model.
- **other-files-unchanged** (A). The other files' bytes, inodes, modes and
  modification times match after the B save and after Ctrl+Q.
- **law2-count** (B). See below.

## R5

`binary_bytes` ports v1's `binary_open_fixture`. The comparator evidence
from the stock half is unchanged: SHA-256
`fd6eaa29df67bd85a9329d15f48362c6479a3bcfbaa646e467c8a197b24b4387`,
256 KiB, seed 2463534242. The first launch opens `fixture.pdf` and a
sibling `sibling.txt` (`sibling text\n`). The sibling tab starts active.
`IN_ACCESS` on the PDF precedes binding the window.

- **open-alive** (A). The fixture is read, an owned window is mapped and the
  editor is live.
- **header-painted** (B). After Alt+1, `fixture.pdf` is active and the first
  document row is exactly `%PDF-1.5`. Under T3 only baked ASCII and known
  replacement artwork resolve. The row reader maps an unresolved cell to
  U+25AF, a character no expected (ASCII) text contains. Unresolved cells in
  other rows are allowed, while a required match that crosses one fails.
- **adjacent-text-and-sibling-save** (A+B). After Alt+2 and `s`, the frame
  draws `ssibling text` in the active `sibling.txt`. The armed save then
  writes exactly `ssibling text\n`.
- **open-exit** (A). Ctrl+Q exits 0.
- **reopen-alive** (A). The same case root starts a new process on the PDF.
- **settled-binary-tab-revisit** (B). Two frames show `fixture.pdf` active
  with `%PDF-1.5` as the first row: the Alt+1 revisit after the sibling save
  in the first process, and Alt+1 in the reopened process.
- **reopen-exit** (A) and **bytes-unchanged** (A). The second Ctrl+Q exits
  0, and both post-quit reads equal the generated bytes.
- **law2-count** (B). See below.

**Font adoption (observation, not asserted).** The transcript records each
frame's count of unresolved cells. Before adoption, v1 draws the PDF's
non-ASCII cells as the resolved `?` placeholder. In every green run the
activation and reopen frames held 0 or 1 unresolved cells, and the revisit
frame held 38–42 (M), where resident shaped clusters are unresolved under T3.
So the revisit frame is a settled frame after font adoption. Its header
assertion passes with those unresolved cells around it. The count is not
asserted, because adoption is asynchronous and the runner has no event for
it.

The reopened editor's Alt+1 is also its only input-to-submit window. The first
green attempt (`contract-files-j36qm41k`) took the reopen frame without
input. Its guard reported `scopes=0`, which the runner rejects as vacuous
(`ERROR`).

## Law-2 count

Each inspected editor prints `oracle law2: active=… allocations=… scopes=…`
at its clean Ctrl+Q exit. A count needs `active=1` and `scopes > 0`, otherwise
the run is `ERROR` (fail closed). In all 10 green runs (M), R3 reported
`allocations=0 scopes=4`, R5's first process `allocations=0 scopes=4`, and the
reopened process `allocations=0 scopes=1`. Under the
[count rule](wave0-contract-design.md#law-2-count-rule-for-inspected-cases-advisor-2026-11-23),
v1 shows 0 for these windows, so 0 is **asserted** for each process and a
nonzero count is `FAIL`.

## Red receipts

The Tier A controls and the new controls each change one expected value per
invocation, through `--negative`. Two controls withhold an event instead.
`R3.snapshot-reply` withholds the first marker, and `R3.save-signal` withholds
Ctrl+S with the watch already armed. The five screen assertions had no green
state before this change. All five reported `ERROR` `waiting: requires
snapshot adapter (v2-he7.11)` on the stock oracle in `contract-files-da8doy86`.

```sh
B=/home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle
PYTHONPATH=tests/contract python3 -B -m cases.files --build-dir $B --display :161 --negative "$control" \
    > "build/files-red-$control.jsonl" 2> "build/files-red-$control.log"
```

Power status was `Charging` [AC] throughout. The load column gives the measured
1-, 5- and 15-minute load averages after each run (M, `/proc/loadavg`).

| Control | Exit | Stopped at | Run under `build/` | Load |
|---|---|---|---|---|
| `R3.results-between-a-and-b` | 3 | `results-between-a-and-b` FAIL | `contract-files-z855xtmi` | 6.32 / 5.35 / 4.53 |
| `R3.results-text-still-visible` | 3 | `results-text-still-visible` FAIL | `contract-files-2fx_zhhl` | 6.32 / 5.35 / 4.53 |
| `R3.law2-count` | 3 | `law2-count` FAIL | `contract-files-8hl8z0mm` | 5.97 / 5.30 / 4.52 |
| `R5.header-painted` | 3 | `header-painted` FAIL (`%PDF-1.4` expected) | `contract-files-oayxccgs` | 2.55 / 4.28 / 4.24 |
| `R5.adjacent-text-and-sibling-save` | 3 | `adjacent-text-and-sibling-save` FAIL | `contract-files-z4v5tlz1` | 5.97 / 5.30 / 4.52 |
| `R5.settled-binary-tab-revisit` | 3 | `settled-binary-tab-revisit` FAIL (crossing) | `contract-files-fh72ksxi` | 2.75 / 4.29 / 4.24 |
| `R5.law2-count` | 3 | `law2-count` FAIL | `contract-files-dmy__v2o` | 5.83 / 5.29 / 4.52 |
| `R3.snapshot-reply` | 4 | `search-readiness` ERROR, missing reply at watchdog | `contract-files-yva8miad` | 5.13 / 5.16 / 4.49 |
| `R3.save-b-bytes` | 3 | `save-b-bytes` FAIL | `contract-files-qyeed9q0` | 5.13 / 5.16 / 4.49 |
| `R3.save-a-bytes` | 3 | `save-a-bytes` FAIL | `contract-files-sayg_b9p` | 5.13 / 5.16 / 4.49 |
| `R3.other-files-unchanged` | 3 | `other-files-unchanged` FAIL | `contract-files-vg7r8gfx` | 4.96 / 5.12 / 4.48 |
| `R3.save-signal` | 4 | `save-b-signal` ERROR, no `IN_MOVED_TO` | `contract-files-7b5ws3p7` | 4.29 / 4.97 / 4.44 |
| `R5.open-alive` | 3 | `open-alive` FAIL | `contract-files-khss4_zp` | 4.29 / 4.97 / 4.44 |
| `R5.open-exit` | 3 | `open-exit` FAIL | `contract-files-q0knppjj` | 4.11 / 4.92 / 4.43 |
| `R5.reopen-alive` | 3 | `reopen-alive` FAIL | `contract-files-mrpimqga` | 4.11 / 4.92 / 4.43 |
| `R5.reopen-exit` | 3 | `reopen-exit` FAIL | `contract-files-wipu6ame` | 3.94 / 4.87 / 4.41 |
| `R5.bytes-unchanged` | 3 | `bytes-unchanged` FAIL | `contract-files-tv8c4m3f` | 3.94 / 4.87 / 4.41 |

**The crossing control (T3).** The negative for `settled-binary-tab-revisit`
requires the revisit frame's document rows up to the first row holding an
unresolved cell. That cell is expected as v1's `?` glyph, and every other
cell equals the frame. The run failed on row 7, `  ?z  ▯G @  ? ?` against
`  ?z  ?G @  ? ?`, and nowhere else. A required match that crosses an
unresolved cell therefore fails, even when every resolved cell matches.

Two earlier header controls are superseded. The first
(`contract-files-qy148r1c`) required the header's second line as U+FFFD. It
failed because v1 draws resolved `?` there, not because of an unresolved
cell. So this control did not exercise T3, and its revisit-frame form
replaced it. The first `R5.settled-binary-tab-revisit` control
(`contract-files-5jrnltie`) expected `%PDF-1.4` on the reopen frame and failed
on that.

## Green receipts

The series has one run per permitted display, `:160` through `:169`: 10 runs
(P, 169 − 160 + 1), the per-display derivation of [W0.6d](v2-he7.11.md). The
count was fixed before sampling. At the start of the series,
`/sys/class/power_supply/BAT0/status` read `Charging` [AC] and `/proc/loadavg`
read 2.79 / 4.25 / 4.23 (M). Every run read `Charging`, with the 1-minute
load between 2.55 and 2.83.

```sh
for d in :160 :161 :162 :163 :164 :165 :166 :167 :168 :169; do
    make --no-print-directory check-contract-files ORACLE_TREE=/home/tobias/Projects/sublimite-v1-inspect \
        CONTRACT_DISPLAY=$d > build/files-green-${d#:}.jsonl 2> build/files-green-${d#:}.log || break
done
```

Each run exited 0 with 15 `PASS` records and no other record: 6 in R3 and 9 in
R5. The serials are `ticket:after→submit`. R3's frames are search, open B,
type b, type a and results. R5's are activate, sibling, revisit and reopen.

| Display | Run under `build/` | R3 frame serials | R5 frame serials | Unresolved in revisit |
|---|---|---|---|---|
| `:160` | `contract-files-auuyzxbk` | 6, 9, 11, 15, 18 | 2, 4, 8, 2 | 38 |
| `:161` | `contract-files-0_p52vo8` | 6, 9, 11, 15, 19 | 2, 4, 10, 2 | 42 |
| `:162` | `contract-files-5zb3sfdn` | 6, 9, 12, 16, 20 | 2, 4, 8, 2 | 39 |
| `:163` | `contract-files-3epk1ef_` | 7, 10, 12, 16, 20 | 2, 4, 8, 2 | 39 |
| `:164` | `contract-files-qz38emjy` | 5, 8, 10, 14, 18 | 2, 4, 8, 2 | 39 |
| `:165` | `contract-files-a1fmsxgz` | 5, 8, 10, 14, 17 | 2, 4, 10, 2 | 40 |
| `:166` | `contract-files-1wch3yec` | 6, 9, 11, 15, 19 | 2, 4, 8, 2 | 39 |
| `:167` | `contract-files-7decbugu` | 5, 8, 10, 14, 18 | 2, 4, 8, 2 | 39 |
| `:168` | `contract-files-x809u7qk` | 5, 8, 10, 14, 17 | 2, 4, 8, 2 | 39 |
| `:169` | `contract-files-bv9q_wyf` | 9, 12, 15, 19, 23 | 2, 4, 8, 2 | 39 |

The R3 edit frames hold one unresolved cell each: the modified dot in the
tab trailer, outside every title read. Before the series,
`contract-files-r4vdz28v` passed all 15 records once on `:160`, at load
6.91 / 5.26 / 4.46 [AC]. After the series, one helper was renamed
(`binary_view` to `tab_view`, with no change in behavior). The same command
on `:160` then passed all 15 records (`contract-files-fctfbmgo`,
5.43 / 4.69 / 4.38 [AC]). The red controls above ran before the rename.

## Builds

Both checkout builds exited 0. Before `make all`, the battery read
`Charging` [AC] and the load was 2.59 / 3.99 / 4.14. Before `make check`, the
load was 3.85 / 4.22 / 4.21 (M, `/proc/loadavg`). Neither target runs the
contract cases. The `tab_view` rename came after `make check` started and
touched only `files.py`.

```sh
HOME="$PWD/build/home" TMPDIR="$PWD/build" make all > build/files-make-all.log 2>&1
HOME="$PWD/build/home" TMPDIR="$PWD/build" make check > build/files-make-check.log 2>&1
```

## W0.7-B5 (2026-11-23): failures scored under listed IDs

Wave 0 review [finding 8](../reviews/wave0-code-astra.md) found two false
FAILs-turned-ERRORs. A completed, contradictory observation at an unlisted
step (`search-readiness`, `edit-b`, `save-b-signal` …) was emitted as FAIL
under that step's name, which the runner treats as a malformed, unowned
record (ERROR). A real nonzero exit after Ctrl+Q raised `Refused` (ERROR)
before `R5.open-exit` could compare it.

**Rule.** A record's `assertion_id` is always the listed assertion the step
serves. A prerequisite runs as `step(<listed ID>, …, part=<step>)`, and every
record carries the executed step in `step`. A `Mismatch` is FAIL only under a
listed ID. `failure()` reports a mismatch anywhere else as an explicit ERROR
("mismatch outside a listed assertion"). After a PASS the case enters
`after-<ID>`, so a later error cannot produce a second record for the
assertion that just passed. ERROR remains the outcome for missing or unusable
observations: a watchdog, an editor exit while waiting, a lost watch.

| Step (`part`) | Scored under |
|---|---|
| `search-readiness` | `R3.results-between-a-and-b` (the scatter match it opens) |
| `edit-b`, `save-b-signal` | `R3.save-b-bytes` |
| `edit-a`, `save-a-signal` | `R3.save-a-bytes` |
| `quit-exit`, `report` | `R3.law2-count`. R3 lists no exit assertion, and its count is the clean exit's report. |
| `edit-sibling`, `save-sibling-signal` | `R5.adjacent-text-and-sibling-save` |
| `open-report`, `reopen-report` | `R5.law2-count` |

`Case.quit()` now returns the reaped status. `R5.open-exit` and
`R5.reopen-exit` compare it, and R3 compares it under `law2-count`.

Two diagnostic negatives were added test-first, outside the gate manifest
like the others. `R3.search-results` expects one match fewer (`8 matches`)
from the completed search. `R5.nonzero-exit` sends SIGKILL by pidfd in place
of Ctrl+Q.

**Receipts.** Every run used AC power (BAT0 `Charging`). The load given is the
one-minute average at the start (M). Runner runs used a scratch copy of
`expectations.json` restricted to the `files` invocation, with the negative
appended to its argv (`build/b5/manifest-*.json`).

| Proof | Code | Exit | Outcome | Case run under `build/` | Load |
|---|---|---|---|---|---|
| `R3.search-results` via run.py | before | 4 | unowned `R3.search-readiness` FAIL | `contract-files-56t046ud` | 12.95 |
| `R3.search-results` via run.py | after | 3 | `R3.results-between-a-and-b` FAIL, step `search-readiness` | `contract-files-s_th5hyi` | 6.12 |
| `R5.nonzero-exit` via run.py | before | 4 | `R5.open-exit` ERROR, `Refused … -9` | `contract-files-8w81of8f` | 12.04 |
| `R5.nonzero-exit` via run.py | after | 3 | `R5.open-exit` FAIL, observed −9, expected 0 | `contract-files-eejby3fa` | 6.12 |
| `R3.snapshot-reply` (case) | after | 4 | `R3.results-between-a-and-b` ERROR, step `search-readiness`, missing reply | `contract-files-8x206yvi` | 6.12 |
| `R3.save-signal` (case) | after | 4 | `R3.save-b-bytes` ERROR, step `save-b-signal`, no `IN_MOVED_TO` | `contract-files-4pndd2q8` | 6.12 |
| plain via run.py | after | 0 | PASS 15 | `contract-files-83zeyf8r` | 10.72 |

The four 6.12 rows ran in parallel on `:161`–`:164`, and the plain run on
`:165` ran beside the two adapter suites. The last two rows supersede the stop
points in the historical `R3.snapshot-reply` and `R3.save-signal` receipts
above. Withheld events still end in ERROR, now under the listed ID that the
step serves.

`directory_watch` and `wait_for_file_event` moved unchanged into
`test_oracle_inspector.py`, beside `Inspector`, because the adapter now arms
a save rename too ([v2-he7.11](v2-he7.11.md)). `files.py` imports them from
there, and its names are unchanged for `cases/navigation.py`.

```sh
python3 -B tests/contract/run.py --manifest build/b5/manifest-R3.search-results.json --role v1 \
    --exe oracle=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite \
    --oracle-build /home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle \
    --displays :160-:169 --out build/b5/runs
python3 -B tests/contract/cases/files.py --build-dir "$B" --display :163 --negative R3.snapshot-reply
```
