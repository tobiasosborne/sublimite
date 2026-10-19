# edit-ut0m worker report (s9)

Verified and fixed the mapped-original rename-over → Keep → edit → save bug.
Keep now refreshes the save controller's retired source through
`file_source_adopt_retired` before rebasing its target. The existing checks for
an unchanged fault epoch, an unfaulted backing, a different replacement inode,
and ctime as the only identity change remain intact. Adoption failure leaves
the source prompt visible. No allocation or fatal-error path was added.

Repository files touched:

- `src/editor/buffers.c`: explicit Keep uses the guarded source/target rebase.
- `src/savectl/savectl.c`, `src/savectl/savectl.h`: add
  `savectl_rebase_kept_target`, with idle/recovery ownership checks before I/O.
- `src/file/file.h`: permit the existing adoption helper during exclusive,
  off-path Keep maintenance as well as worker replacement.
- `tests/editor_session_test.c`: regression with journalling enabled and
  disabled, exact saved bytes, clean-tab assertions, a subsequent edit/save,
  failure-path fixture cleanup, and `--edit-ut0m` selection.
- `docs/decisions/edit-ut0m.md`: record the immediate, exclusive Keep adoption.
- `docs/worker-reports/edit-ut0m-s9.md`: this report.

The initial edit before external rename makes the buffer dirty so the editor
offers Keep instead of automatically reloading a clean source. The fixture
uses `sizeof ""` as the smallest nonzero copy threshold ((P), the C string
terminator occupies a byte); payload sizes derive from the fixture literals.
No large fixture was added.

Run output below is measured (M)[AC]; captured power status was `Charging`.
Exit statuses follow the shell convention ((P), success is zero).
`FILE_ERR_CHANGED` is the file header's error constant, observed as `5` in the
red run. Build concurrency, display identifier and lock timeout copy the
brief's supplied policy ((E), no new performance bound).

The red run preceded implementation. The sanitizer target built successfully
with `make -j3 build/san/tests/editor_session_test` (exit `0`). Each focused run
used a fresh `ut0m_runtime=$(mktemp -d)` with an EXIT trap removing it.

```text
$ XDG_RUNTIME_DIR="$ut0m_runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-ut0m
edit-ut0m: save after mapped rename-over/Keep error=5 journal=on
editor_session_test:173: FAIL !f->e->buffer->save_error
edit-ut0m: save after mapped rename-over/Keep error=5 journal=off
editor_session_test:173: FAIL !f->e->buffer->save_error
exit=1
```

The green run followed implementation and a successful rebuild of the same
sanitizer target (exit `0`).

```text
$ XDG_RUNTIME_DIR="$ut0m_runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-ut0m
edit-ut0m: save after mapped rename-over/Keep error=0 journal=on
edit-ut0m: save after mapped rename-over/Keep error=0 journal=off
exit=0
```

Verification commands and observed exit codes (M)[AC]:

| Command | Exit |
| --- | --- |
| `make -j3 all` — prescribed GCC release configuration, strict warnings and `-Werror` | `0` |
| `make -j3 build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/file_test build/san/tests/dataloss_test build/san/tests/savectl_test` — prescribed Clang ASan/UBSan configuration | `0` |

Every sanitizer command below used the complete binary, with prefix
`XDG_RUNTIME_DIR="$ut0m_runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh`:

| Binary | Exit |
| --- | --- |
| `build/san/tests/editor_test` | `0` |
| `build/san/tests/editor_session_test` | `0` |
| `build/san/tests/file_test` | `0` |
| `build/san/tests/dataloss_test` | `0` |
| `build/san/tests/savectl_test` | `0` |

Release malloc-guard command:
`XDG_RUNTIME_DIR="$ut0m_runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/tests/editor_test`
— exit `0`. Selected output (M)[AC]; key counts are the existing suite's
typing-law workload, and zero allocations is the no-malloc rule's oracle:

```text
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: null hl-on 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
```

The initial sandbox attempt reproduced the assertion failure but LeakSanitizer
could not inspect threads under its ptrace restriction. The clean red/green
runs and all full sanitizer suites above ran outside that restriction, with
leak detection enabled. The assigned display was reachable. Shared display
lock contention delayed the remaining suites; all completed under the
requested lock timeout without bypassing the lock.

The inherited sparse GB fixtures ran with their existing cleanup wrappers.
Disk snapshots before the sanitizer runs and after the final suite are pasted
below (M)[AC], including power/load stamps. These are shared-filesystem readings;
release compilation and other workers were active during verification.

```text
$ cat /sys/class/power_supply/BAT0/status; uptime; df -h /
Charging
 02:26:03 up 51 min,  1 user,  load average: 38.73, 34.43, 19.26
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  381G   75G  84% /

$ cat /sys/class/power_supply/BAT0/status; uptime; df -h /
Charging
 02:45:43 up  1:10,  1 user,  load average: 16.38, 21.70, 23.19
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```

Generated build/run logs and the suite runner are under
`build/edit-ut0m-verification/`. No required work remains for this bead.
No tracker/VCS commands were run, and no prohibited documentation was edited.

Outside scope, observed while editing the Keep branch: an existing failure of
`file_id_stat_path(b->save_path, ...)` still bypasses rebasing and falls through
to dismiss the prompt. This was not reproduced or changed; it is separate from
the successful external rename-over covered here. No additional review pass
was performed.
