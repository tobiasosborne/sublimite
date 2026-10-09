# File module status

P1.7/P1.7b implemented; P1.7c review fixes cover file-1 §§1–5, 7–10.
Published registry generations are pinned through recovery and retired before
unmap/reuse. Previous SIGBUS delivery semantics are preserved; page size and
lock-free atomic checks happen outside the handler. The one process signal
service needs the narrow binding-rule exception proposed in
`docs/decisions/P1.7c.md` (§10), pending coordinator adoption.

Workers publish immutable open/save results; UI queries/mailbox decode install
them. UI owns watches. Saves validate original backing identity, recovery epoch,
and invalidation generation immediately before rename. Temp/target operations
and directory fsync share a retained parent descriptor. Close cancels all
pending file-job completions, including older undrained saves.

Keep refuses changed/faulted mapped backing rather than accepting stale cached
newline counts. Full keep/rebase and immutable retained-generation support need
the piece/consumer design proposal in P1.7c §5; no frozen header was changed.
Other file-1 findings (§6, §§11–32) remain outside this bead. In particular close
still waits, I/O/slices and storage limits retain their earlier limitations,
terminal mailbox saturation and save errno are separate fixes.

Verify with `DISPLAY=:99 EDIT_DISPLAY=:99 make all`,
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check`, `make fuzz`,
and `build/fuzz/file_fuzz -max_total_time=120`. The fuzzer currently checks the
prefix scanner only (stateful file fuzzing is later §30). Run the wrapped tests
from P1.7c for watch ownership, handler sysconf exclusion, and directory-fsync
identity; `FT_CASE=1,2,3,4,5,7,8,9` selects individual regressions. Check the
signal-service symbol limit with `src/file/check_signal_globals.py` via Python.
TSan polling/watch tests pass in a standalone GCC `-fsanitize=thread -no-pie`
build (`FT_CASE=3`); the PIE executable aborted at startup with “unexpected
memory mapping”. See P1.7c for the build sources/flags.

Release and ASan/UBSan file tests and all wrapped regressions pass. `make all`
passes; `make fuzz` builds 19 fuzzers. File fuzz: 9,941,893 executions in 121 s,
(M)[AC, Not charging; load1=7.58], exit 0, requested 120 s minimum.
The full check suite passes outside the local-socket sandbox restriction:
`check: 33 test binaries passed`; `test_replay_cli: all passed`. LSan rerun
remains coordinator work because this sandbox uses `detect_leaks=0`.
The module bench ran once, exit 0, TRACK (M)[AC, Not charging; load1=5.46].
Its stamped rows are in P1.7c; loaded-box values are not gate verdicts.
