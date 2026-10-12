# edit-3u4: continuation verification

The previous worker's fix is retained unchanged. Complete checkpoints now emit
BASE/TARGET/VIEW for a pending open without DELETE/INSERT replacement records.
Adopted buffers still serialize their current contents, including intentional
deletion to empty. The regression reproduces the data loss without the fix and
passes with it. No implementation work remains within this bead; full-suite
validation remains incomplete for the reasons below.

## Files touched

- `src/editor/checkpoint.c`: previous worker's localized pending-open guard retained.
  Temporarily removed only that guard/comment and restored the saved fixed file
  byte for byte after each red run; final `cmp` passes.
- `tests/editor_checkpoint_test.c`: previous worker's regression retained unchanged.
  Held COPY acquisition, inactive/active pending tab checkpoints, abrupt exit,
  recovery, clean state, tab selection/order, byte-exact saving, journal shape,
  and adopted deletion to empty.
- `docs/decisions/edit-3u4.md`: previous worker's decision retained unchanged.
- `docs/worker-reports/edit-3u4-s6.md`: this report added.

Verification artifacts are under `build/edit-3u4-verification/`, including the
fixed backup, reconstructed unfixed checkpoint, unified diff, build logs,
per-suite logs and observed exit statuses. No `bd` or `git` command was run.
No sibling worktree or prohibited document was edited.

## Review and evidence derivations

Read the supplied review finding on pending checkpoints, the existing test and
decision, and the prepare/reload paths in `buffers.c`. Finding references are
review identifiers. Preparation creates an empty full tree and a prefix preview;
reload checkpoints all non-retired buffers. The previous unconditional DELETE
therefore erased the pending buffer's complete BASE in recovery. The guard
changes only explicit checkpoint serialization, outside the typing path, adds
no allocation and preserves existing return-code errors and BASE validation.
No new design choice was needed.

