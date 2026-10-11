# edit-agh: large-file byte jumps and label publication under edit-3kk scrolling

edit-3kk made scrollctl the owner of unwrapped viewport labels and routed every
cursor follow through resident windows, which `src/scroll/resident.c` acquires
on WORK_BULK. Three consequences broke the large-file contract and are fixed in
`src/editor` only; no deadline or test changed.

1. **Byte jump adopts its own certified start.** `editor_large_goto_byte`
   already proves the jumped line start and its ordinal (exact, or a density
   estimate). It now adopts that pair through `editor_scroll_place`
   (scrollctl.c), as `position_buffer` does for launch coordinates, instead of
   re-deriving the cursor line through a bulk-lane window that an unindexed
   file's index build, warm or find jobs occupy. The pure `scroll_seek_line`
   policy must keep the same top row; an exact EOF clip falls back to the
   resident follow path. The jumped line is at the top, as before edit-3kk.
2. **Labels of a lazy large file publish with the complete index.** While
   `lg.lazy && !lg.lines_exact`, prefix growth alone no longer re-resolves the
   top label. The relabel happens when the count becomes exact, so the frame
   after the last estimated frame is the exact one (edit-9yd).
3. **An approximate label is not clamped as a position.** On an extent change
   a READY approximate state keeps its byte anchor and only takes the new
   extent; resolution relabels it. Before, `scroll_set_extent` clamped an
   overestimated label to the exact `max_top` and seeked to EOF.
