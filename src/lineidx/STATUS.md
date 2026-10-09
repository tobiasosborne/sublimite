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
# lineidx status — edit-4w1.46 / P1.6b

Implemented review §1–5, §12 and §14 in the allowed lineidx/test/fuzz files.
See `docs/decisions/P1.6b.md` for verdicts and red/green evidence.

- Hard maximum of 64 KiB per nonempty entry; edit preflights overflow and
  storage before cancellation/mutation. Capacity refusal leaves the model
  unchanged and returns `-1`; `lineidx_create_reserved` reserves edit room on
  the allocating path. Accepted paste refresh scans one chunk per call and
  leaves the remaining chunks approximate for later refresh/background build.
- Supported logical lengths are at most `UINT64_MAX - 1`, subject to allocation
  size checks and allocation success; synthetic input checks counts above 32 bits.
- Epoch wrap no longer acknowledges a running/cancelled lease. Deterministic
  barrier tests hold immutable source copies until the last worker access.
- Approximate line queries/seeks always return a real line start; fuzz assertions
  cover that contract, including unterminated EOF and a prefix ending in a line.
- Owned memory includes capacity, object/summary/job storage and declared source
  bytes (`lineidx_build_start_owned`). Unknown release-hook ownership reports
  `SIZE_MAX`. One active/retiring lease per index; restart returns `-1` while
  retirement is incomplete. Tests independently observe ASan allocator bytes.
- Fuzz/test harnesses drain work mailboxes, require successful builds and use
  bounded completion waits. A persistent replay checks beyond the pool's slots.

Review §13 is confirmed and **not fixed**: `res[]`/`done_n` remain a private
cross-thread result channel. A selective mailbox receive API and lease-complete
API in `src/work` are proposed in P1.6b; changing another module is forbidden
for this bead. `lineidx_test --review=13` is the deliberately failing opt-in
reproducer, excluded from the normal passing suite.

Review §6–11 (query budgets, UI slices, metadata passes and cancellation), and
benchmark corrections remain with later beads. In particular, destroy can
still wait behind an unrelated queued bulk job and poll/derive/refresh metadata
walks remain unbounded. The benchmark is unchanged; its undrained mailboxes,
oracle/gate endpoints and cold-mode limitations remain unvalidated.

Verification (final results to be recorded after completion):

```
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  build/fuzz/lineidx_fuzz -max_total_time=120 -timeout=20 -max_len=2048 <scratch corpus>
```

Run the module bench once at the end, with a power/load stamp, as TRACK only.
The coordinator reruns sanitizer leaks and gate verdicts outside this sandbox
on a quiet box. Socket/display tests may require the socket-capable execution
permission; they must keep Xvfb :99.
