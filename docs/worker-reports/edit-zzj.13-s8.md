# edit-zzj.13 editor review integration decisions

Successful editor actions append recovery records before returning to layout continuations or publishing submit/present callbacks. Successful journal writes establish process-crash protection in the page cache; this change does not add a synchronous durability barrier to typing. Power-loss durability remains the transport's worker sync contract.

On admission/IO failure, stop the UI loop before publishing another frame. Compare accepted sequence before/after each logical call: journal-owned IO records must not be retried by the editor, while an unaccepted suffix retains its staging bytes and offsets. Explicit flush/exit may allocate and block. It first drains accepted records and, if suspended, creates a fresh complete checkpoint containing each retained buffer's BASE, whole current content, view, tab order/active index and window dimensions. Named buffers preserve and validate their original BASE; a conflict returns an error rather than silently changing identity. Persistent filesystem errors remain errors, preserving the earlier recovery generation where transport rotation guarantees it. Successful failure recovery does not resume the stopped UI loop.

Unwrapped editor layout seeds each logical row through exact piece-tree line queries and restricts a layout invocation to that row, preserving resumable layout phase across turns. Piece leaves cover a bounded byte span and newline counts are already warm in this editor. This avoids adding a new checkpoint worker lifecycle to the current loop. It does not solve whole-file warmup (review 8), giant Unicode cluster timing, or distant horizontal column approximation. Wrapped layout continues using its existing visual-row machinery.

Input batches reserve frame work. While the preceding backend frame is active, keep consuming ordered input: freezing that drain can prevent native completion packets behind a burst from being reached, overflow the downstream queue, and deadlock progress. Once the backend frees, stop admitting more actions into the current dirty frame after the batch bound and finish it. This guarantees work progress, not an absolute latency claim while remaining indivisible-operation findings are open.

Undo-restored insertion uses successive chunks of the existing staging arena. This removes the editor's staging-size rejection while keeping typing/replay free of libc allocation. Actual journal admission and replay slice timing remain separate concerns.

Backend initialization is an exclusive src/work bulk job. Publish an immutable integer result in a mailbox, selectively receive it on UI, and keep the configuration/state storage alive until the handle has physically finished. The UI only observes backend state after the mailbox handoff. Open remains synchronous; no new async cancellation API was introduced.

First-run directory creation retains parent descriptors, creates/opens a child and synchronizes the parent before descending. Existing components are also synchronized, covering retries after earlier failed barriers. Journal setup proceeds only after the whole chain succeeds. The filesystem barrier seam is setup-only and tested for namespace reachability and error propagation.

Main's existing ordered/bounded X11 drain and P4.I's final decoration repaint already address findings 1, 2 and 6. Tests preserve those behaviors; controlled fault runs establish that the inherited protections matter. No X11 module source delta remains.
EOF" in /home/tobias/Projects/sublimite/.wt/edit-zzj.13
 succeeded in 0ms:
diff --git a/src/editor/buffers.c b/src/editor/buffers.c
index 13d30f0c6031bd2d6bd6866d643d870e3a1dae1b..3f714fa24cd86458f3c8bf7f7c8ec555bf68e39d
--- a/src/editor/buffers.c
+++ b/src/editor/buffers.c
@@ -51,7 +51,11 @@
     if ((!bytes && len) || (path && strlen(path) >= IPC_PATH_CAP)) return EDITOR_ERR_ARG;
     editor_buffer *b = calloc(1, sizeof *b); if (!b) return EDITOR_ERR_MEMORY;
     int rc = 0;
