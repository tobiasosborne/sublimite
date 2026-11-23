# W0.6g · R31 process-death recovery controls

## Tier A fixture

The stock oracle is v1 commit `6fec2e8` at `/home/tobias/Projects/sublimite-v1-oracle/build/sublimite`. The sandbox manifest binds role `v1` and the absolute executable path to SHA-256 `64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115`. The sandbox checks the pin on every launch. Each case owns an authenticated Xvfb on the explicitly selected display in `:150`–`:159`, with `-noreset -nolisten tcp`, a private authority file and a recorded server PID. The run closes its server and sandbox and retains the receipt and observations under `build/recovery-*`.

- **File tab.** Write the source fixture, sync the file and its parent directory, and read back the same bytes. Open it and verify the active buffer over the clipboard. Restart with the source path to select the recovered file by identity or reopen a clean file omitted from the session.
- **Untitled tab.** Open the same source as a sibling, create a new tab and enter the distinct nonempty seed with ordinary key events. Verify it by copying, then perform one preparatory hot exit and restart to prove the seed survives. The scored paste occurs after that baseline is verified. The crash relaunch has no file arguments, so it tests the recovered active untitled tab. An empty or missing tab cannot satisfy the seed expectation.
- **Scored edit.** Move to the end of the verified buffer, arm the editor pidfd and send one `Ctrl+V` with a unique ASCII marker supplied by the driver's clipboard. The driver returns after its checked XTest and UngrabServer requests have flushed through X. The next action sends `SIGKILL` via the pidfd. The runner does not wait for clipboard consumption or any application acknowledgment. Reap with `waitpid`, require `WIFSIGNALED` and `WTERMSIG == SIGKILL`, then use `Sandbox.restart()` in the same case root and require a distinct PID.
- **Independent observations.** Copy the recovered buffer with one Select All and Copy. The driver preclaims the clipboard with a sentinel and requires the bound editor window to take ownership before reading exact `UTF8_STRING` bytes. Compare against the literal seed or the seed concatenated with the full marker. Check the source bytes independently before launch, after SIGKILL and after the recovery observation. A missing source is an assertion failure. The copied buffer, source observations, editor logs, wait status, PIDs and JSON receipt are retained. A live restarted editor has a null return code at the observation point.

The X command watchdog is inherited from the driver fixture (15 s per X operation and 30 s for the wrapper, see [W0.6b](v2-he7.9.md)). The clipboard ownership poll has a 10 ms interval to cap idle observation traffic at 100 queries per second (P, reciprocal of the interval). The poll observes one Copy without sending it again. These watchdogs apply to setup and readback. The scored paste has no application wait before the signal. Exit statuses are `PASS` 0, `FAIL` 3 and `ERROR` 4, one each. Python keeps 1 for an uncaught exception and argparse keeps 2 for a usage error, so neither can be read as a case result. The pre-ACK receipts below predate this and used `FAIL` 1 and `ERROR` 2. Each selected case runs once and the invocation stops on its first failure.

## Red receipts

The synthetic prefix and the deliberately wrong expected bytes both reach `recovered-buffer` and exit 1. The first runs are `recovery-jz_mpbnb` and `recovery-kjhlch58`. The same commands on the final harness produced `recovery-1iwwhphx` and `recovery-dns1yujv`. The latter observed the legal original file bytes after the SIGKILL and failed against the deliberately wrong pair. Neither negative control reproduces a defect in the oracle.

```sh
make xdriver
python3 -B tests/contract/cases/recovery.py --case torn-prefix
python3 -B tests/contract/cases/recovery.py --oracle /home/tobias/Projects/sublimite-v1-oracle/build/sublimite --display :150 --case pre-ack-file --wrong-expectation
```

An initial green command stopped with an Xvfb readiness `ERROR` (`recovery-910jluz4`). The readiness pipe produced `150` before its newline. The corrected reader accumulates the whole line within the original deadline. The error receipt is retained. The measurements below use the corrected harness.

## Green receipts

Four invocations each exercise both tab kinds. Two separate authenticated displays exercise independent display namespaces. Two invocations per display establish that each tab setup works again on a fresh sandbox and server in the same namespace. The count was fixed before sampling and was not extended to obtain a whole-edit outcome.

