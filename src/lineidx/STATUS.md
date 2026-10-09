# lineidx status

P1.6d / edit-4w1.48 fixes bench review §15–18 only. Implementation and frozen
headers are unchanged; implementation fixes belong to edit-4w1.46 in parallel.

Done: fresh unbuilt G7j request through bulk build, sliced 80×24 layout and null
submit/present; full-build byte-work checks per sample; independent scalar line,
target-offset and viewport-cell oracles; validated default fixture size/count;
alternate fixture TRACK labels; explicit manual per-sample verified cold mode;
checked CLI, monotonic wait deadlines, nonzero completion errors and mailbox
consumption. See `docs/decisions/P1.6d.md` for all red/green lines and proposals.

Missing: optimized asynchronous seek continuation (current G7j builds the full
index before layout); quiet target-A performance verdicts; verified root-evicted
cold measurements; real backend/display end-to-end validation. Existing module
and fuzzer findings outside §15–18 are not resolved by bench changes. Inherited
cancel and reported-memory rows do not establish §9/§12 contracts.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99:
- `make all`
- `ASAN_OPTIONS=detect_leaks=0 make check` (coordinator rechecks leaks on)
- `make fuzz`, then `build/fuzz/lineidx_fuzz -max_total_time=120`
- `build/bench/lineidx_bench --self-check` (untimed contract checks)
- One loaded-box run: `build/bench/lineidx_bench --track --reps=3`, stamped with
  BAT0 status and the 1-minute load. No quiet gate verdict from TRACK numbers.
- Quiet warm gate run: omit `--track` (exit 3 if cold rows remain unvalidated).
- Quiet cold rows: `--cold --reps=3 --timeout-ms=600000`; manually evict and enter
  EVICTED before every sample, following the decision doc. Never use display :0.

Current verification: `make all` exit 0; new bench self-checks pass gcc and clang
ASan/UBSan (`detect_leaks=0`); `make fuzz`: 19 fuzzers built. The unchanged lineidx
fuzzer stalled with a 120-second requested budget and reached the 150-second
watchdog (exit 124, last logged case 2,020), consistent with review §14. It has no
clean fuzz verdict here. Sandboxed `make check` stopped on IPC EPERM; the identical unrestricted
DISPLAY=:99 suite passed: 33 test binaries and replay CLI, exit 0.

One TRACK attempt (`--track --reps=3`, [AC], initial load 8.73) independently
confirmed target byte 966366840. It recorded G7 warm 181.540/221.426 ms and
prefaulted build 131.458/183.812 ms (M)[AC], load 8.51, then stopped on a final
publication/adoption race before aggregating G7j (exit 2). The waiter race is
fixed with a deterministic red/green self-check, passing gcc and ASan/UBSan.
No second performance run was made. These timings predate the final correction;
G7j/cold and quiet-box verdicts remain unvalidated. Full details: P1.6d.md.
