# edit-n95: find in files UI (edit-338 part 2)

Coordinator decisions from the brief are recorded here with the choices made
while coding. Code: `src/editor/fif.c` (new), glue in `findpanel.c`,
`input.c`, `buffers.c`, `editor.c`, `checkpoint.c`, `session.c`, `clip.c`;
engine fixes in `src/fif/worker.c`, `src/fif/fif.c`, `src/fif/fif.h`.

## Engine fixes (src/fif)

1. **Symlinked root.** `fif_worker` init uses `stat()` on the root, so
   `~/proj -> /data/proj` is walked. Entries below the root keep the `lstat`
   symlink-skip rule (loops and external links are still not followed).
2. **Unreadable entries.** `io_error()` only bumps `DONE.errors`; the walk
   continues and `DONE.code` stays `FIF_OK`. `FIF_ERR_IO` is reserved for a
   walk that never started: the root is missing, not a directory, or its own
   `opendir`/`fstat` fails (`root_error`). This supersedes the 34l sentence
   "the aggregate code records I/O errors". The results tab shows
   `N unreadable entries` (singular `1 unreadable entry`) as a warning row.
3. **Context cut at a UTF-8 boundary.** New `fif_context_cut(line, length,
   cap)`: a line longer than the cap (FIF_DEFAULT_CONTEXT, 1024 bytes (E),
   34l derivation) is cut at the cap and moved back over at most three
   continuation bytes ((P): a UTF-8 scalar is at most four bytes). The engine
   stores contexts cut this way and keeps `context_truncated`; the results tab
   appends Sublime's ` ...` to a truncated line. The cut keeps the start of
   the line (the 34l doc's "truncated from the beginning" did not match the
   code, which always kept the start; Sublime keeps the start too).
   `fuzz/fif_fuzz.c` checks the cut's invariants on every input.

## Panel

- Ctrl+Shift+F (`KEYS_ACTION_FIND_IN_FILES`) opens the existing find panel
  with `findui_show(open, replace_mode=false)` and sets `e->fif.mode`.
  The panel is three rows (`editor_text_geometry`): row 0 is findui's Find
  field with its toggles, row 1 is repainted as `Where: <field>`, row 2 as the
  key hints. Painting is in `editor_fif_paint`, called after `findui_render`.
