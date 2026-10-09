# P4.14 / edit-457.23 — real-display-capable startup harness

Done: two-key real-display opt-in (`--real-display` plus caller-supplied exact
`EDIT_ALLOW_REAL_DISPLAY=1`); matching nonempty display validation in the script,
driver and every candidate; safe :99 default; `--launches N` (run default 200,
self-check default two, bounded 1..10000); flushed duration estimate before
service startup; strict native GL for real B and D, explicit :99 probe/fallback;
A normal CPU / B warm GL / C warm CPU interleaving, separate D normal GL exec;
root-sized real viewport and actual sample dimensions; GL renderer/vendor/driver
version identity; separate original-frame CPU readiness, device fence and native
Present validation; startup cost, RSS/PSS/high-water/faults, hidden idle CPU/
context switches and separate after-idle warm launches; arbitrary-count audits;
pre-fork failure/private-runtime cleanup; rejection and endpoint unit contracts.
No production module, frozen header, Makefile, service or adoption change.

Decision/reproduction: [P4.14b harness](../../docs/decisions/P4.14.md#p414b-harness).
The exact coordinator-only announced real-display command is recorded there.
The worker used only :99 and never set the permission variable. Real native
execution/driver identity still requires the coordinator batch. Native init
maps briefly to verify Present, then hides the warm server before IPC readiness.

Verify from repo root (local Xvfb/IPC sockets must be available):

```sh
env DISPLAY=:99 EDIT_DISPLAY=:99 make all
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
env DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
python3 variants/P4.14/test_cli.py --built
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -Isrc variants/P4.14/contract_test.c -o /tmp/zygote-contract-test
/tmp/zygote-contract-test
env DISPLAY=:99 EDIT_DISPLAY=:99 tools/zygote_bench.sh --run --launches 2
python3 variants/P4.14/audit.py variants/P4.14/p4.14b-final-dry-run.log
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 CC=clang ZYGOTE_SAN=1 tools/zygote_bench.sh --self-check --launches 2
python3 variants/P4.14/audit.py
```

The smoke commands print to stdout; redirect a new log and audit that path when
reproducing. Run release/sanitizer builds sequentially: generated executable
paths are shared. Do not rerun benches for a better load.

Verified: GCC make all; clang ASan/UBSan make check (47 binaries and replay CLI);
make fuzz (24 built); IPC decoder smoke without findings; release and sanitizer
endpoint units (including startup-failure cleanup); independent script/driver/
A/B/C/D permission refusal tests; release dry run and sanitizer self-check;
modern and retained-historical raw audits. Leak detection was disabled for the
sandbox, and native GL is explicitly unsupported on Xvfb.

Evidence: p4.14b-red.log, p4.14b-endpoint-red.log, p4.14b-green.log,
p4.14b-verification.txt; final release p4.14b-final-dry-run.log / p4.14b-audit.txt;
sanitizer p4.14b-sanitizer-self-check.log. Earlier p4.14b-dry-run.log preceded
new diagnostics. p4.14b-excluded-mixed-build.log is invalid because release and
sanitizer builds overlapped; it is excluded. Historical results.log/audit.txt
remain the prior experiment's retained evidence. The final cleanup-only change
was verified with both unit compilers after the launch smoke; no timing path
changed and no timing matrix was repeated for it.

Missing by design: actual native real-display results; complete shipped CLI
file/session/journal workload; external effects condition, on-glass/compositor
observation, GPU power/owned graphics peak, G10 ownership and focused/unfocused
G11 blink; production autostart/lifecycle policy; root-only cold-cache rows.
The runner labels these as SKIP. TRACK smoke counts do not establish gate
verdicts. Standalone variant STATUS covers this module; src STATUS files remain
unchanged because no production module was modified.
