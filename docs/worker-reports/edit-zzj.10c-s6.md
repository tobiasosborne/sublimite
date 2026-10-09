Port complete. Changed [view.c](src/view/view.c), [view.h](src/view/view.h), [view tests](tests/view_test.c), [bench](bench/view_bench.c), [fuzzer](fuzz/view_fuzz.c), [decision](docs/decisions/P3.2b.md), and [STATUS](src/view/STATUS.md).

Dry-run first; applied clean hunks and manually merged the conflicts. Existing prototypes and public payloads remain compatible. P4 visual motion, wrap-preserving cancellation, and the editor’s empty-TYPE repair path pass.

Per-finding evidence below pastes the relevant red assertion/diagnostic and green output. Full diagnostics are in the decision file.

| Finding | Verdict | Red | Green |
|---|---|---|---|
| 1 | Fixed | `assertion failed: f.v.state.selection.cursor==n && f.v.state.selection.anchor==n` | `review_1: trailing empty End/Shift+End passed` |
| 2 | Fixed | `assertion failed: f.v.state.hscroll<=n+1 && n+1-f.v.state.hscroll<20` | `review_2: exact long-line follow and rendered cursor passed` |
| 3 | Fixed | `assertion failed: f.v.state.first_byte==0 && f.v.state.first_line==0 && f.v.state.hscroll==0` | `review_3: cancelled edit repair viewport passed` |
| 4 | Fixed, editor-compatible | `assertion failed: view_command(&f.v,VIEW_LEFT,0,((void *)0),0,&c)==VIEW_OK` | `review_4: long-cluster boundary no-ops and legacy external repair passed` |
| 7 | View-owned queries fixed | `review_7 RED: unindexed Down read protected tail before yielding` | `review_7: protected-tail queries and exact resumable line motion passed` |
| 8 | Bounded progress implemented | `review_8 RED: post-capture follow reread protected prefix` | `review_8: bounded replacement/progress/input ownership/cancel/undo passed` |
| 9 | Fixed | `assertion failed: f.v.scanned<=4096` | `review_9: conservative resumable scan budget passed` |
| 10 | Undo routing implemented | `assertion failed: c.groups==1 && c.has_state` | `review_10: undo delete/type/replace/group/redo integration passed` |
| 11 | Fixed | `assertion failed: !c.changed && c.offset==0 && c.old_len==0 && c.new_len==0` | `review_11: reused error change output passed` |
| 12 | Fixed | `review_12 RED: delayed command accepted by timing gate` | `review_12 GREEN: delayed command rejected by G1/G3/slice checks` |
| 13 | Independent oracle implemented | `review_13 RED: wrong movement accepted by oracle` | `review_13 GREEN: wrong movement rejected by independent oracle` |
| 14 | Normalization implemented | `assertion failed: f.v.state.selection.cursor==4 && f.v.state.selection.anchor==4` | `review_14: external rebase/affinity/cluster/restore passed` |
| 15 | Failure contracts covered | `assertion failed: f->v.state.selection.cursor==n && f->v.state.selection.anchor==n && f->v.state.selection.preferred_col==7` | `review_15: insert/delete/prefix allocation failures and retry passed` |
| 16 | Fixed | `review_16 RED: benchmark start splits complete source cluster` | `review_16 GREEN: benchmark start uses complete clusters` |

Final verification passed:

```text
make all: exit 0 — gcc, strict warnings
make check: exit 0 — ASan/UBSan, DISPLAY=:99, detect_leaks=0
check: 39 test binaries passed
test_replay_cli: all passed
make fuzz: exit 0
fuzz: 21 fuzzers built
wrap_port: long visual-column continuation and edited cancellation passed
```

Full-check stamp: (M)[AC], Not charging, load1=13.43.

Fuzz requested 120 s (G) per run. Successful runs/time: 1994/121 s at load1=5.23; 434/206 s at load1=29.18; **final 783/121 s at load1=13.30**, all (M)[AC], exit zero. The initial oracle accounting failure was corrected and its seed retained.

Single benchmark run: exit zero, TRACK only. Selected stamped lines:

```text
BENCH view_log_1g.txt_unindexed_type_bulk1_command mode=TRACK n=5 p50=11461 p99=17270 ns (M)[AC] load1=29.18 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=2565 p50=2955 p99=10027 ns (M)[AC] load1=29.18 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 9037052 ns(M)[AC] load1=29.18 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
```

Guarded allocations were zero (M)[AC], load1=29.18, across the corpus fixtures. The benchmark predates the final prefix-retention fix; incomplete deep replacement/setup rows are documented. No benchmark rerun or gate verdict claimed.

Open limits: inherited uncached P4 layout queries still call opaque piece line APIs; large TYPE payload insertion remains synchronous; replacement accepts reported partial progress rather than providing atomic reservation. Layout findings 5–6 remain edit-zzj.11. Coordinator verification with LSan enabled remains required.