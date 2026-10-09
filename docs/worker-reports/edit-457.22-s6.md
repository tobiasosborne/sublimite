Implemented the module fixes. Full rectangle damage and editor/work wiring remain documented proposals in [P4.5b.md](/home/tobias/Projects/editor/.wt/edit-457.22/docs/decisions/P4.5b.md).

Changed files: `src/minimap/{minimap.c,minimap.h,STATUS.md}`, `src/indent/{indent.c,indent.h,STATUS.md}`, both module tests, fuzzers and benches, plus P4.5b.md.

Changes: source-aware cache validation and buffer binding; bounded indent queries returning `INDENT_ERR_LIMIT`; damage only for changed cells/rows; approximate byte dragging; worker preparation/publication and source-read-free cached fills. Existing prototypes are unchanged. No frozen headers or other modules were edited.

Red runs:
```text
§5 minimap_test:215: minimap_hit(&m,&in,NULL,1,&hit) == 0 && hit.exact && hit.byte == 1
§6 tests/indent_test.c:188: assertion failed: indent_on_enter(t,0,out,sizeof out,&n)!=INDENT_OK && n==0
§7 minimap_test:221: count == 1 && strips[0].first_row == 0 && strips[0].row_count == 1
§8 minimap_test:237: cells[1].bg != model_blend(style.background,style.stale,128)
§9 minimap_test:257: f.calls == 0
```

Green runs:
```text
review 5: equal-size tab switch exact byte=1: ok
review 6: long-line typing/zero-capacity work bounded: ok
review 7: cached fill preserves typing/blink damage: ok
review 8: pending index paints current byte scrollbar and drag works: ok
review 9: UI deferred fill reads no dispersed samples: ok
```

Verification, using DISPLAY=:99 EDIT_DISPLAY=:99:
```text
make all: exit=0
make check: exit=0
check: 39 test binaries passed
test_replay_cli: all passed
make fuzz: exit=0
fuzz: 21 fuzzers built
```
Sanitizers used `ASAN_OPTIONS=detect_leaks=0`. Full check passed with Xvfb socket access.

Fuzz runs, both exit=0 with no sanitizer findings; (M)[AC], BAT0=Not charging, load1=4.52:
```text
indent:  Done 413574 runs in 121 second(s)
minimap: Done 33244 runs in 121 second(s)
```

Each bench ran once, TRACK only. BAT0=Not charging [AC]; timings below are (M).
```text
indent_on_enter       load1=7.22 p50=0.595 us   p99=0.769 us   gate=20 us(G)   pass=1
indent_bracket_match  load1=7.22 p50=0.222 us   p99=3.151 us   gate=20 us(G)   pass=1
indent_long_enter     load1=7.22 p50=8.269 us   p99=19.327 us  gate=20 us(G)   pass=1
indent_long_brace     load1=7.22 p50=8.268 us   p99=21.053 us  gate=20 us(G)   pass=0
indent_detect         load1=7.22 p50=186.296 us p99=196.381 us gate=1000 us(G) pass=1
minimap_small         load1=7.04 p50=141219 ns p99=148168 ns gate=500000 ns(G) pass=1
minimap_small_edit    load1=7.04 p50=145487 ns p99=159695 ns gate=500000 ns(G) pass=1
minimap_log_1g        load1=7.04 p50=154148 ns p99=163587 ns gate=500000 ns(G) pass=1
minimap_log_1g_edit   load1=7.04 p50=154574 ns p99=168443 ns gate=500000 ns(G) pass=1
minimap_cold_samples  load1=7.04 foreground_major_faults=0 source_calls=0 eviction=requested
```
Indent bench exit=1 for the long-line brace TRACK miss; minimap bench exit=0. No reruns.

Open problems: changed sidebar rows still submit full-width damage through frozen render APIs. The editor/work/view integration must adopt cached fills, snapshot publication and approximate byte scrolling. Legacy synchronous minimap fill can still fault on nonresident sources. Contract additions and integration proposals are recorded in P4.5b.md; LeakSanitizer verification remains with the coordinator.