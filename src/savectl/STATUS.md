# savectl — edit-457.7 / P4.7

Standalone controller implemented in savectl.h/c; editor/main/file/journal and
Makefile remain untouched. Design and Integration contract: docs/decisions/P4.7.md.

Done: nonblocking snapshot/enqueue/save status; revision-aware modified bit;
worker durable file core; off-UI journal prepare/finish with complete-session
checkpoint leases; modified conflict banner/keep/reload; automatic unmodified
reload and latest/clamped view offsets; mutation/event validation at installation;
immutable private reload backing and UI-only tree metadata construction;
mailbox-full completion fallback; logical close and deferred cleanup.

Current integration constraints: real file MMAP needs a worker-safe retained
original/fault-epoch guard and actual-open baseline accessor that file.h does
not yet expose. Unguarded MMAP save/keep is refused, never silently unsafe.
Reload uses private copy memory, not the mmap G10f bound. Journal must use an
exclusive session lease and its own private pool distinct from savectl's pool;
caller defers journal operations while leased and builds complete checkpoints.
Failed retained transactions need the session recovery resolver before replacing
this controller. No watched-file/event-loop wiring is performed in this bead.
These accessors/workflows are proposed to edit-4w1.42 / edit-4w1.53 in the decision.

Verify (always DISPLAY=:99 EDIT_DISPLAY=:99):

```
make all
ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test
ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=30 -max_len=64
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
build/bench/savectl_bench           # TRACK by default, --gate for verdict run
```

Optional UI IO interposition verification (without Makefile changes):

```
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE \
 -pthread -O2 -Isrc -include tests/display_guard.h -DSAVECTL_IO_WRAP \
 tests/savectl_test.c build/libedit.a \
 -Wl,--wrap=stat,--wrap=fstat,--wrap=open,--wrap=openat,--wrap=fsync,--wrap=pread,--wrap=close,--wrap=work_publish \
 -lm -ldl -o /tmp/savectl-io-test
DISPLAY=:99 EDIT_DISPLAY=:99 /tmp/savectl-io-test
```

Red evidence: first link failed on absent savectl symbols; behavioral red:
`file_error==FILE_ERR_IO` failed, then source-stop cause preservation fixed it;
`pthread_equal(pthread_self(),*owner)` failed, then private-byte publication and
UI metadata installation fixed it. Release/sanitizer/interposition tests pass.
Final release build: `make: Nothing to be done for 'all'.` (exit 0).
Final fuzz build: `fuzz: 22 fuzzers built` (exit 0).
Final module ASan/UBSan and wrapped IO/publication tests: `savectl_test: ok`.
Final seeded model fuzz: `Done 1151 runs in 31 second(s)` (M)[AC],
Not charging, load1=5.39. Publication ordering also has a deterministic red/green
wrapper test holding the worker after its mailbox wake.
Full suite requires the existing Xvfb :99 outside this sandbox's socket namespace;
the initial sandbox CLI failure was environmental, with no source change.
Final full suite (exit 0): `check: 42 test binaries passed`;
`test_replay_cli: all passed`. ASAN_OPTIONS=detect_leaks=0 throughout;
the coordinator still needs the leak-enabled rerun.

Single TRACK bench run complete, stamps/timings in P4.7.md. G8s within the recorded
budgets; G8d exceeds them under shared load. No quiet-box verdict and no rerun.
