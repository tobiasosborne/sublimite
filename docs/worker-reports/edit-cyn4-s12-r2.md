# edit-cyn4 round 2 — Windows fonts

Verdict: **done**. Implementation covers the triaged findings in
the requested order: 1, 2, 3, 4, 5, 7. The frozen `src/os/os.h` is unchanged.
No state-changing git command, bd command, main-checkout edit, package install,
or other worktree access was performed. The only main-checkout reads were the
two authorized briefing files. Windows tests are headless and run through
`tools/winrun.sh`; WSL interop required lifting the sandbox restriction.

## Changes and files

- `src/font/dwrite_win.c`: normalize the TrueType horizontal origin before
  glyph analysis, load system DLLs with System32-only search, and provide
  compile-time-only acquisition/Release observation points for the supplemental
  test translation unit. Clear private output before an acquisition failure.
- `src/font/discovery_win.c`: validate the selected file/index with the common
  initializer and a real grayscale raster, reject visible glyphs with no ink,
  preserve known blank/ignorable scalars, and retry a documented installed-family
  list. General scalar requests use `GetUserDefaultLocaleName`; the representative
  CJK probe retains `zh-CN`.
- `tests/win/font_dwrite_r2_test.c`, `font_dwrite_oracle.h`: embedded bearing
  mutation, candidate refusal/retry, locale and DLL policy, exact asymmetric
  analytic artwork and cell placement, real acquisition failures and ownership
  balance, apartment preservation, and unavailable-service mailbox/UI coverage.
- `tests/win/font_dwrite_test.c`: remove unsupported cross-filter tolerances;
  retain installed-font observations and routing checks. Permit the intentional
  difference between representative-probe and user-locale Han fallback faces.
- `mk/win.mk`: link the supplemental suite with the instrumented implementations
  in its own translation unit; normal production objects have no test shims.
- `docs/decisions/edit-cyn4.md`: corrected prior placement/filter explanations
  and appended the requested Round 2 finding → change → test table.
- This report, the worktree handoff and the session worklog record the outcome.

## Red before green

Logs are under `build/worker-logs/r2/`. Each behavioral test was written and run
before its corresponding fix. The original implementations were copied there
before editing. A separately linked `font_dwrite_round1_test.exe`, using those
unchanged sources, also replays the regression cases without reverting the
working tree. The locale red used an observation-only hook before MapCharacters;
it did not change the source's locale policy. The oracle red exercised the
actual round-1 predicate factored into the shared test helper, before replacing
that predicate. Assertions' line numbers changed as the suite grew.

Common build and execution commands:

```sh
make TARGET=win build/win/rel/tests/font_dwrite_r2_test.exe
tools/winrun.sh build/win/rel/tests/font_dwrite_r2_test.exe CASE
```

Builds exited zero after test-harness setup (INITGUID ordering and dependencies
were corrected before executing behavioral red tests). Every red command below
exited one; every corresponding green command exited zero. These are (M)
observations [AC, desktop Spiral], not timing gates.

### Finding 1 — horizontal origin

Command: common execution command with `CASE=origin`.
Red excerpt (`origin-red.log`, before the origin fix):

```text
origin U+0132 lsb=-1 native/stb left=0/-1 changed-native=6
font_dwrite_r2_test:77: same_bitmap(&native, &shifted)
exit=1
```

Final green excerpt (`origin-green.log`):

```text
origin U+0132 lsb=-1 native/stb left=0/-1 changed-native=0
origin placed half-coverage box native/stb=0,5,16,24/0,5,15,24
origin U+0021 lsb=516 native/stb left=6/6 changed-native=6
origin placed half-coverage box native/stb=6,5,9,24/6,5,9,24
origin: negative/positive embedded bearings retain stb origin and native cell pixels
exit=0
```

The test selects glyphs from embedded hmtx metrics, rather than hard-coding a
centered glyph. It alters only the selected hmtx lsb and clears head's
lsb-equals-xMin flag; contours and advance remain unchanged. (P) The mutation is
2048/4=512 design units, a quarter em. Both stb output and normalized native
output, including native placed cell pixels, must be exactly invariant.
Placement is additionally compared between the two backends on both glyphs.
The reported half-coverage boxes and their exact per-edge deltas are measured
expectations for these embedded bytes at the inherited bake size, not universal
antialiasing bounds; derivation and filter limitation are in the decision doc.

### Finding 2 — monochrome suitability and retry

Commands: common execution command with `CASE=candidate` and
`CASE=candidate-unsupported`.
Red excerpts (`candidate-red.log`, `candidate-unsupported-red.log`):

```text
candidate: blank first face -> 'build/worker-logs/r2/candidate.ttf'
font_dwrite_r2_test:120: strcmp(path, fixture)
exit=1
candidate: unsupported first face -> 'build/worker-logs/r2/candidate.ttf'
font_dwrite_r2_test:122: strcmp(path, fixture)
exit=1
```

Green excerpts (`candidate-green.log`, `candidate-unsupported-green.log`):