```sh
make check-contract-recovery ORACLE=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite RECOVERY_DISPLAY=:150
for display in :151 :150 :151; do
    make check-contract-recovery ORACLE=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite RECOVERY_DISPLAY="$display" || exit "$?"
done
```

| Tab | Pre-edit | Whole-edit | Illegal | Runs with unchanged source |
|---|---:|---:|---:|---:|
| File | 4 | 0 | 0 | 4/4 |
| Untitled | 4 | 0 | 0 | 4/4 |

Only the pre-edit outcome was observed. Every scored child was reaped with raw wait status `9` (`WIFSIGNALED`, `SIGKILL`). The table gives measured (M) `/proc/loadavg` values at case start on 2026-11-23, using the 1-, 5- and 15-minute fields reported by Linux. `/sys/class/power_supply/BAT0/status` read `Discharging` ([bat]) in all eight runs and in the red and waiting controls. Full load strings are in each receipt.

| Receipt under `build/` | Tab | Display | Killed → restarted PID | Load average |
|---|---|---|---|---|
| `recovery-xhs_9mz9` | File | `:150` | 105374 → 105386 | 10.58 / 10.16 / 7.73 |
| `recovery-gol6reh4` | Untitled | `:150` | 105416 → 105428 | 10.58 / 10.16 / 7.73 |
| `recovery-vqdnfrph` | File | `:151` | 105524 → 105536 | 10.26 / 10.11 / 7.74 |
| `recovery-7p4saul7` | Untitled | `:151` | 105675 → 105754 | 10.08 / 10.07 / 7.74 |
| `recovery-g74bnwy4` | File | `:150` | 105810 → 105825 | 10.08 / 10.07 / 7.74 |
| `recovery-1ygzk71b` | Untitled | `:150` | 105864 → 105878 | 10.08 / 10.07 / 7.74 |
| `recovery-j5cnvkcl` | File | `:151` | 105914 → 105935 | 10.08 / 10.07 / 7.74 |
| `recovery-j0jyg7j0` | Untitled | `:151` | 105973 → 105988 | 9.83 / 10.02 / 7.74 |

## Post-ACK controls

The cases `post-ack-file` and `post-ack-untitled` reuse the Tier A setup above, with three changes.

- **Target.** The inspected v1 build `/home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle/sublimite` is selected with `--build-dir`. Its stamp must match the pin and the patch SHA-256 `54bcf05f…fda1a`, and the sandbox pins the executable digest `ed75336c…2195` from that stamp. Every editor launch in these cases is inspected (`--inspect-fds`). Xvfb runs at `640 × 480 × 24`, the geometry the snapshot mapping capacity is derived from in [W0.6d](v2-he7.11.md). The pre-ACK cases keep the stock oracle and the default screen.
- **Acknowledgment witness.** After Ctrl+End, a SNAP settles the pre-paste frame and fixes its serial as the lower bound. The driver owns the marker, sends Ctrl+V and then a NEXT marker with that bound. X delivers the marker after the paste keys. The runner blocks on the reply socket and the editor's pidfd together, under the 15 s watchdog. **The kill goes out as soon as the reply packet arrives.** The frame is decoded after the reap, from the sealed memfd, which the dead editor can no longer write. Scoring is conditional on the witness, so this is equivalent to "check, then kill" and leaves no decode time between acknowledgment and SIGKILL. The witness requires the frame's document cells to draw the run's random token exactly once (`[` + mkdtemp suffix + `]`). Only the paste places that token, and a paste is one command, so a drawn token means the whole edit is drawn. Counting tokens instead of matching one row tolerates a soft wrap at the space. A missing token is `ERROR` at `submitted-action-frame`. A missing reply, an editor exit while waiting, or a watchdog expiry is also `ERROR`.
- **Why the witness frame is natural.** The adapter answers NEXT from `on_submit` only once `prefix_done` holds: no queued input, no open journal group or pending journal metadata, and no clipboard transfer in flight. The single forced repaint (`editor_inspect_repaint`) is gated on `!e->dirty`. Applying the paste sets `dirty`, which only a submit clears. A forced repaint can therefore only redraw a buffer state an earlier natural frame already submitted, never be the first frame to show it. So a reply whose cells hold the token proves that a natural submitted frame showed the whole paste. In the series every witness serial was the bound + 1, the next submitted frame. The untitled red controls saw a gap (2 → 4), so the runner records the serial and does not require + 1.