+    b->base.path = "";
     if (path) {
+        /* Capture before the file worker loads any bytes, then validate the
+         * same generation after attachment and immediately before publication. */
+        if (e->journal) (void)journal_capture_base(path, &b->base);
         memcpy(b->path, path, strlen(path) + 1);
         rc = file_open_begin(&e->pool, path, NULL, &b->file);
         if (rc) goto fail;
@@ -79,6 +83,9 @@
     piece_allocator a = {&b->arena, piece_alloc, piece_free};
     b->tree = piece_create(&a); if (!b->tree) { rc = EDITOR_ERR_MEMORY; goto fail; }
     rc = b->file ? file_attach(b->file, b->tree) : piece_init_copy(b->tree, bytes, len); if (rc) goto fail;
+    if (e->journal && b->file && (!*b->base.path || journal_check_base(&b->base) || file_check(b->file, NULL))) {
+        rc = EDITOR_ERR_IO; goto fail;
+    }
     b->lines = piece_line_count(b->tree);
     b->history_cap = e->cfg.history_keys ? e->cfg.history_keys : 32768;
     if (b->history_cap > (SIZE_MAX - 8) / (2 * sizeof(editor_delta))) { rc = EDITOR_ERR_ARG; goto fail; }
@@ -139,9 +146,8 @@
 {
     int rc = 0;
     if (e->journal) {
-        journal_base base = {.path = b->path};
-        if (b->file) rc = journal_capture_base(file_path(b->file), &base);
-        if (!rc) rc = journal_set_base(e->journal, id, &base);
+        if (b->file) rc = journal_check_base(&b->base);
+        if (!rc) rc = journal_set_base(e->journal, id, &b->base);
         if (!rc && !b->file && piece_len(b->tree)) {
             piece_iter it; piece_iter_begin(&it, b->tree, 0); const uint8_t *p; size_t n; uint64_t off = 0;
             while (piece_iter_next(&it, &p, &n)) { rc = journal_insert(e->journal, id, off, p, n); if (rc) break; off += n; }
diff --git a/src/editor/editor.c b/src/editor/editor.c
index 82982c51604feed23a7173fe7ad9985435832020..7a6b8f513e3734cf11c4ef4754cda4e156d143ea
--- a/src/editor/editor.c
+++ b/src/editor/editor.c
@@ -7,6 +7,23 @@
 
 static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
 static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
+static int layout_slice(editor *e)
+{
+    layout *l = &e->lay;
+    if (l->wrap) return layout_run(l);
+    /* Piece leaves cover at most 64 KiB and their newline counts are warm.
+     * Seed each logical row independently: layout's bounded clipped-tail scan
+     * is allowed to stop without discovering the next line's newline. */
+    uint32_t end = l->row_end;
+    if (l->phase == 0 && l->row < end) {
+        uint64_t line = l->first_line + l->row;
+        e->row_byte[l->row] = line < e->buffer->lines ? piece_line_to_byte(e->tree, line) : LAYOUT_VOID_ROW;
+    }
+    if (l->row < end) l->row_end = l->row + 1u;
+    int rc = layout_run(l);
+    l->row_end = end;
+    return rc == LAYOUT_DONE && layout_busy(l) ? LAYOUT_MORE : rc;
+}
 static int fail(editor *e, int cause)
 {
     e->stats.error_cause = cause;
@@ -153,8 +170,8 @@
     e->frame.submit_ns = trace_now_ns(); e->active_frame = e->frame;
     if (e->frame.last_sequence) e->stats.submitted_sequence = e->frame.last_sequence;
     e->dirty = false;
+    e->frame_inputs = 0;
     if (e->cfg.on_submit) e->cfg.on_submit(e->cfg.hook_ctx, &e->frame);
-    editor_journal_staged(e);
     return 0;
 }
 static int resize(editor *e)
@@ -218,7 +235,7 @@
 static bool runnable(const editor *e)
 {
     bool can_drain = !e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES);
-    return (e->queue_count && can_drain) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
+    return (e->queue_count && can_drain && (!e->dirty || e->frame_inputs < 32u)) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
            (e->dirty && !e->backend->active) || (e->resize_pending && !e->backend->active && !e->dirty) ||
            (e->buffer->index_dirty && !lineidx_building(e->buffer->index));
 }
@@ -264,12 +281,14 @@
     for (;;) {
         e->stats.input_checks++;
         if (view_busy(&e->v)) rc = editor_continue_action(e);
-        else if (e->queue_count && (!e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES))) {
+        else if (e->queue_count && (!e->dirty || e->frame_inputs < 32u || e->queue[e->queue_head].kind == PLAT_EV_CLOSE) &&
+                 (!e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES))) {
             plat_event ev = e->queue[e->queue_head];
             e->queue_head = (e->queue_head + 1) % EDITOR_INPUT_CAP; e->queue_count--;
+            e->frame_inputs++;
             rc = ev.kind == PLAT_EV_KEY ? editor_handle_key(e, &ev) : nonkey(e, &ev);
         } else if (layout_busy(&e->lay)) {
-            uint64_t slice = trace_now_ns(); rc = layout_run(&e->lay);
+            uint64_t slice = trace_now_ns(); rc = layout_slice(e);
             uint64_t elapsed = trace_now_ns() - slice; e->stats.slices++;
             if (elapsed > e->stats.longest_slice_ns) e->stats.longest_slice_ns = elapsed;
             if (rc >= 0) rc = 0;
@@ -281,9 +300,17 @@
             if (rc >= 0) rc = 0;
         } else if (e->dirty && !e->paint_ready) rc = editor_paint_prepare(e);
         else break;
+        /* Protect every mutation before yielding to layout or publishing a
+         * frame. Successful journal calls establish the page-cache cutoff. */
+        editor_journal_staged(e);
+        if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
         if (rc) return fail(e, rc);
         if (e->quit || trace_now_ns() - start >= EDITOR_SLICE_NS) break;
     }
+    if (e->quit) {
+        rc = editor_flush(e);
+        return rc ? fail(e, EDITOR_ERR_IO) : EDITOR_CLOSED;
+    }
     rc = submit(e); if (rc) return rc;
     if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
     rc = present(e);
@@ -344,6 +371,9 @@
 {
     if (!e) return EDITOR_ERR_ARG;
     editor_journal_staged(e);
-    if (e->stats.journal_error) return e->stats.journal_error;
-    return e->journal ? journal_flush(e->journal) : 0;
+    if (!e->journal) return 0;
+    int rc = journal_flush(e->journal);
+    if (rc == JOURNAL_FULL || rc == JOURNAL_IO || e->stats.journal_error)
+        return editor_checkpoint(e);
+    return rc;
 }
diff --git a/src/editor/input.c b/src/editor/input.c
index 26132512e8bfaff5a017d3735dd7f806493922f4..f07c395079873b2d4cbdcdda53279e04492051bf
--- a/src/editor/input.c
+++ b/src/editor/input.c
@@ -63,21 +63,38 @@
 static int stage_tree(editor *e, uint64_t off, uint64_t n)
 {
     if (!n || !e->journal) return 0;
-    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
-    size_t count = (size_t)n;
-    int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
-    e->ops[e->op_count++] = (editor_jop){off, n, e->stage_used, true, e->buffer->id}; e->stage_used += count; return 0;
+    while (n) {
+        if (e->op_count == EDITOR_STAGE_OPS || e->stage_used == EDITOR_STAGE_BYTES) {
+            editor_journal_staged(e);
+            if (e->stats.journal_error) return EDITOR_ERR_IO;
+        }
+        size_t room = EDITOR_STAGE_BYTES - e->stage_used;
+        size_t count = n < room ? (size_t)n : room;
+        int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
+        e->ops[e->op_count++] = (editor_jop){off, count, e->stage_used, true, e->buffer->id};
+        e->stage_used += count; off += count; n -= count;
+    }
+    return 0;
 }
 void editor_journal_staged(editor *e)
 {
-    for (size_t i = 0; i < e->op_count; i++) {
-        editor_jop op = e->ops[i];
+    size_t consumed = 0;
+    while (consumed < e->op_count) {
+        editor_jop op = e->ops[consumed];
         if (e->stats.journal_error) break;
+        uint64_t before = journal_get_stats(e->journal).accepted_sequence;
         int rc = op.insert ? journal_insert(e->journal, op.id, op.off, e->stage + op.at, (size_t)op.len) :
                             journal_delete(e->journal, op.id, op.off, op.len);
-        if (rc) e->stats.journal_error = rc; else e->stats.journal_records++;
+        /* IO can accept a whole logical call into journal-owned retry storage.
+         * Do not duplicate it; FULL/BUSY accept nothing and stay in this queue. */
+        if (journal_get_stats(e->journal).accepted_sequence != before) {
+            consumed++; e->stats.journal_records++;
+        }
+        if (rc) { e->stats.journal_error = rc; break; }
     }
-    e->op_count = e->stage_used = 0;
+    e->op_count -= consumed;
+    if (e->op_count) memmove(e->ops, e->ops + consumed, e->op_count * sizeof *e->ops);
+    else e->stage_used = 0;
 }
 static editor_delta *delta_at(editor *e, size_t i) { return &e->buffer->history[(e->buffer->history_head + i) % e->buffer->history_cap]; }
 static void remember(editor *e, editor_delta d)
@@ -214,7 +231,7 @@
     if ((!redo && !e->buffer->history_cursor) || (redo && e->buffer->history_cursor == e->buffer->history_count)) return editor_refresh_cursor(e, e->old_cursor);
     editor_delta d = *delta_at(e, redo ? e->buffer->history_cursor : e->buffer->history_cursor - 1);
     uint64_t old = redo ? d.old : d.add, add = redo ? d.add : d.old;
-    if (e->journal && (e->op_count + 2 > EDITOR_STAGE_OPS || add > EDITOR_STAGE_BYTES - e->stage_used)) return EDITOR_ERR_MEMORY;
+    if (e->journal && e->op_count + 2 > EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
     uint64_t old_nl = newlines(e, d.off, old), before_len = piece_len(e->tree);
     undo_change c; int rc = redo ? undo_redo(e->undo, 1, &c) : undo_undo(e->undo, 1, &c);
     uint64_t after_len = piece_len(e->tree);
diff --git a/src/editor/private.h b/src/editor/private.h
index 52773370556595889bd9d2dc6aa3991006e52e4f..9623ad4e7a89254cfdbb623c6e2c8298cbb0d422
--- a/src/editor/private.h
+++ b/src/editor/private.h
@@ -20,6 +20,7 @@
     piece_tree *tree;
     undo_log undo;
     file *file;
+    journal_base base;
     lineidx *index;
     editor_delta *history;
     size_t history_head, history_count, history_cursor, history_cap;
@@ -68,6 +69,7 @@
     uint32_t max_cols, max_rows;
     plat_event queue[EDITOR_INPUT_CAP];
     size_t queue_head, queue_count;
+    size_t frame_inputs;
     editor_delta edit_delta;
     editor_jop *ops;
     uint8_t *stage;
@@ -99,6 +101,7 @@
 void editor_restart_blink(editor *e, uint64_t now);
 void editor_route_work(const work_msg *msg, void *ctx);
 void editor_journal_staged(editor *e);
+int editor_checkpoint(editor *e);
 int editor_handle_key(editor *e, const plat_event *ev);
 int editor_continue_action(editor *e);
 int editor_register_journal(editor *e, editor_buffer *b, uint64_t id);

diff --git a/src/editor/buffers.c b/src/editor/buffers.c
index 13d30f0c6031bd2d6bd6866d643d870e3a1dae1b..3f714fa24cd86458f3c8bf7f7c8ec555bf68e39d
--- a/src/editor/buffers.c
+++ b/src/editor/buffers.c
@@ -51,7 +51,11 @@
     if ((!bytes && len) || (path && strlen(path) >= IPC_PATH_CAP)) return EDITOR_ERR_ARG;
     editor_buffer *b = calloc(1, sizeof *b); if (!b) return EDITOR_ERR_MEMORY;
     int rc = 0;
+    b->base.path = "";
     if (path) {
+        /* Capture before the file worker loads any bytes, then validate the
+         * same generation after attachment and immediately before publication. */
+        if (e->journal) (void)journal_capture_base(path, &b->base);
         memcpy(b->path, path, strlen(path) + 1);
         rc = file_open_begin(&e->pool, path, NULL, &b->file);
         if (rc) goto fail;
@@ -79,6 +83,9 @@
     piece_allocator a = {&b->arena, piece_alloc, piece_free};
     b->tree = piece_create(&a); if (!b->tree) { rc = EDITOR_ERR_MEMORY; goto fail; }
     rc = b->file ? file_attach(b->file, b->tree) : piece_init_copy(b->tree, bytes, len); if (rc) goto fail;
+    if (e->journal && b->file && (!*b->base.path || journal_check_base(&b->base) || file_check(b->file, NULL))) {
+        rc = EDITOR_ERR_IO; goto fail;
+    }
     b->lines = piece_line_count(b->tree);
     b->history_cap = e->cfg.history_keys ? e->cfg.history_keys : 32768;
     if (b->history_cap > (SIZE_MAX - 8) / (2 * sizeof(editor_delta))) { rc = EDITOR_ERR_ARG; goto fail; }
@@ -139,9 +146,8 @@
 {
     int rc = 0;
     if (e->journal) {
-        journal_base base = {.path = b->path};
-        if (b->file) rc = journal_capture_base(file_path(b->file), &base);
-        if (!rc) rc = journal_set_base(e->journal, id, &base);
+        if (b->file) rc = journal_check_base(&b->base);
+        if (!rc) rc = journal_set_base(e->journal, id, &b->base);
         if (!rc && !b->file && piece_len(b->tree)) {
             piece_iter it; piece_iter_begin(&it, b->tree, 0); const uint8_t *p; size_t n; uint64_t off = 0;
             while (piece_iter_next(&it, &p, &n)) { rc = journal_insert(e->journal, id, off, p, n); if (rc) break; off += n; }
diff --git a/src/editor/editor.c b/src/editor/editor.c
index 82982c51604feed23a7173fe7ad9985435832020..7a6b8f513e3734cf11c4ef4754cda4e156d143ea
--- a/src/editor/editor.c
+++ b/src/editor/editor.c
@@ -7,6 +7,23 @@
 
 static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
 static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
+static int layout_slice(editor *e)
+{
+    layout *l = &e->lay;
+    if (l->wrap) return layout_run(l);
+    /* Piece leaves cover at most 64 KiB and their newline counts are warm.
+     * Seed each logical row independently: layout's bounded clipped-tail scan
+     * is allowed to stop without discovering the next line's newline. */
+    uint32_t end = l->row_end;
+    if (l->phase == 0 && l->row < end) {
+        uint64_t line = l->first_line + l->row;
+        e->row_byte[l->row] = line < e->buffer->lines ? piece_line_to_byte(e->tree, line) : LAYOUT_VOID_ROW;
+    }
+    if (l->row < end) l->row_end = l->row + 1u;
+    int rc = layout_run(l);
+    l->row_end = end;
+    return rc == LAYOUT_DONE && layout_busy(l) ? LAYOUT_MORE : rc;
+}
 static int fail(editor *e, int cause)
 {
     e->stats.error_cause = cause;
@@ -153,8 +170,8 @@
     e->frame.submit_ns = trace_now_ns(); e->active_frame = e->frame;
     if (e->frame.last_sequence) e->stats.submitted_sequence = e->frame.last_sequence;
     e->dirty = false;
+    e->frame_inputs = 0;
     if (e->cfg.on_submit) e->cfg.on_submit(e->cfg.hook_ctx, &e->frame);
-    editor_journal_staged(e);
     return 0;
 }
 static int resize(editor *e)
@@ -218,7 +235,7 @@
 static bool runnable(const editor *e)
 {
     bool can_drain = !e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES);
-    return (e->queue_count && can_drain) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
+    return (e->queue_count && can_drain && (!e->dirty || e->frame_inputs < 32u)) || view_busy(&e->v) || layout_busy(&e->lay) || e->extra_rows ||
            (e->dirty && !e->backend->active) || (e->resize_pending && !e->backend->active && !e->dirty) ||
            (e->buffer->index_dirty && !lineidx_building(e->buffer->index));
 }
@@ -264,12 +281,14 @@
     for (;;) {
         e->stats.input_checks++;
         if (view_busy(&e->v)) rc = editor_continue_action(e);
-        else if (e->queue_count && (!e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES))) {
+        else if (e->queue_count && (!e->dirty || e->frame_inputs < 32u || e->queue[e->queue_head].kind == PLAT_EV_CLOSE) &&
+                 (!e->journal || (e->op_count + 3 <= EDITOR_STAGE_OPS && e->stage_used + PLAT_UTF8_MAX <= EDITOR_STAGE_BYTES))) {
             plat_event ev = e->queue[e->queue_head];
             e->queue_head = (e->queue_head + 1) % EDITOR_INPUT_CAP; e->queue_count--;
+            e->frame_inputs++;
             rc = ev.kind == PLAT_EV_KEY ? editor_handle_key(e, &ev) : nonkey(e, &ev);
         } else if (layout_busy(&e->lay)) {
-            uint64_t slice = trace_now_ns(); rc = layout_run(&e->lay);
+            uint64_t slice = trace_now_ns(); rc = layout_slice(e);
             uint64_t elapsed = trace_now_ns() - slice; e->stats.slices++;
             if (elapsed > e->stats.longest_slice_ns) e->stats.longest_slice_ns = elapsed;
             if (rc >= 0) rc = 0;
@@ -281,9 +300,17 @@
             if (rc >= 0) rc = 0;
         } else if (e->dirty && !e->paint_ready) rc = editor_paint_prepare(e);
         else break;
+        /* Protect every mutation before yielding to layout or publishing a
+         * frame. Successful journal calls establish the page-cache cutoff. */
+        editor_journal_staged(e);
+        if (e->stats.journal_error) return fail(e, EDITOR_ERR_IO);
         if (rc) return fail(e, rc);
         if (e->quit || trace_now_ns() - start >= EDITOR_SLICE_NS) break;
     }
+    if (e->quit) {
+        rc = editor_flush(e);
+        return rc ? fail(e, EDITOR_ERR_IO) : EDITOR_CLOSED;
+    }
     rc = submit(e); if (rc) return rc;
     if (e->cfg.on_io) e->cfg.on_io(e->cfg.hook_ctx, true);
     rc = present(e);
@@ -344,6 +371,9 @@
 {
     if (!e) return EDITOR_ERR_ARG;
     editor_journal_staged(e);
-    if (e->stats.journal_error) return e->stats.journal_error;
-    return e->journal ? journal_flush(e->journal) : 0;
+    if (!e->journal) return 0;
+    int rc = journal_flush(e->journal);
+    if (rc == JOURNAL_FULL || rc == JOURNAL_IO || e->stats.journal_error)
+        return editor_checkpoint(e);
+    return rc;
 }
diff --git a/src/editor/input.c b/src/editor/input.c
index 26132512e8bfaff5a017d3735dd7f806493922f4..f07c395079873b2d4cbdcdda53279e04492051bf
--- a/src/editor/input.c
+++ b/src/editor/input.c
@@ -63,21 +63,38 @@
 static int stage_tree(editor *e, uint64_t off, uint64_t n)
 {
     if (!n || !e->journal) return 0;
-    if (e->op_count == EDITOR_STAGE_OPS || n > EDITOR_STAGE_BYTES - e->stage_used) return EDITOR_ERR_MEMORY;
-    size_t count = (size_t)n;
-    int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
-    e->ops[e->op_count++] = (editor_jop){off, n, e->stage_used, true, e->buffer->id}; e->stage_used += count; return 0;
+    while (n) {
+        if (e->op_count == EDITOR_STAGE_OPS || e->stage_used == EDITOR_STAGE_BYTES) {
+            editor_journal_staged(e);
+            if (e->stats.journal_error) return EDITOR_ERR_IO;
+        }
+        size_t room = EDITOR_STAGE_BYTES - e->stage_used;
+        size_t count = n < room ? (size_t)n : room;
+        int rc = piece_read(e->tree, off, e->stage + e->stage_used, count); if (rc) return rc;
+        e->ops[e->op_count++] = (editor_jop){off, count, e->stage_used, true, e->buffer->id};
+        e->stage_used += count; off += count; n -= count;
+    }
+    return 0;
 }
 void editor_journal_staged(editor *e)
 {
-    for (size_t i = 0; i < e->op_count; i++) {
-        editor_jop op = e->ops[i];
+    size_t consumed = 0;
+    while (consumed < e->op_count) {
+        editor_jop op = e->ops[consumed];
         if (e->stats.journal_error) break;
+        uint64_t before = journal_get_stats(e->journal).accepted_sequence;
         int rc = op.insert ? journal_insert(e->journal, op.id, op.off, e->stage + op.at, (size_t)op.len) :
                             journal_delete(e->journal, op.id, op.off, op.len);
-        if (rc) e->stats.journal_error = rc; else e->stats.journal_records++;
+        /* IO can accept a whole logical call into journal-owned retry storage.
+         * Do not duplicate it; FULL/BUSY accept nothing and stay in this queue. */
+        if (journal_get_stats(e->journal).accepted_sequence != before) {
+            consumed++; e->stats.journal_records++;
+        }
+        if (rc) { e->stats.journal_error = rc; break; }
     }
-    e->op_count = e->stage_used = 0;
+    e->op_count -= consumed;
+    if (e->op_count) memmove(e->ops, e->ops + consumed, e->op_count * sizeof *e->ops);
+    else e->stage_used = 0;
 }
 static editor_delta *delta_at(editor *e, size_t i) { return &e->buffer->history[(e->buffer->history_head + i) % e->buffer->history_cap]; }
 static void remember(editor *e, editor_delta d)
@@ -214,7 +231,7 @@
     if ((!redo && !e->buffer->history_cursor) || (redo && e->buffer->history_cursor == e->buffer->history_count)) return editor_refresh_cursor(e, e->old_cursor);
     editor_delta d = *delta_at(e, redo ? e->buffer->history_cursor : e->buffer->history_cursor - 1);
     uint64_t old = redo ? d.old : d.add, add = redo ? d.add : d.old;
-    if (e->journal && (e->op_count + 2 > EDITOR_STAGE_OPS || add > EDITOR_STAGE_BYTES - e->stage_used)) return EDITOR_ERR_MEMORY;
+    if (e->journal && e->op_count + 2 > EDITOR_STAGE_OPS) return EDITOR_ERR_MEMORY;
     uint64_t old_nl = newlines(e, d.off, old), before_len = piece_len(e->tree);
     undo_change c; int rc = redo ? undo_redo(e->undo, 1, &c) : undo_undo(e->undo, 1, &c);
     uint64_t after_len = piece_len(e->tree);
diff --git a/src/editor/private.h b/src/editor/private.h
index 52773370556595889bd9d2dc6aa3991006e52e4f..9623ad4e7a89254cfdbb623c6e2c8298cbb0d422
--- a/src/editor/private.h
+++ b/src/editor/private.h
@@ -20,6 +20,7 @@
     piece_tree *tree;
     undo_log undo;
     file *file;
+    journal_base base;
     lineidx *index;
     editor_delta *history;
     size_t history_head, history_count, history_cursor, history_cap;
@@ -68,6 +69,7 @@
     uint32_t max_cols, max_rows;
     plat_event queue[EDITOR_INPUT_CAP];
     size_t queue_head, queue_count;
+    size_t frame_inputs;
     editor_delta edit_delta;
     editor_jop *ops;
     uint8_t *stage;
@@ -99,6 +101,7 @@
 void editor_restart_blink(editor *e, uint64_t now);
 void editor_route_work(const work_msg *msg, void *ctx);
 void editor_journal_staged(editor *e);
+int editor_checkpoint(editor *e);
 int editor_handle_key(editor *e, const plat_event *ev);
 int editor_continue_action(editor *e);
 int editor_register_journal(editor *e, editor_buffer *b, uint64_t id);

---

## Final message (codex -o)

Completed/adopted blockers 1–7, findings 12/23, journal review §2/3/5, and flushed WM close.

`make all` and `make check` pass. Typing allocations: 0 (M)[AC]. Timed fuzz ran 61 s clean (M)[AC]; a separate inherited failure is preserved. LeakSanitizer was disabled.

Finding 14 was withdrawn. Remaining work and red/green evidence are in the [required report](/home/tobias/Projects/sublimite/.wt/edit-zzj.13/docs/worker-reports/edit-zzj.13-s8.md). No git writes.