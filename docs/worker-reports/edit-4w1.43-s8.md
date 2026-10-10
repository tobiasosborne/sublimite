# edit-4w1.43 — session eight

The inherited WIP implements file-1 §§17–18 and §§20–23 and the safe/private
original portions of §19. **The bead is incomplete:** keep after damaged mapped
backing still refuses, consistent with the settled piece contract. The failing
`FT_CASE=19mapped` proposal remains available. The coordinator must not close
full §19 on the passing ordinary suite alone.

Session eight strengthened fortified-read injection and added a red-first fix
for deletion/symlink replacement between canonicalization and source open.
Design and remaining cross-module prerequisite:
[`edit-4w1.43.md`](../decisions/edit-4w1.43.md).

Verification and pasted per-finding red/green evidence are appended below before
handoff. No git write command, new production global, or real-display window.

## Per-finding results and evidence

Observed correctness results below are (M)[AC], BAT0 `Not charging`.
The storage bound is (G), not a loaded-box timing gate. Main-source red runs
and WIP green runs were performed back to back; no timings are compared.
The original WIP sessions did not leave their red logs in this worktree. The
reproductions below use the inherited regression tests against `main` source
compiled into a separate object with the current public header. They establish
that each regression rejects the old implementation; they do not retroactively
```text
tests/file_test.c:1700: FAIL file_resolve_keep(f) == FILE_OK && !file_changed(f)
finding19 mapped proposal: FAIL
file_test: 1 FAILED
```

No safe file-only fix can restore immutable snapshot bytes and cached newline
counts after in-place mapped truncation. The ordinary suite includes the settled
safe-refusal regression; the proposed new contract stays explicitly red under
`FT_CASE=19mapped`. The required coordinated acquisition/rebase work is described
in the decision document. This is a remaining in-scope acceptance gap, not a
passed or waived finding. Other file-1 findings outside §§17–23 were not changed.

## Reproduction

After `make all`, build the wrapped targeted executable:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE \
  -DFILE_SEMANTICS_WRAP -pthread -O2 -g -Isrc -include tests/display_guard.h \
  tests/file_test.c build/libedit.a \
  -Wl,--wrap=malloc,--wrap=calloc,--wrap=free,--wrap=lstat,--wrap=fstat \
  -Wl,--wrap=pread,--wrap=__pread_chk,--wrap=fsetxattr,--wrap=open \
  -lm -ldl -o build/s8-evidence/file-green
