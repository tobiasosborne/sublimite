# keys — P4.3 / edit-457.3

Implemented: pinned Sublime Text build 4200 Linux normal-editor keymap as a
const table; action IDs and fixed-size arguments; binary search; caller-owned
Ctrl+K/Ctrl+J chord state; XKB normalization and explicit unavailable-action
flags. Independent per-binding tests, release malloc guard, mixed-event
benchmark, and sequence fuzzer are included.

Execution remains with the editor/view side. UI/grammar/snippet/panel context
overrides, config.toml overrides, arbitrary-layout base-symbol recovery, and
executors for flagged actions belong to subsequent beads. Exact scope,
reference hash, and proposals are in `docs/decisions/P4.3.md`.

Verify on the safe display:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/keys_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/keys_bench
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  build/fuzz/keys_fuzz -max_total_time=60 -max_len=4096
```

Benchmark gate: p99 <= 1 us (G), zero mallocs (G); shared-machine numbers are
TRACK only. LeakSanitizer is disabled for sandbox checks; the coordinator must
repeat with leaks enabled.

Validation: gcc `make all` passed; clang `make fuzz` built all fuzzers; release
and ASan/UBSan keys tests passed. Release coverage is 256 independent binding
rows and 20,000 guarded lookups with zero mallocs (M)[AC], load1=3.35.
Final fuzz: 222,562 inputs in 61 seconds (M)[AC], load1=3.35, no findings.
Benchmark: p50=0.037 us, p99=0.104 us, zero mallocs (M)[AC], load1=4.02,
TRACK only, against p99<=1 us (G).

The sandbox suite initially failed at existing x11_stall_test because :99
socket access returned EPERM. With socket access, the full `make check` passed:
`check: 26 test binaries passed` and `test_replay_cli: all passed`, with
DISPLAY/EDIT_DISPLAY=:99 and leak detection disabled. No unrelated source
changes were needed. Red/green output and benchmark evidence are preserved in
`docs/decisions/P4.3.md`.
