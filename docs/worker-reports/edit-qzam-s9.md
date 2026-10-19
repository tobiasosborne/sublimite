# edit-qzam (s9): worker report

Worker, 2026-10-20. Worktree `.wt/edit-qzam-s9` (main 1f32a31). Every display
run used `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:98 EDIT_DISPLAY=:98
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh`, outside the sandbox.
Power: `Not charging` (BAT0 status) for every run.

## Finding

The lock is not held by an earlier case. Each case has its own mkdtemp root,
and the guard the test locks is that root directory's inode. The holder is
ivv_choice's own editor. The edit-fi2/7cj8 BASE copy is started by the first
edit `x` and holds the directory guard `LOCK_SH` for the whole copy. When the
copy outlasts `stale_prompt`'s settle, the test's `LOCK_EX|LOCK_NB` is
refused. A finished copy's reference checkpoint (a worker `journal_rotate`)
takes the same shared guard. The y315 close path is not involved.
See docs/decisions/edit-qzam.md.

## Files touched

- tests/dataloss_test.c: new `guard_quiet` / `wait_guard_quiet`, called in
  `ivv_choice` before the guard is taken. The assertion at the guard is
  unchanged.
- docs/decisions/edit-qzam.md (new).
- docs/worker-reports/edit-qzam-s9.md (this file).

## Red

Unmodified main, full run, 06:01:35, load1 4.66:
```
dataloss_test:1121: FAIL guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0
dataloss_test:1690: FAIL ivv_choice(false) == 0
rc=1
```
The run was instrumented to print the state when the flock is refused
(temporary, since removed). Of two runs, the first passed (timing-dependent);
the second failed at 06:03, load1 3.37:
```
QZAM: flock refused errno=11 root=build/dataloss-gRhqJP ino=18770046 retain_state=1 retain_used=0 save_active=0
```
`retain_state=1` is `EDITOR_RETAIN_RUNNING`. The root held only `source`,
the `session-*` journal and its `.lock`, so no earlier case's file was there.

## Green: three consecutive full runs of san dataloss_test

| Run | Start | load1 | Exit |
|---|---|---|---|
| 1 | 06:05:06 | 2.01 | 0 |
| 2 | 06:05:51 | 2.06 | 0 |
| 3 | 06:06:37 | 3.34 | 0 |

Each run printed `edit-qzam own guard holders done: retain_state=0
retain_used=0` (twice, once per case) and the final "... passed" line. The
cases after ivv_choice that had not run since the failure now run in the
default sequence and pass:
- save_during_prompt;
- hot_exit_mixed;
- reload_vs_keystroke;
- two_process_save;
- torn_undo_group;
- stranded_orphan ×3.

## Verification

| Command | Exit |
|---|---|
| `make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{dataloss,editor_session,editor}_test` | 0 |
| san dataloss_test full, ×3 (above) | 0, 0, 0 |
| san dataloss_test --edit-ivv, 06:09:31, load1 9.28 | 0 |
| san editor_session_test | 0 |
| san editor_test, 06:10:07 to 06:13:10, load1 2.59 at the end | 0 |
| release build/tests/editor_test, EDITOR_ALLOC_ONLY=null: `null 10000 keys mallocs=0 guard=active` | 0 |
| release build/tests/editor_test, EDITOR_ALLOC_ONLY=raster: `raster 10000 keys mallocs=0 guard=active` | 0 |

## Diff

```diff
--- /tmp/claude-1000/-home-tobias-Projects-sublimite/2d812017-7f93-44c2-a0ab-fe83e6bed443/scratchpad/dataloss_test.c.orig	2026-10-20 06:01:53.291895434 +1100
+++ tests/dataloss_test.c	2026-10-20 06:04:47.154386482 +1100
@@ -1107,6 +1107,28 @@
     remove_tree(root); return rc;
 }
 
+/* edit-qzam: this editor's own jobs that hold the session directory guard
+ * LOCK_SH (journal.c): the edit-fi2/7cj8 BASE copy (RUNNING) and the worker
+ * checkpoint (e->publish, edit-ze59) that references a READY copy or, for an
+ * unprotected stale BASE, the edit-7cj8 detached original. Both follow the
+ * first edit by themselves; a test that takes the guard LOCK_EX|LOCK_NB to
+ * stand in for another editor's sweep waits until neither runs or is owed. */
+static bool guard_quiet(const editor *e)
+{
+    const editor_buffer *b = e->buffer;
+    if (e->publish || b->retain_state == EDITOR_RETAIN_RUNNING) return false;
+    if (b->retain_state == EDITOR_RETAIN_READY) return b->retain_used;
+    return !editor_retain_unprotected(b);
+}
+static int wait_guard_quiet(editor *e)
+{
+    uint64_t limit = deadline();
+    while (!guard_quiet(e)) { T(turn(e) == 0); T(trace_now_ns() < limit); sched_yield(); }
+    fprintf(stderr, "dataloss_test: edit-qzam own guard holders done: retain_state=%u retain_used=%d\n",
+        e->buffer->retain_state, e->buffer->retain_used);
+    return 0;
+}
+
 /* edit-ivv (d): Reload/Keep chosen while another editor's directory sweep
  * holds the shared guard LOCK_EX (journal_test --rotate-busy pattern): the
  * choice survives the refused checkpoint and lands on a later journal tick.
@@ -1118,6 +1140,7 @@
     T(make_session(root, jpath, sizeof jpath) == 0);
     render_backend backend; editor *e; T(open_editor(&e, &backend, source, jpath) == 0);
     T(press(e, 'x', 0, "x") == 0); T(stale_prompt(e, source, "external") == 0);
+    T(wait_guard_quiet(e) == 0); /* edit-qzam: no own job holds or is owed the guard */
     int guard = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC); T(guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0);
     T(press(e, reload ? 'r' : 'k', 0, NULL) == 0);
     uint64_t limit = deadline();
```

## Out of scope (reported, not changed)

- Only one other test takes the guard `LOCK_EX|LOCK_NB` while an editor is
  live: e3z5_save_as (dataloss_test ~1500). It already waits on
  `retain_settled` first, but that helper does not wait for an in-flight
  `e->publish` or an owed edit-7cj8 detached checkpoint. No failure was
  observed in these runs.
- The bead's y315 hypothesis was not exercised: editor_close returns void,
  and no case's close was implicated.
