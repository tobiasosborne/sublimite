Changed [bench/lineidx_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.48/bench/lineidx_bench.c), [P1.6d.md](/home/tobias/Projects/editor/.wt/edit-4w1.48/docs/decisions/P1.6d.md), and [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.48/src/lineidx/STATUS.md).

Fixes cover fresh G7j requests through viewport submission, independent correctness oracles, fixture validation, enforceable manual cold rows, checked arguments, and bounded waits. Module implementations and frozen headers are unchanged.

Red runs:
```text
SELF_CHECK 15 FAIL fresh_prefix=2 viewport_submitted=0
SELF_CHECK 16 FAIL independent_oracle=0 rejects_tiny_fixture=0
SELF_CHECK 17 FAIL cold_index_miss=0 cold_jump_miss=0 unvalidated_cold_fails=0
SELF_CHECK 18 FAIL rejects_invalid_workloads=0
stranded self-check exit=124 (124 = watchdog killed indefinite wait)
SELF_CHECK 18 FAIL final_publication_adopted=0
```

Green runs, gcc and ASan/UBSan:
```text
SELF_CHECK 15 PASS fresh_prefix=0 viewport_submitted=1
SELF_CHECK 16 PASS independent_oracle=1 rejects_tiny_fixture=1
SELF_CHECK 17 PASS cold_index_miss=1 cold_jump_miss=1 unvalidated_cold_fails=1
SELF_CHECK 18 PASS rejects_invalid_workloads=1
SELF_CHECK 18 PASS bounded_stranded_wait=1 active_deadline=1 retirement_deadline=1
SELF_CHECK 18 PASS final_publication_adopted=1
```

Verification:
```text
make all exit=0
make check exit=0
check: 33 test binaries passed
test_replay_cli: all passed
make fuzz exit=0
fuzz: 19 fuzzers built
```

`make check` passed outside the sandbox after IPC EPERM; LeakSanitizer was disabled.

One fuzzer run requested 120 seconds. It stalled after last logged case 2,020 and reached the 150-second watchdog: exit 124, (M)[AC], load 11.80. This is consistent with existing review §14; no clean fuzz verdict.

Single TRACK attempt:
```text
TRACK_G7_index_1g_fixture_warm n=3 p50=181539501 p99=221425732 ns (M)[AC] load=8.51
TRACK_build_prefaulted n=3 p50=131458162 p99=183812018 ns (M)[AC] load=8.51
```

The attempt exposed the final-publication waiter race, now fixed and tested. It exited 2 before aggregating G7j; no benchmark rerun was made. These timings predate that correction.

Open: G7j aggregate and quiet/cold verdicts remain unvalidated; the existing fuzzer needs its assigned fix. G7j currently pays for a full worker index build; the proposed async seek continuation is documented.