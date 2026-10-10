# edit-mdv s8 worker report

Scope: docs/reviews/P4-modules-2.md §1 BLOCKER only. §§9–20 were not started.

Done: reload retains and compares the complete current file identity using
shared file-module helpers. Descriptor and canonical directory entry are
validated before and after acquisition; canonical entry inspection uses lstat.
OP_CHECK and OP_KEEP also capture and compare complete identities. Journal
BASE retains its existing four-field cross-check because its format does not
carry extended identity metadata. Existing savectl fixtures now supply full
identities. No typing-path allocation was added.

The new tests/savectl_reload_test.c is automatically included by the unchanged
Makefile. Its executable-local pread seam reads the first 1 MiB of a 2 MiB A
file, rewrites the actual file with B through a separate descriptor, restores
mtime and verifies that ctime changed, then permits the remaining read. It
requires FILE_ERR_CHANGED, no replacement installation, unchanged old edited
tree and view offsets, and preserved modified/banner state. Additional tests
exercise restored-mtime checks and preservation of the adopted keep identity.

## Red, before the production fix

Command: `DISPLAY=:99 make -j4 build/tests/savectl_reload_test`, then
`DISPLAY=:99 build/tests/savectl_reload_test`. Build exit 0; test exit 134:

```text
restored-mtime reload: install=0 file_error=0 first=A last=B modified=0
tests/savectl_reload_test.c:96: rc==SAVECTL_BUSY && model.file_error==FILE_ERR_CHANGED
```

The original test was run before changing src/file or src/savectl. The line
number above refers to that initial regression version. Red log:
`/tmp/edit-mdv-s8-red.log`.

The added check/keep case was independently run against the original HEAD
savectl object, linked with the file module's unchanged helper behavior:
`DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 /tmp/edit-mdv-s8-baseline/savectl_identity_test --identity`.
Exit 134:

```text
tests/savectl_reload_test.c:73: savectl_get_model(s).banner && savectl_get_model(s).modified
```

## Green for this slice

Release commands: `DISPLAY=:99 build/tests/savectl_reload_test` and
`DISPLAY=:99 build/tests/savectl_test`. Both exit 0.

Sanitizer commands: `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_reload_test`
and `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test`.
Both exit 0, with no ASan/UBSan diagnostics:

```text
restored-mtime reload: install=-1 file_error=5 first=o last=s modified=1
savectl restored-mtime check/keep identity: ok
savectl_reload_test: ok
savectl ack never waits for worker/I/O: ok
savectl save states/snapshot isolation/self event: ok
savectl failed save preserves old file and modified flag: ok
savectl §2.14 save/external-change races (notified, unnotified, restored identity): ok
savectl external banner/silent reload/latest offsets/edit protection: ok
savectl journal prepare/finish/replay/failures/retained generation: ok
savectl full-mailbox completion fallback: ok
savectl_test: ok
```

Logs: `/tmp/edit-mdv-s8-final-release-green.log` and
`/tmp/edit-mdv-s8-final-san-green.log`.

`DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/file_test` also exits 0
(`file_test: ok`), verifying the shared file helper implementation's existing
suite. Log: `/tmp/edit-mdv-s8-file-san.log`.

## Required repository checks and acceptance blocker

`DISPLAY=:99 make -j4 all`: exit 0 (GCC, repository -Werror flags).
Final log: `/tmp/edit-mdv-s8-final-all.log`.

`DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`: exit 2.
The restricted sandbox initially prevented display/IPC access and stopped at
cli_test. Rerunning with automatically approved sandbox escalation on the
existing private DISPLAY=:99 passed cli_test, then consistently failed:

```text
== build/san/tests/editor_close_test
editor_close_test:42: RED editor_length(e) == 1 && e->op_count == 1
editor_close_test:72: RED close_case(false) == 0
make: *** [Makefile:101: check] Error 1
```

Final log: `/tmp/edit-mdv-s8-final-check.log`. The mandatory full-suite exit-0
acceptance is therefore not met; this report does not claim a green full tree.
Baseline confirmation: copied the sanitizer archive to /tmp, replaced file.o
and savectl.o with freshly compiled original HEAD sources obtained using
read-only git show, then linked the unchanged editor_close_test.o. This baseline
binary reproduces both failures above on :99. Log:
`/tmp/edit-mdv-s8-baseline-editor-close.log`. The unrelated test/code was not
changed to make the slice appear green.

`git diff --check`: exit 0. Git was used read-only; no bd, Makefile edits,
HANDOFF.md edits or docs/worklog edits. Design recorded in
docs/decisions/edit-mdv-s8.md.

## Next slice / remaining

§1's demonstrated torn-copy blocker is fixed and its scoped tests are green.
Coordinator must resolve or account for the pre-existing editor_close_test
failure and rerun the mandatory full check, including leak detection enabled.
The next savectl scope remains review §9 (FIFO acquisition), followed by
§§10–20. None of those fixes was started here. The existing optimistic
final-validation/publication race remains as documented by the file module.
