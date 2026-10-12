# edit-9u3: save finish after the saving tab is evicted

Source: docs/reviews/editor-2.md finding 12 (src/editor/session.c:157, :302, :364).

## Choice

Retain the saving buffer's session leaf after the buffer is evicted, until
`s->saving` clears (save SAVED or failed without a retained token). Do not
make finish tolerate a forgotten leaf.

`editor_session_metadata()` used to forget every retired buffer's leaf. Now it
skips `e->session->saving`, and if that leaf is still marked live (no metadata
pass ran between close and eviction), it republishes the leaf as closed
(`live=false`). That keeps the TABS ranks and `live_count` equal to the real
tab set. After completion, the next metadata pass forgets the leaf as before.
`capture()` also checks for a NULL leaf: a missing leaf now yields a NULL
request snapshot, which `savectl_finish_request()` refuses, instead of a
dereference.

## Why not "finish tolerates a forgotten leaf"

I tried that first (snapshot from the retained tree, NULL-tolerant `prepare()`
walk). The crash went away, but finish then failed with JOURNAL_INVALID and
the save got stuck needing a finish. `journal_save_finish()`
(src/journal/journal.c, `if(!found) return JOURNAL_INVALID`) requires the
finish checkpoint to contain a BASE record for the saving buffer id equal to
the saved identity. journal.h states the same contract: "complete
checkpoints must retain every in-flight token's BASE and SAVE metadata". A
finish checkpoint without the evicted buffer can never complete the
transaction, so the save cannot become clean. Retaining the leaf is the only
option of the two that finishes.

## Saved bytes

`needs_finish` is set only after `file_save_write()` has replaced the target
from the snapshot captured at Ctrl+S, so the bytes are already on disk when
finish starts. The retained leaf holds the buffer's last published content. A
closed buffer accepts no edits, so that content is its current text.
`encode_leaf()` gives it the replacement BASE (same save path), so recovery
from the finish checkpoint reproduces the saved file.

## Cost accepted

Between finish and the next full checkpoint, the journal lists the evicted
buffer as a closed buffer. A crash in that window brings it back as a
reopenable closed tab after recovery (recovery.c installs non-live journalled
buffers as closed tabs). No data is lost and nothing wrong is shown.
