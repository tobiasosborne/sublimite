# P4.2 / P4.5b indent status

Standalone queries complete. Original contract:
[decision P4.2](../../docs/decisions/P4.2.md); review §6 fix and red/green:
[decision P4.5b](../../docs/decisions/P4.5b.md).

Enter preserves exact leading SP/TAB and LF/CRLF; brace handling returns a
pre-insertion dedent replacement containing `}` for one undo group. Bracket
matching and trailing whitespace scan only the supplied viewport window;
style detection reads only its bounded prefix. Queries have explicit errors,
caller-owned storage and no allocations or mutable module state.

Review fix: Enter/brace line-boundary scans now stop at INDENT_TYPING_BYTES in
each direction. A missing boundary returns added INDENT_ERR_LIMIT, cleared
results and no output writes. Successful result semantics and every existing
prototype are unchanged. The editor loop must explicitly reject/defer LIMIT
without mutation or a synchronous retry loop; this is the only new indent
integration obligation. Tests prove forward/backward limits with protected
mapped pages and include zero-capacity Enter. Fuzz covers synthetic long-line
queries; the bench adds bounded long-line Enter and brace cases.

Bounds limit byte work, not page-fault latency. Quotes/comments still are
ordinary bytes in bracket matching; detection remains a documented heuristic.
No editor, piece, layout, undo or frozen header changes were made. Integrated
painting and loop error handling remain with their owning beads.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99:
```
make all
ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/fuzz/indent_fuzz -max_total_time=120 -max_len=8192 -timeout=10
```
Before the single final TRACK bench invocation:
```
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
build/bench/indent_bench
```

Final verification: make all exit=0; make check exit=0, 39 test binaries and
replay CLI passed (with access to Xvfb :99); make fuzz exit=0, 21 fuzzers built.
Indent fuzz: Done 413574 runs in 121 second(s), exit=0, no sanitizer findings,
(M)[AC], Not charging, load1=4.52. Release typing allocation guard reports zero.
One final indent TRACK bench, (M)[AC], Not charging, load1=7.22: ordinary Enter
p99=0.769 us; bracket p99=3.151 us; bounded long-line Enter p99=19.327 us;
brace p99=21.053 us; detection p99=196.381 us. Typing guards are (G) 20 us,
detection (G) 1000 us. Bench exit=1 for the brace TRACK miss only; no rerun or
number chasing. Quiet-box gate verdict belongs to the coordinator. Full stamped
lines and the review red/green are recorded in P4.5b. LeakSanitizer cannot run in this
sandbox; coordinator reruns with leaks enabled. The full X11 suite requires
access to Xvfb :99; sandbox-only check cannot connect to its socket.