After the reap (`WIFSIGNALED`, `WTERMSIG == SIGKILL`) and the source check, the restart and readback are the same as pre-ACK. The only legal outcome is the exact whole-edit bytes, `before + marker`. Anything else is `FAIL` at `recovered-buffer`.

### Law-2 count

SIGKILL discards the killed editor's in-process count, because the adapter prints it only at a clean close. Reading it any earlier would delay the kill. The restarted editor's Select All + Copy opened no scope (`scopes=0` on the first green attempt, `recovery-e63_9f48`), so that count measures no window, and the runner rejects it as vacuous (`ERROR`). The runner therefore repeats the scored command in the restarted editor as a twin. It runs Ctrl+End, the same marker bytes, Ctrl+V and a NEXT witness drawing the token twice. It then sends SIGTERM, which v1's signalfd turns into an ordinary close, and parses `oracle law2: active=… allocations=… scopes=…`. In the untitled case the clean seed hot exit also reports its typing window. A count needs `active=1` and `scopes > 0`, or it is `ERROR` (fail closed). v1 measured 0 on the first green runs, so under the [count rule](wave0-contract-design.md#law-2-count-rule-for-inspected-cases-advisor-2026-11-23) 0 is **asserted** on v1 and a nonzero count is `FAIL`. Across the 16 post-ACK runs (M): twin windows `allocations=0` with `scopes=2` in 14 runs and `scopes=3` in two (`recovery-fvt8aad2`, `recovery-b5nlb3y4`). The untitled seed windows were `allocations=0`, `scopes=2` in all 8.

### Run count

Each tab kind runs **8** times, one first attempt per permitted display `:152` through `:159` (P, 159 − 152 + 1). This follows the per-display derivation of [W0.6d](v2-he7.11.md). Each invocation runs `RECOVERY_CASE=all`, so the same fresh Xvfb namespace also re-runs both pre-ACK cases. The count was fixed before sampling. With 0 illegal outcomes in 8 runs, the exact one-sided 95 % upper bound on a per-run illegal-outcome probability is 1 − 0.05^(1/8) = 0.312 (P, binomial). The series characterizes the harness and oracle. It is not a flake budget.

### Red receipts

```sh
B=/home/tobias/Projects/sublimite-v1-inspect/build/contract-oracle
python3 -B tests/contract/cases/recovery.py --build-dir $B --display :153 --case post-ack-file --wrong-expectation      # exit 3
python3 -B tests/contract/cases/recovery.py --build-dir $B --display :153 --case post-ack-file --decoy-paste            # exit 4
python3 -B tests/contract/cases/recovery.py --build-dir $B --display :153 --case post-ack-untitled --wrong-expectation  # exit 3
python3 -B tests/contract/cases/recovery.py --build-dir $B --display :153 --case post-ack-untitled --decoy-paste        # exit 4
python3 -B tests/contract/cases/recovery.py --oracle /home/tobias/Projects/sublimite-v1-oracle/build/sublimite --display :154 --case pre-ack-file --wrong-expectation  # exit 3
python3 -B tests/contract/cases/recovery.py --case torn-prefix  # exit 3
```

| Receipt under `build/` | Control | Result | Assertion | Observed |
|---|---|---|---|---|
| `recovery-8etfu90b` | file, wrong expectation | FAIL (3) | `recovered-buffer` | `durable source bytes [8etfu90b]`, the whole edit |
| `recovery-ojmqug7d` | file, decoy paste | ERROR (4) | `submitted-action-frame` | frame drew ` [decoy]`, no token |
| `recovery-9ks8lpsg` | untitled, wrong expectation | FAIL (3) | `recovered-buffer` | `untitledseed [9ks8lpsg]`, the whole edit |
| `recovery-zazhm1ae` | untitled, decoy paste | ERROR (4) | `submitted-action-frame` | frame drew ` [decoy]`, no token |
| `recovery-oenybqxk` | pre-ACK file, wrong expectation | FAIL (3) | `recovered-buffer` | `durable source bytes`, the pre-edit bytes |
| `recovery-zieuo3bt` | synthetic torn prefix | FAIL (3) | `recovered-buffer` | half marker |

