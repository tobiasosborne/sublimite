# edit-177r.14 report: reusable Windows editor fixture

Verdict: done (check exit codes at the end). Design: docs/decisions/edit-177r.14.md.

## Changes
- `tests/win/editor_fixture.h` (new, header-only, frozen contract in its header comment): private
  `%TEMP%\sublimite-test-<tag>-<hex>` profile (create/adopt/path/destroy; cleanup only of a
  `sublimite-test-*` tree), posted input (`efx_post_char/text/ctrl_key`), `efx_settle`,
  `efx_wait_mutation`, `efx_type`, exact byte oracle `efx_expect_bytes`, `efx_open_null` +
  `efx_wait_presented` (SIMULATED completion), `efx_open_d3d11` + `efx_wait_submitted` (CPU-submit
  completion, not a display present), `efx_child_run` (re-run test exe with mode and exit-code
  watchdog), sublimite.exe child helpers (`efx_wait_window`, `efx_wait_contents`), journal
  durability oracle (`efx_journal_has_insert`, `efx_wait_journal_insert`, profile variants).
- `tests/win/editor_fixture_test.c` (new): for each backend a child types "ab", waits for completion
  and for the journal insert 'b', `ExitProcess(75)`; the parent restarts on the same journal and
  expects exactly "abbase\n". Negative controls: oracle fails on missing byte, extra byte,
  short, and same-length wrong byte; absent journal marker; wrong expected child exit code.
- `tests/win/editor_win_test.c`: settle/wait_mutation/cleanup/run_child_case/profile setup now
  delegate to the fixture (thin wrappers keep every call site unchanged); a fail hook releases
  the held publication gate as `T` did. No assertion removed. Only lost: the `dirty/active/queue`
  detail of the settle-timeout diagnostic (private fields; the fixture prints pending/render_active/font_pending).
- `mk/win.mk`: link rules for `editor_fixture_test` (editor objects, rel and san); test is picked
  up by the existing `tests/win/*_test.c` wildcard.
- `src/editor/editor.c` (one expression in `present()`): see decision doc.

## Open risk resolved
The null backend does not complete under the Win32 loop as it was: (1) it is HEADLESS, so no HWND
exists and posted input is impossible (hence the inject path); (2) `present()` gated on
`d3d11_present_ready`, false for any non-D3D11 backend. Red before the seam:
`settle timeout pending=1 render_active=1 font_pending=0` / `editor_fixture_test:27 FAIL ... exit=1`.
After the seam the null frame presents synchronously.

## Red / green
Red (restart oracle broken to expect "abbasX\n"), `tools/winrun.sh build/win/rel/tests/editor_fixture_test.exe`:
```
editor_fixture: bytes differ: length 7, expected 7
editor_fixture:216 FAIL efx_expect_bytes native=0
exit=1
```
Green (real expectation):
```
editor_fixture_test: private profile, typing (inject and posted), completion, abrupt kill, restart, exact bytes PASS
SKIP (needs live desktop: real present timing; null-backend completion is simulated, D3D11 completion is a CPU submit)
exit=0
```
editor_win_test after the move: exit 0 (rel, 67 s), same PASS/SKIP lines as before, mallocs=0 on the typing path.

## Notes for consumers
- Use `efx_open_null` + `efx_type` (inject) for editor logic that needs no window; `efx_open_d3d11` + posted
  input when native messages matter. Both: type, `efx_settle`, `efx_expect_bytes`.
- Hot-exit style kill: `efx_wait_journal_insert` then `ExitProcess(n)` in a child run by `efx_child_run`.
- Timeouts are multiples of EFX_WATCH_NS = WATCH_NS (5 s, existing settle watchdog).

## Found, not fixed
- `windows_surface`/`d3d11_*` calls elsewhere in editor.c (resize paths near lines 680-720) still assume D3D11; unreachable headless today.
- efx_failed prints a stale GetLastError (native=1400 in negative controls); cosmetic.

## Verification (CPU `taskset -c 48-63 nice -n 10`, -j16, WIN_CHECK_JOBS=4; AC desktop Spiral, Tobias using the box)
- `make all`: exit 0.
- `DEBUGINFOD_URLS= make check`: exit 0.
- `WIN_CHECK_JOBS=4 make TARGET=win check`: **exit 2**, "failures=6" (654 s): rel+san of editor_exe_test
  (`editor_exe:198 FAIL other_window`), editor_win_test (child `--close-queue` result=258 = WAIT_TIMEOUT
  at the 3 x WATCH_NS child watchdog) and journal_kill_test (`journal_hot_exit_test:115 ... native=258`,
  also WAIT_TIMEOUT). editor_fixture_test passed rel and san in that run.
- Rerun of those three suites plus the fixture suite alone (`tools/win_check.py --jobs 3`, same prefix):
  all 8 PASS (editor_exe 41.6/42.5 s, editor_fixture 25.3/10.1 s, editor_win 58.5/80.8 s,
  journal_kill 178.0/205.8 s). All exits 258 are timeouts, so I attribute the full-run failures to station/CPU
  contention with 4 parallel jobs (journal_kill alone takes 178 s, near its limit). Unproven: I did not run the
  same full check on unmodified main. The coordinator should decide whether to re-run it.
