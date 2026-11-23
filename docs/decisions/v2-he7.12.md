# W0.6e · Navigation against the pinned v1 inspector

The lane runs R1 and R6 against the prepared release executable at v1 pin
`6fec2e8c41220e284066ae8479d8f1b573febdb2` with the W0.6d inspector. The
run manifest binds the pin, patch digest and executable digest. The sandbox
checks the executable digest and owns each editor process and profile.
`make check-contract-navigation` invokes the cases directly. Each case
starts one editor once. A mismatch ends the case with FAIL. A missing
event, invalid reply, or broken launch ends it with ERROR. The independent
case can still run, and either status makes the command exit nonzero.

Each run retains `manifest.json`, `results.jsonl`, the source fixtures,
the editor logs and an arrival/action transcript under
`build/contract-navigation-*/`. The transcript includes inspector tickets
and submit serials, the document rectangle, the drawn caret, visible text,
and fixture offsets. The reply and Xvfb readiness watchdog uses the existing
X driver test's 15 s allowance. Case synchronization uses Xvfb readiness,
snapshot replies, process exit and inotify file events. The log fixture's
space check accounts for the navigation file and the full search corpus
before generation. The initial `df -h .` showed 1.0 TiB available (M).

## Derivations

R1 follows the mid-file wrapped case in v1's
`tests/editor_test.c:n6090_typing_top`. The fixture has 128 lines (P, two
halves of 64) with 32 unique six-cell ASCII words per line (P, enough to span
multiple rows in this fixture). Go To Line selects line 65. The wrap model
greedily packs words at the measured width, preserving the
starting byte and text of every visual row. The initial screen labels
identify the model's top row. A second snapshot verifies the entire viewport
and the hit-tested insertion cell on the bottom document row.

The green document measured 80 columns by 42 rows (M). Eleven words fit
across 80 cells, so the 32 words occupy three visual rows (P). In zero-based
document coordinates the visible interval was 192 through 233, with top
label `065w00` and bottom label `078w22`. The click placed the caret three
cells into `078w22`, at byte 17,405 (P, 77 × 224 bytes per full line +
22 × 7 bytes per word and separator + 3 bytes). The model inserts LF at
that byte, wraps again, and advances the top only as far as needed to
include the new caret. That computes top 192 and caret at row 233,
column 0. The settled frame kept the top and painted the suffix `w22`
on the last document row. Ctrl+S produced the exact modeled bytes after
`IN_MOVED_TO`.

R6 matches the 1 GiB size assertion in v1's
`tests/editor_large_test.c:starved_follows`. Seed `w0-r6-log-v1` generates
the body batch labels. The head and last two resident windows contain
individual numbered records. The fixture oracle reads those bytes back
and derives each row start from LF boundaries. Each record occupies 64
bytes by construction, including LF. The last LF contributes the empty
EOF row at byte 1,073,741,824. The first document viewport arrived at
75 columns by 42 rows (M). The runner sent Ctrl+End directly after checking
that snapshot. It did not wait for index publication.

The last 64 KiB (P, v1 `SCROLL_SCAN_BUDGET`) starts at byte 1,073,676,288
and contains 65,536 ÷ 64 = 1,024 records plus the EOF start (P). The
outbound traversal took floor(1,025 ÷ 42) + 1 = 25 pages, or 1,050 rows (P).
The oracle verifies the caret column and a whole viewport of consecutive
fixture labels on every motion, including the path out of the resident
window and back in.

| Action | Expected byte (P, fixture row start plus column) | Label at the caret |
| --- | ---: | --- |
| Ctrl+End | 1,073,741,824 | Empty EOF row after `L16777215` |
| PageUp 25 of 25 | 1,073,674,624 | `L16776166`, 26 records before the last window |
| PageDown | 1,073,677,312 | `L16776208`, column 0 |
| Right | 1,073,677,313 | `L16776208`, column 1 |
| Home | 1,073,677,312 | `L16776208`, column 0 |
| End | 1,073,677,375 | `L16776208`, column 63 |
| Type `z` | 1,073,677,376 | `L16776208` with `z` at column 63 and caret at 64 |

The Right step gives Home a discriminating starting column. The marker
is verified in the painted row and reversed before the search companion.

The companion sends a case-sensitive literal Find in Files query for
`navprobe` over four 256 MiB shards (P, 1 GiB ÷
`FIF_DEFAULT_FILE_BYTES`), byte copies of the navigation fixture in
sequence. Return, Alt+1, and Ctrl+End are sent consecutively. In the green
run both Ctrl+End and the next PageUp returned a correct snapshot before
the last shard's inotify close-after-read event was observable (M). After
switching to Find Results, subsequent snapshots showed the early match
alone, then the final match and the summary `2 matches across 2 files`.
The final match was on shard 3's last record, line 4,194,304
(P, 256 MiB ÷ 64). That witnesses navigation with corpus reading still
outstanding. The timing of initial index completion was unobserved. Wave 2
owns the deliberately held-worker test.

## Receipts and reproduction

The following commands used the already built, stamped oracle. The
release executable SHA-256 was
`ed75336ca0b31baf1c8726e940b4500eafe6f17db73a94bf0fe51ffb084f2195`.
All red controls returned exit 1, kept a single editor PID in the result
records, and stopped at the named predicate. Their power status was
`Charging` [AC] and start load was 4.98 / 6.73 / 8.82 (M, 1, 5, 15 min).
The green run used `Charging` [AC] with start load
3.76 / 6.25 / 8.55 (M).