DISPLAY=:99 EDIT_DISPLAY=:99 FT_CASE=18 build/s8-evidence/file-green
```

`FT_CASE` accepts each review section from 17 through 23 and the separately
failing `19mapped` proposal. Sanitizer reproduction replaces gcc/release library
with clang/sanitizer library and adds address/undefined sanitizer flags, with
leak detection disabled. The main red object came from the read-only command
`git show main:src/file/file.c`; it was linked before the current archive. No
production source was swapped to perform the reproduction.


 succeeded in 65202ms:
Not charging
18.60 15.24 7.32 11/2464 4
final file fuzz exit=0

diff --git a/HANDOFF.md b/HANDOFF.md
index e73712d60fed64024feefc92559008554676a815..c1b6c2cd7afcb3ca4f9bba44e5c0a3ff878d62b9
--- a/HANDOFF.md
+++ b/HANDOFF.md
@@ -1,5 +1,13 @@
 # HANDOFF — sublimité, session 6 wound down (updated 2026-10-09 17:40)
 
+Worker continuation, 2026-10-10 / session eight: edit-4w1.43 has targeted
+red/main and green/WIP evidence for file-1 §§17–23 plus a red-first fix for
+canonicalization-to-open deletion/symlink races. Full §19 remains incomplete:
+damaged mapped keep requires stable originals or a coordinated piece rebase
+contract; the safe refusal remains, and `FT_CASE=19mapped` is explicitly red.
+See docs/worker-reports/edit-4w1.43-s8.md and
+docs/decisions/edit-4w1.43.md before deciding closure. No worker git writes.
+
 Read: this → `CLAUDE.md` → `PLAN.md` §1 and §5 → `bd ready --type task` and `bd ready -n 40`. Session details: `docs/worklog/2026-10-09.md` (sessions 4, 5 and the crash recovery), `docs/worklog/2026-10-08.md` (sessions 2, 3). Session 1's long handoff is in git history (commit 20e9887); its §2 settled decisions still bind.
 
 ## Method (session 4–5, keep)
diff --git a/docs/decisions/edit-4w1.43.md b/docs/decisions/edit-4w1.43.md
new file mode 100644
index 0000000000000000000000000000000000000000..7257c1e966391fed3c3b208cc91d2a022d8bf694
--- /dev/null
+++ b/docs/decisions/edit-4w1.43.md
@@ -0,0 +1,73 @@
+# P1.7e — file keep/check/save semantics
+
+Scope: `docs/reviews/file-1.md` §§17–23, `src/file`, and its regressions.
+Continuation of the inherited session-six/session-seven WIP. The worker leaves
+git history unchanged; the coordinator commits and decides bead closure.
+
+- §17: COPY allocates one private original on the prefix worker. The prefix
+  aliases its head, BULK fills its suffix, and attachment shares it through
+  existing piece lifetime hooks. The allocation stays alive through the last
+  snapshot. No full-sized attachment copy or separate retained prefix exists.
+  Wrapped allocation accounting covers open, attachment, snapshot, destruction,
+  peak storage and final release against G10f (G).
+- §18: descriptor and canonical entry identities are validated around prefix
+  and full acquisition. Identity includes ctime, permission bits, ownership and
+  type. A short read before the captured length is `FILE_ERR_CHANGED`, never
+  successful shortened content. Failed acquisition withdraws the prefix and
+  full generation and installs sticky changed state through mailbox decode.
+  Session eight additionally treats ENOENT, ENOTDIR and ELOOP from `open` after
+  successful canonicalization as a changed acquisition. Other errors retain
+  their I/O classification and errno. No descriptor/stat work moves onto UI.
+- §19: both-absent identities compare equal. Keep can accept deletion,
+  truncation and replacement of private originals, and deletion/replacement
+  of intact mapped originals, while preserving edits and retained snapshots.
+  Missing-path recreation retains the last accepted permission bits. Unexpected
+  pathname/backing stat errors refuse keep without changing its baseline.
+- §20: `file_check` returns sticky state; `reasons` describes the current
+  comparison, including zero when a previously detected change remains pending.
+- §21: inotify observes parent/name, so rename-over and deletion/recreation
+  remain observable. Keep refreshes off path, poll handles ignored/moved/deleted
+  watches, and failed setup returns failure even if an inotify fd remains open.
+  Poll still reads one bounded buffer and queues asynchronous identity work.
+- §22: save captures the current target's metadata through a validated worker
+  descriptor. It preserves ownership/group, supported xattrs (including ACLs
+  and labels), removes differing inherited attributes, and applies mode after
+  ownership/ACL installation, before data fsync. Metadata changes refuse normal
+  saves. Preservation errors refuse replacement and remove owned temps; immutable
+  attributes already equal on the temp need no successful set. Explicit mode
+  zero is distinguished from the default for new targets. An unreadable existing
+  mode-zero target safely fails with EACCES instead of widening permissions.
+- §23: capture and commit use `fstatat` with `AT_SYMLINK_NOFOLLOW` against the
+  pinned save parent. A replacement symlink refuses even FORCE/no-baseline saves.
+  Initially opened symlinks still resolve to their canonical target.
+
+## Damaged mapped generations remain incomplete
+
+Keep still refuses modified/faulted mapped backing. This preserves the settled
+containment decision in `P1.7c.md` §5. `piece.h` requires unchanged originals
+through every tree/snapshot owner and exposes no operation to rebase cached
+newline metadata or invalidate immutable snapshots. Detaching or zero-filling
+the mapping after truncation cannot recover discarded original bytes or repair
+those caches. `FT_CASE=19mapped` retains the explicit failing requested-keep
+oracle, outside the ordinary passing suite. Full §19 acceptance is unclaimed.
+
+A future coordinated design must provide stable independent original generations
+at acquisition or a reviewed piece/index/undo rebase contract. This worker did
+not change those modules or their headers. A forced save does not rehabilitate
+the faulted backing or clear its recovery epoch. It therefore does not solve
+subsequent ordinary save/keep of a damaged mapping.
+
+## Verification policy
+
+The production object retains only the sanctioned mutable `file_bus` service.
+Regression-only mutable state is confined to test executables. The strengthened
+syscall wrapper covers both `pread` and fortified `__pread_chk`; the injected
+read oracle asserts that the injection actually ran. Test-only open interception
+reproduces pathname races before any prefix read.
+
+Session-eight evidence is in `docs/worker-reports/edit-4w1.43-s8.md`. Main-source
+reproduction uses a separate object under `build/s8-evidence`, without swapping
+the worktree source or performing git writes. That reconstructed red run is not
+a claim that original WIP session logs survived. No latency measurements or gate
+verdicts are claimed; no benchmark variant was selected. Leak-enabled sanitizer
+verification remains coordinator work.
diff --git a/docs/worker-reports/edit-4w1.43-s8.md b/docs/worker-reports/edit-4w1.43-s8.md
new file mode 100644
index 0000000000000000000000000000000000000000..40f849fb9c44eb4dba6636cd28ed40311a4e6adb
--- /dev/null
+++ b/docs/worker-reports/edit-4w1.43-s8.md
@@ -0,0 +1,15 @@
+# edit-4w1.43 — session eight
+
+The inherited WIP implements file-1 §§17–18 and §§20–23 and the safe/private
+original portions of §19. **The bead is incomplete:** keep after damaged mapped
+backing still refuses, consistent with the settled piece contract. The failing
+`FT_CASE=19mapped` proposal remains available. The coordinator must not close
+full §19 on the passing ordinary suite alone.
+
+Session eight strengthened fortified-read injection and added a red-first fix
+for deletion/symlink replacement between canonicalization and source open.
+Design and remaining cross-module prerequisite:
+[`edit-4w1.43.md`](../decisions/edit-4w1.43.md).
+
+Verification and pasted per-finding red/green evidence are appended below before
+handoff. No git write command, new production global, or real-display window.
diff --git a/docs/worklog/2026-10-10.md b/docs/worklog/2026-10-10.md
new file mode 100644
index 0000000000000000000000000000000000000000..3ad6eb38d6ed2b702fe4fa56956ae73519f751d7
--- /dev/null
+++ b/docs/worklog/2026-10-10.md
@@ -0,0 +1,15 @@
+# 2026-10-10
+
+Session-eight worker continuation, edit-4w1.43 / P1.7e: inspected the inherited
+WIP and file-1 §§17–23. Reproduced each targeted regression against main source
+without changing git state. Strengthened the syscall harness to intercept
+fortified prefix reads and confirm injection consumption. Added a failing
+canonicalization-to-open deletion/symlink-race test, then fixed acquisition to
+publish sticky `FILE_ERR_CHANGED` for those races.
+
+Full damaged-mapping keep remains incomplete under the settled immutable piece
+contract. Private originals and intact mapped deletion/replacement are covered;
+the explicit `FT_CASE=19mapped` proposal remains red. No unrelated module change,
+new production global, or real-display window. Final verification and public
+red/green evidence: docs/worker-reports/edit-4w1.43-s8.md. Design rationale and
+the coordinated remaining prerequisite: docs/decisions/edit-4w1.43.md.
diff --git a/src/file/STATUS.md b/src/file/STATUS.md
index 784d9ca3ba48c134b1573dec6c42c310617d3aff..09381930c4b7f15aa33aace8f38ccec5baf1ca69
--- a/src/file/STATUS.md
+++ b/src/file/STATUS.md
@@ -1,5 +1,16 @@
 # File module status
 
+P1.7e / edit-4w1.43 session-eight continuation: §§17–18, 20–23 have targeted
+red/main and green/WIP coverage. §19 private-original keep and intact mapped
+deletion/replacement work; damaged mapped keep remains refused under P1.7c §5.
+The explicit `FT_CASE=19mapped` requested-keep oracle remains red and is excluded
+from ordinary passing suites. Do not close full §19 without the coordinated
+stable-original/rebase design described in docs/decisions/edit-4w1.43.md.
+Session eight fixes acquisition state when canonicalization-to-open races lose
+the entry or replace it with a symlink, and covers fortified prefix reads in
+the syscall-injection harness. Final evidence is in
+docs/worker-reports/edit-4w1.43-s8.md. No unrelated module implementation changed.
+
 P1.7/P1.7b and P1.7c safety fixes remain. P1.7d (edit-4w1.42) implements
 file-1 §§11–13, 24–25, 31–32 and copy/scan elimination in §16.
 See docs/decisions/P1.7d.md for each verdict and exact red/green lines.
diff --git a/src/file/file.c b/src/file/file.c
index ce0fe1a8934abf734deda839ce5d92cd6b297dab..292694034334e1ea07b6988ff85fe6a9cd0200f8
--- a/src/file/file.c
+++ b/src/file/file.c
@@ -569,7 +569,14 @@
     }
     /* NONBLOCK is required before fstat: FIFOs must not await a writer. */
     fd = open(f->prefix_result.path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
-    if (fd < 0 || fstat(fd, &st) != 0) { en = errno; rc = FILE_ERR_IO; goto done; }
+    if (fd < 0) {
+        en = errno;
+        /* realpath just established this canonical entry. Disappearance or a
+         * new symlink invalidates that acquisition, including before reads. */
+        rc = en == ENOENT || en == ENOTDIR || en == ELOOP ? FILE_ERR_CHANGED : FILE_ERR_IO;
+        goto done;
+    }
+    if (fstat(fd, &st) != 0) { en = errno; rc = FILE_ERR_IO; goto done; }
     if (!S_ISREG(st.st_mode)) { rc = FILE_ERR_NOTREG; goto done; }
     if (st.st_size < 0 || (uint64_t)st.st_size > SIZE_MAX) { en = EFBIG; rc = FILE_ERR_IO; goto done; }
     id_from_stat(&f->prefix_result.id, &st);
diff --git a/tests/file_test.c b/tests/file_test.c
index e274669b08746842964ccd773f99e9ff1b6474db..b7b827ebe72f88bf7c8887f11da929c61b3185fd
--- a/tests/file_test.c
+++ b/tests/file_test.c
@@ -1392,7 +1392,7 @@
 #ifdef FILE_SEMANTICS_WRAP
 static _Atomic int semantic_xattr_error, semantic_restore_stat;
 #ifndef FILE_REVIEW_WRAP
-static _Atomic int semantic_fstat_error, semantic_pread_mode;
+static _Atomic int semantic_fstat_error, semantic_pread_mode, semantic_open_swap;
 static off_t semantic_pread_offset;
 #endif
 static char semantic_source[512];
@@ -1416,6 +1416,22 @@
     return __real_lstat(path, st);
 }
 #ifndef FILE_REVIEW_WRAP
+int __real_open(const char *, int, ...);
+int __wrap_open(const char *path, int flags, ...)
+{
+    mode_t mode = 0;
+    if (flags & O_CREAT) {
+        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
+    }
+    if (strcmp(path, semantic_source) == 0) {
+        int action = atomic_exchange(&semantic_open_swap, 0);
+        if (action) {
+            EDIT_ASSERT(unlink(path) == 0);
+            if (action == 2) EDIT_ASSERT(symlink("acquire-name-hard", path) == 0);
+        }
+    }
+    return __real_open(path, flags, mode);
+}
 int __real_fstat(int, struct stat *);
 int __wrap_fstat(int fd, struct stat *st)
 {
@@ -1438,6 +1454,13 @@
     }
     return __real_pread(fd, bytes, size, off);
 }
+/* gcc's fortified prefix reads use this ABI instead of pread. Exercise the
+ * same injection oracle in both builds; keep the capacity check explicit. */
+ssize_t __wrap___pread_chk(int fd, void *bytes, size_t size, off_t off, size_t capacity)
+{
+    EDIT_ASSERT(size <= capacity);
+    return __wrap_pread(fd, bytes, size, off);
+}
 #endif
 #endif
 static void t_semantic_storage(void)
@@ -1770,6 +1793,7 @@
         atomic_store(&semantic_pread_mode, action);
         file *f = NULL; file_msg m; CHECK(file_open_begin(&pool, semantic_source, NULL, &f) == FILE_OK);
         CHECK(semantic_open_terminal(&m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_CHANGED);
+        CHECK(atomic_load(&semantic_pread_mode) == 0);
         CHECK(!file_open_ready(f) && !file_prefix_ready(f) && file_changed(f));
         size_t len = 99; CHECK(file_prefix(f, &len) == NULL && len == 0);
         atomic_store(&semantic_pread_mode, 0); file_close(f);
@@ -1777,10 +1801,33 @@
     }
 #endif
 }
+static void t_semantic_open_entry_race(void)
+{
+#if defined(FILE_SEMANTICS_WRAP) && !defined(FILE_REVIEW_WRAP)
+    char hard[512]; path_of(hard, sizeof hard, "acquire-name-hard");
+    path_of(semantic_source, sizeof semantic_source, "acquire-name");
+    for (int action = 1; action <= 2; action++) {
+        write_file("acquire-name", (const uint8_t *)"original", 8);
+        CHECK(link(semantic_source, hard) == 0);
+        atomic_store(&semantic_open_swap, action);
+        file *f = NULL; file_msg m;
+        CHECK(file_open_begin(&pool, semantic_source, NULL, &f) == FILE_OK);
+        CHECK(semantic_open_terminal(&m) && m.kind == FILE_MSG_OPEN_FAILED && m.status == FILE_ERR_CHANGED);
+        CHECK(atomic_load(&semantic_open_swap) == 0);
+        CHECK(file_changed(f) && !file_open_ready(f) && !file_prefix_ready(f));
+        size_t n = 99; CHECK(file_prefix(f, &n) == NULL && n == 0);
+        struct stat st;
+        if (action == 2) CHECK(lstat(semantic_source, &st) == 0 && S_ISLNK(st.st_mode));
+        file_close(f); CHECK(unlink(hard) == 0);
+        if (action == 2) CHECK(unlink(semantic_source) == 0);
+        coll c = {0}; (void)work_mailbox_drain(&pool, on_msg, &c);
+    }
+#endif
+}
 static int semantic_dispatch(const char *which)
 {
     if (strcmp(which, "17") == 0) review_run("finding17", t_semantic_storage);
-    else if (strcmp(which, "18") == 0) { review_run("finding18", t_semantic_open_version); review_run("finding18 injected reads", t_semantic_wrapped_acquisition); }
+    else if (strcmp(which, "18") == 0) { review_run("finding18", t_semantic_open_version); review_run("finding18 injected reads", t_semantic_wrapped_acquisition); review_run("finding18 entry race", t_semantic_open_entry_race); }
     else if (strcmp(which, "19") == 0) { review_run("finding19 missing", t_semantic_keep_missing); review_run("finding19 I/O", t_semantic_keep_stat_error); review_run("finding19 intact generations", t_semantic_keep_generations); review_run("finding19 backing I/O", t_semantic_keep_backing_error); }
     else if (strcmp(which, "20") == 0) { review_run("finding20", t_semantic_sticky); review_run("finding20 exact baseline", t_semantic_wrapped_sticky); }
     else if (strcmp(which, "21") == 0) { review_run("finding21", t_semantic_watch_keep); review_run("finding21 refresh failure", t_semantic_watch_refresh_error); }

---

## Final message (codex -o)

Updated WIP with red-first acquisition and keep-version fixes. **§19 damaged mapped keep remains incomplete.**

`make all`, `make check`, and allocation guards pass. Final fuzz ran clean for 61 seconds (M)[AC]. LeakSanitizer was disabled; the coordinator must rerun with leaks enabled. No git writes.

[Required report](/home/tobias/Projects/sublimite/.wt/edit-4w1.43/docs/worker-reports/edit-4w1.43-s8.md) includes red/green evidence and remaining work.