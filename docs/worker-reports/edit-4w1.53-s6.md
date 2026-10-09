Implemented synchronous UI-thread page-cache writes, CRC/PAD protection, and sticky I/O failures with exact worker retry. `fdatasync` cadence and `journal_save_prepare/finish` remain intact.

Changed: `src/journal/{journal.c,journal.h,STATUS.md}`, both journal tests, journal fuzz/bench, `docs/decisions/P1.9.md`, and only the requested PRD §7 sentence. Full evidence is in [P1.9g](/home/tobias/Projects/editor/.wt/edit-4w1.53/docs/decisions/P1.9.md:930).

RED against the asynchronous path (M)[AC], load1=6.73:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=0 error=0
```

GREEN (M)[AC], launch load1=27.00:

```text
journal_kill_test: ok trials=1000 script_edits=100000 issued=49089469 page_cache=49088511 replayed=49089072 acknowledged=47942332 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)
kill_elapsed_seconds=580.64
```

Final release journal suite (M)[AC], load1=11.26:

```text
journal_test: append failures ok (EAGAIN/ENOSPC/EIO/EINTR/short, single attempt, retained worker retry, sticky IO, allocations=0)
journal_test: append gaps ok (PAD failure surfaced, split INSERT suffix retained, retry blocks later offsets)
journal_test: malloc_guard=active append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
```

Required checks:

```text
make all: exit=0                                  [AC] load1=23.93
make check: exit=0; check: 41 test binaries passed [AC] load1=24.95
test_replay_cli: all passed                       [AC] load1=24.95
make fuzz: exit=0; fuzz: 21 fuzzers built          [AC] load1=25.59
Done 5872 runs in 121 second(s), no findings    (M)[AC] load1=26.14
```

The successful sanitizer check used approved local Unix/Xvfb socket access after the sandboxed CLI connection failed. Displays stayed `:99`; LSan was disabled.

Single benchmark invocation, TRACK only:

```text
1 B append:   p50=3.740 us  p99=36.103 us  (M)[AC] load1=29.72
1 KiB append: p50=7.810 us  p99=114.236 us (M)[AC] load1=29.26
Comparison gate: p99 <=20 us (G); both rows exceeded it.
1 MB paste: p50=18.067 ms p99=36.239 ms   (M)[AC] load1=29.72
journal_bench: ok (default options, exact content, TRACK only)
```

Open constraint: the absolute no-stall requirement is **not met**—Linux regular-file writes can block inside the kernel, and `O_NONBLOCK` cannot prevent that. [Linux open(2)](https://man7.org/linux/man-pages/man2/open.2.html) The power-loss values remain worker cadence targets subject to scheduling/I/O delays. Coordinator gate verdicts and LSan verification remain pending. No unrelated implementation problem was fixed.