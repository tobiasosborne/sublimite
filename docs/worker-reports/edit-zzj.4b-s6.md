Fixed the documented scrolling contract. The cause was an existing scroll omission: bounded cursor following accepted a coarse byte boundary and returned success without proving cursor visibility. P1.6b/P1.8c semantics were not responsible.

Changed files:

- [src/scroll/index.c](/home/tobias/Projects/editor/.wt/edit-zzj.4/src/scroll/index.c): return `SCROLL_MORE` with unchanged state when cursor/margin proof is incomplete.
- [tests/scroll_test.c](/home/tobias/Projects/editor/.wt/edit-zzj.4/tests/scroll_test.c): regressions and cleanup on every assertion failure.
- Updated [P3.4.md](/home/tobias/Projects/editor/.wt/edit-zzj.4/docs/decisions/P3.4.md), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-zzj.4/src/scroll/STATUS.md), and [worklog](/home/tobias/Projects/editor/.wt/edit-zzj.4/docs/worklog/2026-10-09.md).

Red, ASan/UBSan, (M)[AC], Not charging, load1=4.26:

```text
scroll_test:157: FAIL scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE && s.first_byte == 0
```

New regression red, (M)[AC], Not charging, load1=16.31:

```text
scroll_test:220: FAIL scroll_follow_cursor(&s, index, &src, 150000) == SCROLL_MORE
```

Green, GCC release, (M)[AC], Not charging, load1=15.13:

```text
scroll_test: bounded cursor proof/margins/publication retry PASS; allocations=0 guard=active
scroll_test: bounded long-line/source errors/partial seeks PASS; allocations=0 guard=active
scroll_test: PASS
```

Verification, all on `DISPLAY=:99 EDIT_DISPLAY=:99`; power Not charging:

```text
make all: exit 0                         (M)[AC] load1=15.13
make check: exit 0; ASan/UBSan/LSan on    (M)[AC] load1=13.99
check: 45 test binaries passed
test_replay_cli: all passed
make fuzz: exit 0                       (M)[AC] load1=14.53
fuzz: 23 fuzzers built
```

Injected assertion failures in every index-owning fixture were LSan-clean, (M)[AC], load1=5.92. Saved fuzz reproducers passed; the campaign completed:

```text
Done 17519 runs in 121 second(s)
```

Fuzz counts/time: (M)[AC], Not charging, pre-run load1=13.17.

TRACK benchmark, Not charging:

```text
BENCH G7j_warm p99_ns=145540021 (M)[AC] load1=12.91 (G)<=50000000 TRACK=1
BENCH G7j_warm p50_ns=89745047 (M)[AC] load1=12.91 (G)<=30000000 TRACK=1
BENCH G3z_work_proxy (M)[AC] load1=12.91 power=Not charging n=10000 p50_ns=35014 p99_ns=77587 max_ns=195957 over_T_half=0 (G)max_ns<=4166666 (G)over_T_half=0 TRACK=1
```

No scoped work or unrelated failures remain. Deep-jump TRACK comparisons still miss; editor wiring and displayed-frame cadence remain outside this fix-up.