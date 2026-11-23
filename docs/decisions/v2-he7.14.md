# W0.6g · R31 process-death recovery controls

## Tier A fixture

The stock oracle is v1 commit `6fec2e8` at `/home/tobias/Projects/sublimite-v1-oracle/build/sublimite`. The sandbox manifest binds role `v1` and the absolute executable path to SHA-256 `64c27b03f20f51abbfc512fc0b5441883e2d2b8f1ca916b9061c6eb18ca71115`. The sandbox checks the pin on every launch. Each case owns an authenticated Xvfb on the explicitly selected display in `:150`–`:159`, with `-noreset -nolisten tcp`, a private authority file and a recorded server PID. The run closes its server and sandbox and retains the receipt and observations under `build/recovery-*`.

- **File tab.** Write the source fixture, sync the file and its parent directory, and read back the same bytes. Open it and verify the active buffer over the clipboard. Restart with the source path to select the recovered file by identity or reopen a clean file omitted from the session.
- **Untitled tab.** Open the same source as a sibling, create a new tab and enter the distinct nonempty seed with ordinary key events. Verify it by copying, then perform one preparatory hot exit and restart to prove the seed survives. The scored paste occurs after that baseline is verified. The crash relaunch has no file arguments, so it tests the recovered active untitled tab. An empty or missing tab cannot satisfy the seed expectation.
- **Scored edit.** Move to the end of the verified buffer, arm the editor pidfd and send one `Ctrl+V` with a unique ASCII marker supplied by the driver's clipboard. The driver returns after its checked XTest and UngrabServer requests have flushed through X. The next action sends `SIGKILL` via the pidfd. The runner does not wait for clipboard consumption or any application acknowledgment. Reap with `waitpid`, require `WIFSIGNALED` and `WTERMSIG == SIGKILL`, then use `Sandbox.restart()` in the same case root and require a distinct PID.
- **Independent observations.** Copy the recovered buffer with one Select All and Copy. The driver preclaims the clipboard with a sentinel and requires the bound editor window to take ownership before reading exact `UTF8_STRING` bytes. Compare against the literal seed or the seed concatenated with the full marker. Check the source bytes independently before launch, after SIGKILL and after the recovery observation. A missing source is an assertion failure. The copied buffer, source observations, editor logs, wait status, PIDs and JSON receipt are retained. A live restarted editor has a null return code at the observation point.

The X command watchdog is inherited from the driver fixture (15 s per X operation and 30 s for the wrapper, see [W0.6b](v2-he7.9.md)). The clipboard ownership poll has a 10 ms interval to cap idle observation traffic at 100 queries per second (P, reciprocal of the interval). The poll observes one Copy without sending it again. These watchdogs apply to setup and readback. The scored paste has no application wait before the signal. A result of `FAIL` exits the Python runner with code 1 and `ERROR` with code 2. Each selected case runs once and the invocation stops on its first failure.

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

## Waiting on v2-he7.11

The Tier B post-ACK controls for both tabs require the snapshot cells section. After pasting, wait for a naturally submitted action frame containing the unique marker, kill immediately, restart in the same root and require exact whole-edit bytes and the unchanged source. The current skeleton exits 2 with `ERROR` and `waiting: requires snapshot adapter` at `submitted-action-frame` (`recovery-sp7qt59u`).

```sh
python3 -B tests/contract/cases/recovery.py --case post-ack
```

The dedicated make target is separate from the regular build and check. Build and check were run with checkout-local temporary storage.

```sh
TMPDIR="$PWD/build" make all > build/recovery-make-all.log 2>&1 && TMPDIR="$PWD/build" make check > build/recovery-make-check.log 2>&1
```
