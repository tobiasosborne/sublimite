# edit-3u4: preserve pending opens in complete checkpoints

The complete checkpoint must distinguish an unadopted file from an adopted
empty document. `pending_open` is the existing ownership boundary: the full
file has not been attached to `tree`, and `preview` contains only the displayed
prefix. Neither tree represents replacement contents for that buffer.

Keep the existing BASE validation and BASE, TARGET, VIEW, tab-order and window
encoding. Emit DELETE and INSERT replacement records only after adoption.
An unadopted file therefore replays its validated BASE unchanged; an adopted
document still checkpoints its complete current contents, including deletion
to an empty document. Do not substitute preview text or wait for acquisition.
This changes only explicit checkpoint serialization, outside the typing path.

The regression uses a held bulk worker and the existing raster acquisition
lane to keep a COPY open pending with a null renderer, following the
`editor_large_test` fixture. It checkpoints with that tab inactive and active,
then exits without editor cleanup. Recovery must preserve full bytes, clean
state, tab order and active selection; saving must preserve the source file.
Journal inspection additionally requires BASE, TARGET and VIEW without content
replacement for the pending buffer. An adopted named document deleted to empty
must still recover empty and modified. (P) Fixture bytes derive from
`FILE_PREFIX_MAX` plus the literal tail's length, crossing the preview boundary
without inventing a size or timing budget. (E) Test watchdogs reuse the existing
`EDITOR_IDLE_NS` loop policy.

The regression lives in a separate test source to avoid overlapping the
session and input test edits in sibling worktrees. Session graph publication,
save-controller encoding and recovery are unchanged.
