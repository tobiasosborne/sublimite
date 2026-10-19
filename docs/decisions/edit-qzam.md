# edit-qzam: ivv_choice waits for its own editor's directory-guard jobs

Bead edit-qzam (make check red on main: dataloss_test:1121). Worker session
s9, 2026-10-20. Test only: tests/dataloss_test.c (`guard_quiet`,
`wait_guard_quiet`, called in `ivv_choice`). No editor or journal code changed.

## Cause

The bead's first hypothesis (an earlier case's editor_close keeps its session
lock past the edit-y315 deadline) does not apply. Every case makes its own
directory (`make_root`: mkdtemp), so an earlier case's lock is on a different
inode, and the test's guard is the directory inode itself, not the
`.session-*.lock` file. Instrumented red run (failure printed the state on
refusal): `flock refused errno=11 ... retain_state=1 retain_used=0
save_active=0`. State 1 is `EDITOR_RETAIN_RUNNING`. The lock holder is the case's
own editor. Its edit-fi2 BASE copy of `source` ("base" fits under
`retain_limit`) is started by the first edit. The copy holds the directory
guard `LOCK_SH|LOCK_NB` for its whole copy, fsync and promotion (edit-7cj8:
"the shared directory lease starts before creation"). The test then takes
`LOCK_EX|LOCK_NB` to stand in for another editor's sweep, and is refused
whenever that copy is still running. A copy that has finished is followed by
its reference checkpoint, `editor_publish_start`'s worker `journal_rotate`,
which takes the same shared guard. Alone, or on a quiet box, the copy is
done before `stale_prompt` settles, so the case passes only when the timing
allows it.

## Choice

Before taking the guard, `ivv_choice` turns the editor until none of its own
guard holders is running or owed:
- no copy RUNNING;
- no worker publication in flight (`e->publish`);
- no READY copy waiting for its reference checkpoint (`!retain_used`);
- no edit-7cj8 unprotected BASE waiting for its detached checkpoint.

Jobs are submitted only from `editor_step` on the test thread, and no step
runs between that check and the `flock`. So no own job holds the lease when
the test asks for it. While the test holds it, nothing is owed that would
publish, so `busy_refusals` counts only the Reload/Keep checkpoint under
test. A retention restart from IDLE is refused BUSY and returns to IDLE
silently; retention errors do not touch `busy_refusals`. This is the
`retain_settled` precondition that e3z5_save_as already uses before its
guard, plus the publication and 7cj8 terms. The assertion is unchanged.

Not chosen:
- `retain_limit = 0`. It also disables the 7cj8 fallback and changes the
  Reload/Keep path under test.
- Retrying the `LOCK_NB` without turning the editor. A READY copy would then
  publish under the held guard, and its refusal would count as the choice's.
- Any change to y315's close path. It is not involved.

## Numbers

(E) The wait's watchdog is the file's `deadline()`, EDITOR_IDLE_NS, the
existing test escape policy; no new duration. Green full runs: (M) [Not
charging, load1 2.01 to 3.34] three consecutive exits 0; the wait ended with
`retain_state=0 retain_used=0` in each run.