- The Where field lives in the editor (`e->fif.where`, PATH_MAX bytes: the
  engine's root and filter are both bounded by PATH_MAX, fif.h). Default when
  empty: the directory of the active buffer's path, else `getcwd()`. It keeps
  its last value across searches, as Sublime does.
- Tab cycles Find / Where; focusing Where selects it all (typing replaces).
  Ctrl+A, Left/Right/Home/End, BackSpace/Delete, typed text and Ctrl+V (via
  `editor_find_paste`, CR/LF dropped) edit it. Replace-in-files is not part of
  this bead (follow-up).
- The three toggles are findui's (`findui_state.options`) and map 1:1 to
  `fif_options` at Return. The Find row's in-buffer match status is findui's
  current-buffer count; it is not a find-in-files count.
- Where syntax: the first comma-separated token (trimmed) is the folder; the
  rest of the string is passed unchanged as the engine's `include` filter
  (paths/globs and `-excludes`, fif.h grammar). No `~` or project-variable
  expansion (out of scope).
- Return runs the search and closes the panel (focus moves to the results
  tab). It also closes findui (`findui_show(false)`): otherwise the in-buffer
  finder re-searches the results buffer on the single bulk lane after every
  append. Ctrl+F or F3 reopens it with the same query. Results highlight
  ranges are cleared when another tab becomes active (`ranges_owned`). Escape with no search running closes the panel and leaves the mode,
  as the find panel does. Ctrl+F/Ctrl+H leave the mode.
- Ctrl+Shift+F while a search runs cancels it (the partial buffer stays) and
  reopens the panel.

## Results tab

- One tab titled `Find Results` per editor. Each search gets a fresh empty
  buffer: `editor_results_open` swaps it into the existing results tab with
  `tabs_replace` (ID and order kept; the outgoing buffer retires like an
  evicted closed tab) or opens a new tab when none is live. A swap avoids
  clearing text in place, which would need view/scroll repair of a deletion.
- `editor_buffer.read_only` (the first read-only flag) refuses every user
  edit: typing, Enter-as-text, BackSpace/Delete (the caret's delete motion is
  rolled back), line/default edits, undo/redo/soft-undo text replay, cut,
  paste (key, middle-click and native replies through `clip.c`
  `start_replace`), find-panel replace. A refusal is a no-op that bumps
  `stats.rejected_commands`; it never latches an error. Copy and motion work.
- `editor_buffer.derived` keeps the buffer out of everything that survives a
  restart: no journal BASE (`editor_register_journal` is not called), no
  checkpoint record, not in TABS or VIEW metadata, no session leaf, Ctrl+S is
  inert. It does not survive hot exit.
- Text is assembled on the UI thread by `editor_fif_service` (each editor
  turn, after `editor_find_service`), only while the results tab is active and
  no action/view continuation/replace is running. Drained mailbox messages
  only record counts (`editor_fif_receive`, first in `editor_route_work`);
  the text comes from `fif_results` (hit indices) and the stored result paths.
  A turn appends hits until `WORK_DRAIN_BUDGET_NS` (G, work.h) elapses; the
  rest waits for the next turn (hits are stored in the job; nothing is lost).
  Lines are staged in the editor's existing `indent_bytes` staging area
  (EDITOR_STAGE_BYTES) and appended by `editor_results_append`: an ordinary
  `piece_insert` plus the shared `changed()` bookkeeping (line index, scroll,
  layout); no undo record, no journal staging, no new text storage class. The
  read-only refusal applies to user edits only; this owner call is the only
  bypass.
- Format (Sublime's): per file a header `<path>:`; one line per matching
  line, `<line>: <context>` with the line number right-aligned in five columns
  (Sublime's layout, e.g. `    1: Alpha cat`); several matches on one line
  share that line and each is marked; a blank line between files; then a
  blank line and `N matches across M files` (`1 match`, `in 1 file` for one
  file, as Sublime words it), M counting files with matches; then warning
  rows: `Search failed: ...` (root unreadable, invalid pattern),
  `Search truncated: result limits reached`, `Search cancelled`,
  `N unreadable entries`. Sublime's leading `Searching N files for "q"` line
  is omitted: the engine streams and does not know N before it finishes.
  Context lines around hits (Sublime's optional context) are not shown.
- Match ranges are marked with the existing find-match highlight
  (`e->find_ranges`, painted by `paint.c`), refilled each turn for the
  visible window from the per-hit mark table.
- Per-hit tables (line start/end byte, mark byte/length, header byte) hold
  `FIF_DEFAULT_HITS` records (the engine's default; limits are zero), in a
  dedicated arena reserved at editor open.

## Navigation and coordinates

- Enter (no Ctrl/Alt/Super) or a double-click on a results line opens the
  hit; on a header line it opens the file at line 1. The hit under the caret
  is the last match on that line at or before the caret (else its first).
- Opening uses `editor_open_request` with one `ipc_path {path, line, col}`.
  The editor's `ipc_path.col` convention is a **one-based display cell**
  (tab stops at the buffer's indent width, cluster widths from
  `utf8_cluster_step`). fif reports a **one-based byte column**; converting it
  to cells needs the target buffer's tab width, which is not known before the
  file is opened. Instead `e->open_byte_column` puts `position_buffer` into a
  byte-column mode for this call: it steps whole clusters while the next
  cluster ends at or before the target byte, so the caret lands on the hit's
  first byte (or the cluster holding it). In this mode an already open tab is
  always repositioned (also for line 1 column 1), and a column past the
  resident scan bound stops at that bound instead of refusing.
  Lines above UINT32_MAX are clamped to it (ipc_path width).
- F4 / Shift+F4 (`next_result` / `previous_result`, now implemented): in the
  results tab they move from the caret's hit (or the nearest hit after/before
  the caret) and open it; from any other tab they continue from the last
  visited hit. They wrap at both ends. The caret of the results tab does not
  follow F4 (the opened file becomes active).
- The `KEYS_ARG_UNIMPLEMENTED` flag is cleared for Ctrl+Shift+F, F4 and
  Shift+F4 (keys.c, keys_test); the editor_test unimplemented-shortcut notice
  case drops Ctrl+Shift+F.

## Lifecycle (34l duties)

- `e->fif.current` owns the live job; `e->fif.retiring` holds a replaced one.
  A new search while a job is live cancels its search handle (if still
  running), moves it to `retiring`, and keeps draining; its messages,
  including the companion DONE, are consumed and ignored by generation. The
  retiring job is disposed by the service once `fif_job_finished` and
  `fif_job_dispose` returns OK (dispose itself reports BUSY while mail is
  pending). If a second replacement arrives while a retiring job is still
  alive, the new search is refused (counted), not queued: two slots bound it.
- A finished job stays current after DONE: navigation reads its paths and
  hits until the next search replaces it.
- `fif_job_init` runs on Return, never per keystroke. Submission, draining,
  accessors and assembly do not allocate (test (f) and the release
  counting-allocator suite).
- Shutdown (`editor_find_close` -> `editor_fif_close`) cancels both slots and
  drains the still-live pool until each job disposes, reusing findpanel's
  close loop (1 ms poll on the pool eventfd).
- Escape while a search runs cancels it first (even with the panel open);
  the companion DONE then reports `cancelled`, the stored hits are assembled
  and the summary gets `Search cancelled`.

## Limits

fif defaults (`fif_limits` zero): no new caps. When DONE.truncated is set the
summary says so. Five-column line numbers and twenty decimal digits are (P):
Sublime's format and the width of UINT64_MAX.

## Out of scope (follow-ups, reported)

Replace in files; a project/folder sidebar; the command palette; Sublime
project-variable and `~` expansion in Where; Sublime's context lines and
"Searching N files" header; selecting the whole match (not just placing the
caret) when a hit opens; showing a fif job-init failure (e.g. a filter
syntax error) in the results tab instead of only counting the refusal.