```text
candidate: blank first face -> 'C:\WINDOWS\FONTS\SEGUISYM.TTF'
candidate: U+1F600 accepted 'C:\WINDOWS\FONTS\SEGUIEMJ.TTF' index=0
candidate: blank and unsupported first faces rejected; retry and space passed
candidate: unsupported first face -> 'C:\WINDOWS\FONTS\SEGUISYM.TTF'
candidate: U+1F600 accepted 'C:\WINDOWS\FONTS\SEGUIEMJ.TTF' index=0
candidate: blank and unsupported first faces rejected; retry and space passed
exit=0/0
```

The path seam replaces only the first candidate's path/index. Common file
loading, font validation, native rasterization and subsequent installed-family
selection remain real. One fixture retains cmap coverage but removes the
monochrome contours, reproducing the empty-base failure mode of color-only
artwork; the other file is unsupported. A space remains valid despite no ink.
MapCharacters has no next-result cursor, so the explicitly documented family
list is used. On this box U+1F600's first candidate is Segoe UI Emoji, with
working monochrome outlines; it stays accepted despite its color tables. No
real color-only first candidate was identified by this probe, and no exhaustive
Unicode/installed-font sweep is claimed.

### Finding 3 — locale

Command: common execution command with `CASE=locale`.
Red (`locale-red.log`):

```text
locale: requested=de-DE delivered=zh-CN
font_dwrite_r2_test:159: !wcscmp(user_locale, observed_locale)
exit=1
```

Green (`locale-green.log`):

```text
locale: requested=de-DE delivered=de-DE
locale: requested=ja-JP delivered=ja-JP
locale: general Han lookup follows user locale
exit=0
```

The same Han scalar is queried with distinct injected user locales; the hook
reads the actual source's GetLocaleName result immediately before the real
fallback call. General discovery uses the requested GetUserDefaultLocaleName
API, falling back to an empty neutral locale on API failure. A German user
therefore gets Windows' German-locale Han fallback policy, rather than an
explicit Simplified Chinese bias. German does not specify a Han document's
language; document-specific Japanese/Traditional Chinese selection would need
a future preference. The representative probe still requests zh-CN.

The installed integration test initially failed its old equal-path assertion
after this policy change. Its corrected check preserves source-path and ink
validation while allowing the user-locale Han face to differ. (M) On this box
the probe selected `MSYH.TTC` index one and general lookup selected
`YUGOTHM.TTC` index one; emoji remained `SEGUIEMJ.TTF` index zero.

### Finding 4 — DLL search

Command: common execution command with `CASE=dll`.
Red (`dll-red.log`):

```text
dll: requested flags=0x0
font_dwrite_r2_test:180: load_flags == LOAD_LIBRARY_SEARCH_SYSTEM32
exit=1
```

Green (`dll-green.log`):

```text
dll: requested flags=0x800
dll: System32-only load and exact stb fallback passed
exit=0
```

(P) `0x800` is the Windows constant LOAD_LIBRARY_SEARCH_SYSTEM32, observed at
the actual API boundary. A forced load failure must leave no owned DLL/factory/
apartment and the public raster must return byte-identical stb output.

### Finding 5 — spatial conformance

Command: common execution command with `CASE=oracle`.
Red with the factored round-1 predicate (`oracle-red.log`):

```text
oracle: row-flipped equal-sum artwork accepted=1
font_dwrite_r2_test:233: !font_test_artwork_equal(&f, &expected, &bad)
exit=1
```

Green (`oracle-green.log`):

```text
oracle: row-flipped equal-sum artwork accepted=0
oracle: native box=2,-10,8,12 advance=16; expected=2,-10,8,12 advance=16
oracle: analytic L placement/pixels exact; row flip, ink shift and bearing corruption rejected
exit=0
```

The former rectangle-perimeter bound is withdrawn. General installed-font
coverage sums and boxes are printed as observations only. The decisive fixture
uses an independently specified asymmetric L and compares metadata, every
sample, and final cell pixels. (P) Scale=32/2048=1/64; integer-pixel contour
edges give zero floor/ceil error. The L's area is `3*10 + 8*2 = 46` pixels, so
its exact integrated coverage is `46*255 = 11730`. There is no general coverage
error budget. Row flips and in-box shifts preserve the coverage sum but must
fail the spatial oracle; changing only the bearing also fails.

### Finding 7 — acquired resources and failures

Commands (all stages were tested before adding the injection points):

```sh
for point in loader file face analysis bounds pixels texture; do
    tools/winrun.sh build/win/rel/tests/font_dwrite_r2_test.exe failure-$point
done
```

Red excerpts (`failure-POINT-red.log`); each invocation exited one:

```text
loader: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
file: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
face: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
analysis: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
bounds: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
pixels: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
texture: font_dwrite_r2_test:319: font_dwrite_raster(&f, gid, &a, &b, 0) == FONT_ERR_INIT
```

Green excerpts (`failure-POINT-green.log`); each invocation exited zero:

```text
failure loader: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure file: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure face: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure analysis: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure bounds: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure pixels: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
failure texture: real acquisitions; factory=0 loader=1 stream=1; DLL/COM=0; rollback and scalar/id stb exact
```

