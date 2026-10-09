edit-457.1 finishing work is complete. Release and sanitizer checks are green; benchmark misses are recorded as TRACK.

Changed files:

- [wrap.c](/home/tobias/Projects/editor/.wt/edit-457.1c/src/layout/wrap.c): retain examined word lookahead after soft-break rewind, fixing incorrect edit invalidation.
- [layout.c](/home/tobias/Projects/editor/.wt/edit-457.1c/src/layout/layout.c): clear repacked storage after width changes in both wrap modes.
- [wrap_test.c](/home/tobias/Projects/editor/.wt/edit-457.1c/tests/wrap_test.c), [layout_test.c](/home/tobias/Projects/editor/.wt/edit-457.1c/tests/layout_test.c), [layout_fuzz.c](/home/tobias/Projects/editor/.wt/edit-457.1c/fuzz/layout_fuzz.c): strengthen Home affinity/rendering coverage; test resize growth, shrinkage, toggles and fresh-layout comparisons.
- [P4.1.md](/home/tobias/Projects/editor/.wt/edit-457.1c/docs/decisions/P4.1.md) and [STATUS.md](/home/tobias/Projects/editor/.wt/edit-457.1c/src/layout/STATUS.md): completed state, design decisions and stamped evidence.

The whitespace Home fix was already on main. Its original red run was recovered from the previous worker’s log; no additional `view.c` change was needed:

```text
FAIL wrap_test:260 view_command(&v,VIEW_HOME,false,NULL,0,&ch)==0 && v.state.selection.cursor==5 && v.state.visual_end
```

New red runs, before fixes:

```text
FAIL wrap_test:295 memcmp(a.cells,b.cells,(size_t)widths[i]*8u*sizeof(render_cell))==0
FAIL tests/layout_test.c:603 same_grid(&f,&fresh)
FAIL tests/layout_test.c:71 render_grid_validate(&f->g) == RENDER_OK
layout_test: 8 FAILED
```

The saved fuzz artifact exposed word-lookahead invalidation; its new standalone regression failed:

```text
FAIL wrap_test:249 memcmp(a.cells,b.cells,16u*sizeof(render_cell))==0
```

Final green verification, (M)[AC], BAT0=Not charging, load1=7.07:

```text
wrap_test: 10000 relayouts mallocs=0 guard=active
wrap_test: 10000 typing edits mallocs=0 guard=active
wrap_test: all passed
layout no-malloc: 0 allocations over 10000 relayouts (guard active)
layout_test: all passed
view_test: 10000 keys mallocs=0 guard=active
view_test: all passed
check: 39 test binaries passed
test_replay_cli: all passed
fuzz: 20 fuzzers built
```

`make all`: exit 0, gcc release.  
`make check`: exit 0, clang ASan/UBSan, `ASAN_OPTIONS=detect_leaks=0`.  
`make fuzz`: exit 0.

Layout fuzzing used seed 4571, `-max_total_time=600 -max_len=4096 -timeout=30`. (M)[AC], Not charging, load1=4.25 before launch:

```text
Done 5043 runs in 601 second(s)
```

No sanitizer/property failure. Both previous crash artifacts replay cleanly.

One complete layout bench run exited 1 for observed misses. TRACK only; values below are ns, measurements (M), gates (G). Power was [AC], Not charging; load1=6.97 before the full campaign and 8.28 at the wrapped subcampaign:

```text
BENCH name=layout_wrapped_unicode.txt_360x300 n=2000 p50=735419 p99=1006498 ci95=[732020,739960] gate_p50=300000 gate_p99=300000 pass=0 power=[AC]
BENCH name=layout_wrapped_unicode.txt_typing_row_TRACK n=2000 p50=11114 p99=23875 ci95=[10946,11341] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=layout_wrapped_oneline_1g.txt_360x300 n=2000 p50=731102 p99=3742199 ci95=[728567,734376] gate_p50=300000 gate_p99=300000 pass=0 power=[AC]
BENCH name=layout_wrapped_oneline_1g.txt_typing_row_TRACK n=2000 p50=832359 p99=3833118 ci95=[829144,835923] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=layout_wrapped_oneline_1g.txt_byte_100000000_TRACK n=200 p50=705048 p99=764190 ci95=[703565,706111] gate_p50=300000 gate_p99=300000 pass=0 power=[AC]
```

Original ASCII/log viewport rows also observed misses; indexed wrap-off rows passed. Complete raw output is in P4.1.md. No gates were relaxed, no load-driven tuning was done, and the bench was not retried.

Open items: coordinator gate verdict and leak-enabled verification; editor-loop integration remains with its integration bead. Deep uncached global wrap phase remains explicitly approximate. No new unresolved module failure remains.