The decoy paste puts ` [decoy]` on the clipboard in place of the marker. The editor acknowledges a real, different edit, the witness predicate finds no token, and the run stops at `ERROR` before any scoring. The first wrong-expectation run (`recovery-too0204w`, exit 3) and the first decoy run (`recovery-t_dnmt96`, exit 4) came before the Law-2 twin was added. Before this change the post-ACK skeleton exited `ERROR` "waiting" (`recovery-sp7qt59u`).

### Green receipts

At series start `/sys/class/power_supply/BAT0/status` read `Charging` ([AC]) and `/proc/loadavg` read 0.45 / 1.73 / 3.85 (M, 1-, 5- and 15-minute). Every receipt read `Charging`, with the 1-minute load between 0.45 and 0.81. The full strings are in each `result.json`.

```sh
for d in :152 :153 :154 :155 :156 :157 :158 :159; do
  make --no-print-directory check-contract-recovery ORACLE=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite \
    ORACLE_TREE=/home/tobias/Projects/sublimite-v1-inspect RECOVERY_DISPLAY="$d" RECOVERY_CASE=all \
    > build/series-${d#:}.log 2>&1 || break
done
```

| Tab | Case | Pre-edit | Whole-edit | Illegal | Runs with unchanged source |
|---|---|---:|---:|---:|---:|
| File | pre-ACK | 8 | 0 | 0 | 8/8 |
| Untitled | pre-ACK | 8 | 0 | 0 | 8/8 |
| File | post-ACK | — (illegal) | 8 | 0 | 8/8 |
| Untitled | post-ACK | — (illegal) | 8 | 0 | 8/8 |

Every scored child was reaped with raw wait status `9`.

| Display | post-ack-file | Witness serial | post-ack-untitled | Witness serial |
|---|---|---|---|---|
| `:152` | `recovery-7iorl0js` | 2 → 3 | `recovery-1uyugby_` | 3 → 4 |
| `:153` | `recovery-6jf7o2h4` | 2 → 3 | `recovery-z3lme32p` | 2 → 3 |
| `:154` | `recovery-449c7rw0` | 2 → 3 | `recovery-hktp0ol4` | 3 → 4 |
| `:155` | `recovery-exsyrisn` | 2 → 3 | `recovery-xzwat3zu` | 3 → 4 |
| `:156` | `recovery-fvt8aad2` | 2 → 3 | `recovery-d5dah0sx` | 2 → 3 |
| `:157` | `recovery-6j_k8btl` | 3 → 4 | `recovery-b5nlb3y4` | 3 → 4 |
| `:158` | `recovery-ufyleh7z` | 3 → 4 | `recovery-u2rmxz5m` | 3 → 4 |
| `:159` | `recovery-ckhs6ba1` | 2 → 3 | `recovery-9a6h3puy` | 2 → 3 |

Pre-ACK receipts in the same invocations, file then untitled: `:152` `recovery-ix0doihf` / `recovery-hk5t3kd4`, `:153` `recovery-xgc8wgm5` / `recovery-k8li6v2q`, `:154` `recovery-pzqxb_jv` / `recovery-jfdkmo9o`, `:155` `recovery-twfza1q3` / `recovery-pzv2nfst`, `:156` `recovery-en680cj5` / `recovery-r4ellv9k`, `:157` `recovery-p6bv6lhu` / `recovery-zoveezt8`, `:158` `recovery-56eknb1d` / `recovery-h9h6lvge`, `:159` `recovery-r9v6_8jr` / `recovery-akohq8af`. All 16 were pre-edit. The shorter marker (`[suffix]` in place of `[paste recovery-suffix]`) and the exit-status remap apply to them as well.

### Waits

Every wait in the post-ACK path is on an event. The snapshot replies wait on the socket and pidfd. The reaps wait on the pidfd. The untitled seed's hot exit now uses a pidfd wait (`navigation.wait_exit`) instead of `Popen.wait(timeout)`, which polls. Two pre-existing polls remain, untouched in this change. The clipboard-ownership readback still sleeps 10 ms between `owner` queries, because the driver exposes no selection-owner event. The Xvfb teardown still uses `Popen.wait(timeout)`.

The dedicated make target is separate from the regular build and check. Both passed in this checkout:

```sh
TMPDIR="$PWD/build" make all > build/recovery-make-all.log 2>&1 && TMPDIR="$PWD/build" make check > build/recovery-make-check.log 2>&1
```