The stage mask must contain exactly the successful acquisition prefix up to the
injection. Failures after pixel allocation and texture creation must rewind a
nonzero entry mark and expose no pixels. Factory final Release must return zero;
loader and stream must each return to their initial owner reference of one.
Unregister must succeed; DLL acquisitions/frees and successful COM init/uninit
calls balance. Public scalar and glyph-ID calls must each exactly match stb.

The default supplemental run repeats every stage in an uninitialized apartment,
an existing MTA, and an existing STA; CoGetApartmentType must be unchanged after
the raster. (M) Case derivation: seven listed stages × three apartment states =
twenty-one stage/context combinations, each containing direct/scalar/id calls.
Replay against the saved round-1 sources also fails every existing-MTA/STA
combination (`round1-failure-POINT-mta-red.log`, `...-sta-red.log`).

Additional coverage pins already-correct behavior rather than inventing defects:
missing factory export/interface must release the acquired DLL/apartment;
unavailable discovery and async rendering must complete through live mailbox
adoption; a thread-ID hook forbids native entry during cold UI lookup, pumping
or adoption. Those compatibility checks do not claim a previously failing
production behavior. Supplemental green excerpts:

```text
service: absent export and factory interface balance DLL/COM
unavailable: live discovery/async mailbox delivery; no native calls on UI lookup/pump/adoption
font_dwrite_r2_test: origins, monochrome retry, locale, DLL, exact oracle and resource balance passed
```

## Verification

Final Windows command: `make -j64 TARGET=win check`, exit zero on the final tree
(`windows-check.log`). Its exact summary lines are:

```text
Windows release: 7 test binaries passed
Windows sanitizer: 7 test binaries passed
ASan fired: ok
```

(M) The count is the Makefile's expanded test list: smoke, os, os_win, font,
font_gsub, font_dwrite and font_dwrite_r2, in each build mode. Both font suites,
the original headless native integration and the new supplemental cases passed
in release and ASan/UBSan. The pre-existing OS supplemental suite still reports
its known unavailable symlink case; no new font case is skipped.

Final Linux release `make -j64 all` exited zero (`linux-all.log`). The full
Linux command `DISPLAY=:97 ASAN_OPTIONS=detect_leaks=1 make -j64 check` exited
zero (`linux-check.log`), with LeakSanitizer enabled. Exact suite summary:

```text
check: 104 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

(M) The binary count is the Makefile's expanded Linux sanitizer test list, as
printed after every binary succeeded. The shell checks also finished. Their
display-lock contract intentionally emits nested make failures and short :99
lock timeouts as negative controls; those are followed by its PASS, not a
failure of the outer :97 check. The contract's existing shell-probe sanitizer
exception remains as previously accepted by the coordinator; no test-binary
LeakSanitizer setting was weakened.

Final measurement stamp: (M) [AC, desktop Spiral], BAT0 absent;
`12:37:20 up 4:53, 1 user, load average: 3.62, 5.42, 5.86`.

An initial Linux full check exited two at the unrelated
`editor_large_test:2226: FAIL e->buffer->lg.warm` assertion
(`linux-check-first-failed.log`). A standalone unchanged rerun under the same
display lock and sanitizer settings passed (`editor-large-retry.log`, exit
zero); the full check was restarted and passed, including editor_large_test.
An initial display-lock wait was respected;
the other process was not interrupted.

The existing Linux module benchmark ran once, exit zero (`font-bench.log`).
(M) [AC, desktop Spiral]: BAT0 absent; uptime sample
`12:15:43 up 4:32, 1 user, load average: 3.97, 5.64, 4.43`.
The harness prints `[unknown]` on this desktop; this report supplies the verified
AC stamp. Raster p50/p99 was 2558/4722 ns and CJK raster 3935/12691 ns; these
are the benchmark's sorted-sample percentiles, tracked observations, not gates.
The counting guard was active and recorded zero allocations. The display guard
refused the inherited real display; the font benchmark itself has no display
operations. No native Windows performance claim is made.

## Found, not fixed / coordinator

- `src/d3d11` is absent in this worktree. There is no `d3dcompiler_47.dll`
  loader here to inspect; no other font-module DLL is loaded. The coordinator
  should check the separate W1.2 tree for the same unrestricted-load pattern;
  nothing in that module was read outside this worktree or edited.
- Review findings 6, 8, 9, 10 remain deferred exactly as triaged. The analytic
  fixture's true-em setup exists to make coordinates integral, not to close the
  separate GSUB/mark/size coverage backlog.
- The unrelated Linux editor warm-index assertion was observed once and passed
  in isolation. No editor source or test change was made.
- Retain both W0.5's expanded module/test lists and this supplemental link rule
  when the coordinator integrates `mk/win.mk`, as already decided.
- The pre-existing untracked `docs/worker-reports/edit-cyn4-review-final.md` was
  left untouched.

No new product decision is required for the font changes. The retry-family
policy, locale consequence, exact oracle derivation and measured-only filter
expectations are explicit in the Round 2 decision table and accompanying text.