```sh
oracle=/home/tobias/Projects/sublimite-v1-inspect
make xdriver all
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :170 --negative R1.bottom-setup
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :171 --negative R1.wrapped-return
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :172 --negative R1.return-caret
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :173 --negative R1.saved-bytes
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :174 --negative R1.snapshot-reply
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :175 --negative R6.first-viewport
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :176 --negative R6.tail-navigation
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :177 --negative R6.marker
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/contract-oracle" --display :178 --negative R6.occupied-navigation
make check-contract-navigation ORACLE_TREE="$oracle" CONTRACT_DISPLAY=:179
make all
make check
```

| Control | Altered expectation or transport | Result | Artifact directory under `build/contract-navigation-` |
| --- | --- | --- | --- |
| R1.bottom-setup | Caret one row above the document bottom | FAIL | `nyz0ca7o` |
| R1.wrapped-return | View starts one visual row lower | FAIL | `06kbp_c_` |
| R1.return-caret | Caret one column to the right | FAIL | `0_kiyupt` |
| R1.saved-bytes | Original bytes without LF insertion | FAIL | `cdhmh25b` |
| R1.snapshot-reply | ClientMessage withheld while editor remains live | ERROR | `814_1l5z` |
| R6.first-viewport | Second record at the initial caret | FAIL | `zc8ti31d` |
| R6.tail-navigation | Last numbered record at the EOF caret | FAIL | `emy90c4u` |
| R6.marker | Original row with no marker | FAIL | `gu29hf9v` |
| R6.occupied-navigation | One search match and only the first endpoint | FAIL | `_9rxdezb` |
| R1 and R6 | Standard fixture expectations | 8 PASS, exit 0 | `r78srg2k` |

A live runner control on `:170` wrapped R1 so that only its first
invocation used the wrong Return viewport. It recorded one invocation,
suite exit 1, and the original FAIL receipt in
`build/contract-navigation-rbz7upku/`. The wrapper's own check exited
zero (`build/navigation-first-failure.log`).

An early R1 diagnostic stopped with ERROR when the transcript attempted
to read the non-ASCII modified-tab dot. The reader now selects the
ASCII title span, and that diagnostic's receipt remains in
`build/contract-navigation-smfp0l55/`. The green R1 editor log also
reported four calls within the adapter's allocation guard. Allocation
counts are outside these navigation predicates. The R6 green editor
reported zero. `make all` and `make check` exited zero in this checkout.

The T1–T4 trims leave R1 with a settled frame, and defer the first-action,
wrapped large-file variants and minimap coverage to their owning gates.
This lane uses baked ASCII and carries no latency threshold or worker hold.
The layout check requires no registry rows for Python helpers under
`tests/contract/`.

## W0.7-B3 (2026-11-23): the Law-2 count, retention, publish

Review findings 4, 27 (the R6 half) and 26 (the navigation part) of
`docs/reviews/wave0-code-astra.md`.

**Count (finding 4).** After its scenario, each case parses the editor's
`oracle law2:` line with files.py's checks. A missing report, `active != 1`
or `scopes == 0` is ERROR at `law2-report`. A valid count is attached to
every record of the case as `law2: {allocations, scopes}`. That count is
recorded and **not asserted**, for R6 as well as R1. This departs from the
brief, which asked for R6 to be asserted at 0. The R6 measurement on v1
depends on run order:

| Invocation (all [AC] `Charging`, start load 7.4–17.6 (M, 1 min)) | R6 runs | R6 `allocations` (M) |
| --- | ---: | --- |
| R6 first on a fresh Xvfb (`--case R6`, `--negative R6.*`) | 10 | 4 in all 10 |
| R6 after R1 on the same Xvfb (the runner's `navigation` argv) | 6 | 0 in all 6 |

`--negative R6.tail-navigation` sends only Ctrl+End. It measured 4 with
`scopes=1`, so all four are in the first scope after arrival. R1 measured 4
in every run. Its four are also in the first scope after arrival (the Go To
Line burst, v2-he7.23). The R6 calls are probably the same libxcb
`read_packet` path, but no backtrace was taken, so this is (E). The
"zero for R6" above came from a combined run (the second row). The count rule
in `wave0-contract-design.md` asserts on v1 only where v1 shows 0, and R6's
zero holds only if R1 ran first on the same server. An R6 zero assertion
would couple R6's verdict to case order. R3 and R5 already assert 0 on v1,
which proves the adapter's window. The coordinator may still choose to assert
R6. In that case `--case R6` FAILs on v1.

The static oracle cannot be preloaded or rebuilt here. So the red controls
edited the `oracle law2:` line in `editor.log` after a clean editor exit and
before the case read it. The editing wrapper was a scratch script and is not
committed.

| Injection | Before (unchanged case) | After |
| --- | --- | --- |
| one more allocation | R6 alone: 4 PASS, exit 0, the report (5) is never read | R1+R6: 8 PASS, exit 0, records carry `law2.allocations` 5 (R1) and 1 (R6) |
| line removed, R1+R6 | 8 PASS, exit 0 | 8 PASS + `R1/R6.law2-report` ERROR, exit 4 |
| `active=0`, R1+R6 | 8 PASS, exit 0 | 8 PASS + 2 ERROR, exit 4 |
| `scopes=0`, R1+R6 | 8 PASS, exit 0 | 8 PASS + 2 ERROR, exit 4 |

**Retention (finding 27).** `tail.log` is moved into the case artifacts only
when the R6 interaction raises, which covers FAIL and ERROR. On success the
sandbox removes it with the rest of its root. A run's seed, size and
transcript stay in `snapshots.jsonl` either way. Green
`contract-navigation-ldwcipws/R6/` holds no `tail.log`. Red
`--negative R6.marker` (`0gx8wwg4`) holds the 1 GiB input.

**Publish (finding 26).** `Case.publish` no longer returns the bool that
nothing read.
