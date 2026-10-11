# edit-d50 continuation — daily-use keys

The remaining daily-use executors now include file-language toggle comment,
go-to-line preview/accept/cancel, Ctrl+D first-word selection, Ctrl+K Ctrl+C
centering and Ctrl+K Ctrl+D single-selection literal skipping. The existing
indent/unindent, duplicate/delete/move/join, case, transpose and mark/kill/yank
fixtures still pass. New action and independent key-capability fixtures were
run red before implementation, then green in release and ASan/UBSan builds.

No typing-path malloc: plans reuse prepaid staging and bounded stack windows;
the editor owns prompt storage before input, and exact index seeks continue
through existing bounded calls. `KEYS_ARG_SINGLE_SELECTION` identifies partial
occurrence capabilities explicitly. Further Ctrl+D expansion, syntax reindent,
clipboard history UI, embedded/custom comment scopes, folding/diff/panes and
oversized edit/skip plans remain unsupported. Unknown/plain text has no comment
syntax. The full bead is still incomplete for complete Sublime parity.

`make all`, the focused release/sanitizer checks, the headless module sweep
and the view fuzzer corpus replay passed. Release guarded typing and command
fixtures allocate no libc storage. See the
[worker report](../../docs/worker-reports/edit-d50.md) and
[decision audit](../../docs/decisions/edit-d50.md) for exact evidence and limits.
Standalone IPC still fails with sandbox EPERM at socket initialization.
The coordinator must rerun native display/IPC/leak checks and the comparison
drive; no display suites were executed in this continuation.

---

# edit-d50 — default Linux keys (src:tobias)

Worker changes are buildable but **this bead is incomplete**. The decision
[audit](../../docs/decisions/edit-d50.md) records implemented fast paths,
remaining text/syntax/multi-selection capabilities and retained UI notices.
The [worker report](../../docs/worker-reports/edit-d50.md) contains the red/green
runs, release/sanitizer verification and sandbox socket failures. The coordinator
must rerun the display/IPC suites and compare native behavior with Sublime on
its safe display before closing or handing over this bead.

Typing-path storage is prepaid. New view plans use existing view scan/window
limits; oversized or approximate commands refuse without changing text,
selection or history. Accepted edits are explicit undo groups; undo preserves
trusted distant anchors, and soft-history eviction keeps text/selection ordering.

The older module status follows; the capability-flag audit above supersedes
older descriptions of `KEYS_ARG_UNIMPLEMENTED`.

---

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