All numeric results, source-line locators and exit statuses in the transcripts
below are (M)[AC], copied from the actual command output. BAT0 was checked before
verification and again during it: `Not charging`, the repository's [AC] stamp.
Compiler versions are (M)[AC] from `gcc --version` and `clang --version`:
GCC 13.3.0 and clang 18.1.3. The build commands retain the required
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` flags.

(P) Expected fixture bytes = `FILE_PREFIX_MAX` + `sizeof "\npending tail\n" - 1`
= `(1 << 20)` + 14 = 1048590 bytes, from the existing prefix constant and literal
length; zero recovered bytes denotes the observed empty document. (P) Test
geometry derives from the status-string width and tab/text/status rows;
permissions derive from POSIX owner read/write. (E) Watchdogs reuse the existing
`EDITOR_IDLE_NS` policy, as documented in the retained decision. Display `:99`
is the coordinator-assigned identifier from the user brief. `detect_leaks=0`
is the Boolean disabling LeakSanitizer only; ASan and UBSan remain enabled.
No new budget, duration or count was introduced.

## Red run

Temporarily reconstructed the pre-fix unconditional DELETE/INSERT block using
only the retained checkpoint change; the regression test was already present.

```sh
make build/san/tests/editor_checkpoint_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_checkpoint_test
```

The build exited successfully; the test failed with the actual empty recovery
and source-file truncation, before the later cases could execute:

```text
editor_checkpoint_test: inactive pending COPY recovered=0 expected=1048590 clean=no disk_intact=no
editor_checkpoint_test:125: FAIL intact
editor_checkpoint_test:169: FAIL pending_checkpoint(false) == 0
exit: 1
```

The initial run with default leak detection reproduced the same assertion,
then LeakSanitizer reported that it cannot work under ptrace in this sandbox.
That exact output is retained in `red-with-lsan.log`. Repeating with only leak
detection disabled produced the clean red transcript above. The fixed source
was restored in a `finally` block after both attempts.

## Green run

```sh
make build/san/tests/editor_checkpoint_test build/san/tests/editor_session_test build/san/tests/editor_test build/san/tests/editor_p4_test build/san/tests/unicode_render_test build/san/tests/journal_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_checkpoint_test
```

```text
editor_checkpoint_test: inactive pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: active pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: adopted deletion to empty survives checkpoint/crash/recovery
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save passed
exit: 0
```

Related sanitizer suites were then run serially with the same environment:

```text
$ build/san/tests/editor_session_test
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
exit: 0
$ build/san/tests/editor_test --headless
editor_test: headless passed
exit: 0
$ build/san/tests/unicode_render_test
unicode_render_test: initial and injected é α 日 🙂; widths, combining overlay, atlas pixels and allocation guard passed
exit: 0
$ build/san/tests/journal_test
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
exit: 0
$ build/san/tests/editor_p4_test --73j-null
exit: 0
```

Full outputs are retained in the corresponding per-suite logs. The sanitizer
allocator guard is ASan-inert, as those suites explicitly report; the checkpoint
change does not alter the typing path.

## Release build and broader checks

```text
$ make all
exit: 0
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check
```

`make check` built its sanitizer binaries, passed `base_test`, waited for the
shared display lock, and then stopped at the CLI socket failure:

```text
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:131: check] Error 1
exit: 2
```

The remaining non-display suites and available headless subsets were invoked
serially despite that early stop. Successful invocations (names ending in an
option include the option actually used):

`base_test`, `config_editor_test`, `config_test`, `config_worker_test`, `editor_checkpoint_test`, `editor_close_test --headless`, `editor_hot_exit_test`, `editor_p4_test --73j-null`, `editor_scroll_test`, `editor_session_test`, `editor_test`, `file_test`, `find_bench_test`, `find_bestof_test`, `find_p1r_test`, `find_simd_filter_verify_test`, `find_test`, `find_timeout_test`, `findui_test`, `font_selection_test`, `font_size_test`, `font_test`, `gl_bench_test`, `gl_gate_test`, `gl_scroll_test`, `gl_test`, `harness_test`, `hl_parse_test`, `hl_test`, `hl_worker_test`, `indent_test`, `journal_test`, `keys_test`, `layout_test`, `lineidx_test`, `minimap_test`, `piece_bounds_test`, `piece_mem_test`, `piece_mt_test`, `piece_test`, `prewake_test`, `raster_scroll_test`, `render_test`, `replay-cli-shell`, `replay_test`, `runtime_test`, `savectl_reload_test`, `savectl_test`, `scan_test`, `scroll_display_test`, `scroll_test`, `startup_test`, `tabs_test`, `trace_pairs_test`, `trace_test`, `undo_test`, `unicode_render_test`, `utf8_tables_test`, `utf8_test`, `view_test`, `wayland_bench_test`, `wayland_gl_test`, `wayland_test`, `window_size_test`, `work_foreground_test`, `work_test`, `wrap_test`, `x11_display_guard_test`.

`gl_test` passed its pure checks and explicitly skipped native diagnostics.
The remaining results are detailed below; this report does not claim a complete
`make check` pass.

## What remains and findings outside scope

- Display suites cannot connect to the X socket inside this sandbox. The
  coordinator must rerun them on Xvfb `:99`. Direct evidence: `refproto_test`
  reports connection failure, `editor_close_test --native` fails editor opening,
  and `raster_test` fails `plat_init` in its native review case. `file_kill_test`
  also contains native viewport/queued-open/queued-save checks through the
  included bench's `view_init`, which calls `plat_init`; these fail here.
- Unix-domain socket restrictions also block non-display IPC coverage:
  `cli_test`, `ipc_test`, `editor_p4_test --headless`, and `editor_large_test`
  fail at IPC initialization. A direct AF_UNIX socket/bind probe returned
  `PermissionError(1, 'Operation not permitted')` (M)[AC], where the errno
  is the observed POSIX EPERM. The P4 null subset passed. Rerun the full IPC
  suites in a socket-capable environment; no source workaround was added.
- `journal_kill_test` has an unrelated replay callback failure. It exercises
  journal APIs directly and never calls `editor_checkpoint`. Inspection finds
  its WINDOW callback still asserts legacy `r->size == 8`, while
  `journal_set_window` emits `JOURNAL_WINDOW_BYTES == 12`: (P) the current
  payload is three uint32 fields, width + height + flags, each four bytes;
  the legacy expectation is width + height only. (M)[AC] The observed return
  `rc=6` is `JOURNAL_CALLBACK`, from the enum order in `journal.h`. This stale
  assertion was reported without modification:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
exit: 1
```

- LeakSanitizer cannot complete under the sandbox ptrace restriction. All
  reported sanitizer runs used leak detection disabled, preserving address
  and undefined-behavior instrumentation. A normal unsandboxed leak check
  remains for the coordinator.
- `make check` stopped before its final display-lock shell contract; that
  contract remains for the coordinator. The independent replay CLI shell
  contract was run and passed.

No unrelated source was fixed. The bead remains local and mechanically mergeable;
no module STATUS file is needed because the requested checkpoint implementation
and regression are complete. Stop after this bead.
