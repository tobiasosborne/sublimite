# edit-zzj.15 — session 8c rebase conflict resolution

Resolved working-tree conflicts only. No git state-changing commands and no bd.
The coordinator must stage the resolution and continue the existing rebase.

## Resolution

- `src/editor/editor.c`: retained main's `queue_count < EDITOR_INPUT_CAP`
  platform input-drain gate and the EGL pending-frame poll, bounded GPU wait,
  and Present completion callback. Neither loop behavior was dropped.
- `src/editor/open.c`: retained main's foreground-capable work pool and its
  backend-init work job, immutable mailbox result, and physical-completion
  retention. Extracted that mechanism into `init_backend` and invoke it for
  both the EGL attempt and raster fallback. Provision raster workers whenever
  fallback is possible. Before replacing a failed EGL descriptor, reset the
  editor arena to the mark preceding its state allocation; this occurs only
  after the init job has physically finished. Backend init still owns native
  and heap rollback; successful backend shutdown precedes arena destruction.
  Record the original error in `stats.backend_init_error`, log the fallback
  once, and retain `plat_set_present_events(false)` for raster-pool backends.
- `tests/editor_test.c`: retained the complete main review suite, its environment
  selectors, and the EGL default/override, injected-init/dlopen failure,
  stays-raster, GPU-completion and selected-backend fixtures and CLI selectors.
  The injected failure also verifies execution on the bulk work-pool thread.

## Additional integration failures resolved

The first release link exposed a duplicate `plat_set_present_events` export:
`gl_test` includes a private platform implementation and renames its exports.
Added the new function to that existing rename/undef list in `tests/gl_test.c`.

The first runnable sanitizer suite exposed two pre-existing integration issues
in main's close behavior: its close fixture expected operations to remain staged
although the review changes journal them eagerly, and early close return paths
skipped the required flush. Route all early close returns through a checked
`editor_flush`. Update `tests/editor_close_test.c` to assert eager admission and
reserve one native queue slot, consistent with the required platform capacity
gate; injected close still exercises a completely saturated queue. Both retain
held-frame, blocked-view, one-turn close and journal replay assertions. Main's
native-order and burst tests remain unchanged.

## Validation

Commands set `DISPLAY=:99 EDIT_DISPLAY=:99`; LeakSanitizer is disabled as
requested. The sandbox initially prevented Xvfb socket binding and display/IPC
connections. An escalated Xvfb using its filesystem Unix socket
(`-nolisten tcp -nolisten local`) started :99 successfully; display-dependent
checks run outside that socket restriction. No real user display was used.

- `make -j4 all`: exit 0, gcc with `-Werror` (after all source changes).
- `git diff --check`: exit 0.
- `grep -rn '^<<<<<<<\|^>>>>>>>' src tests bench`: no matches, exit 1.
- Standalone merged `build/san/tests/editor_test`: exit 0, including review
  regressions, EGL fallback cases, both selected backends, exact idle poll
  assertions, allocation loops and native input/close.
- `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`:
  exit 0; all 58 sanitizer test binaries and `tools/test_replay_cli.sh` passed.
  This includes the final merged editor suite, both close fixtures, raster
  conformance, GL cleanup/readback and native-order review regressions.

The existing GL test reports its native lifecycle SKIP on Xvfb because EGL
swap does not supply matching PIXMAP Present MSC; its readback and cleanup
checks pass. This run does not establish a hardware EGL lifecycle result.

Logs: `/tmp/edit-zzj.15-s8c-all.log`, `/tmp/edit-zzj.15-s8c-check.log`,
`/tmp/edit-zzj.15-s8c-editor.log`. Coordinator will rerun with leaks enabled.
