# edit-r8e — unshaped emoji artwork and zero-width modifiers

The document layout keeps the complete Unicode grapheme for segmentation,
source byte offsets, cursor/selection boundaries, and cell width. It projects
that grapheme to a separate artwork key before requesting an async glyph.
This is a layout rule, with shared Unicode helpers; the rasteriser and font
discovery policy remain unchanged. No shaper or toolkit is introduced.

## Diagnosis

`layout_decode_cluster` recognises `👩‍💻` as one grapheme. Previously both
unwrapped and wrapped layout passed its complete UTF-8 bytes to
`editor_font_glyph`, which forwards them to `font_async_glyph`. The worker
passes the same key to `font_cache_glyph` / `compose_cluster`.

The cache does **not** look up the cluster as an unknown ligature. It resolves
and composites individual scalar outlines. `font_cluster_ignorable` already
skips U+200D and the variation selectors, before face resolution or rasterising.
Any non-ignored scalar that cannot resolve aborts the composition; the async
owner publishes its reserved replacement image for the entire request.
Thus U+200D is not the missing glyph responsible for the reported box.

On this worktree's machine, `fc-list ':charset=1f469' -f '%{file}\n'` reports
only `NotoColorEmoji.ttf`. The existing discovery policy requests a monochrome
face and checks actual charset coverage; it finds no covering monochrome face
for U+1F469. The reported sequence therefore fails at its first scalar here.
Even if that first scalar were covered, the old composition would also require
every subsequent visible scalar and overlay its ink in the same cells. Layout
must express the accepted first-emoji fallback independently of face coverage.

## Rule

- U+200D, U+FE0E/U+FE0F, and U+1F3FB–U+1F3FF contribute no cells and never
  appear in the document's artwork requests. Standalone sequences of these
  scalars disappear from the cell grid without requesting a glyph.
- Skin-tone scalar width is tailored to zero in the shared width API. The
  generated Unicode tables stay intact. Consequently one-shot and resumable
  cluster width, wrapping, column checkpoints and cursor-column calculation
  use the same policy, rather than a layout-only width override.
- If a grapheme's first nonzero-width scalar is Extended_Pictographic, its
  artwork stops at the first ZWJ. Joined emoji do not get separate cells and
  cannot overlay the first emoji or turn its artwork into a replacement image.
  Ordinary combining marks before that joiner retain their ink.
- In other graphemes, the specified invisible scalars are removed and ordinary
  combining scalars are retained. A non-emoji ZWJ does not truncate a conjunct.
- The original grapheme's width is retained. The cell count is bounded by the
  existing cluster width contract (G): one or two cells for visible clusters,
  derived from the first visible scalar and existing presentation widening.
  VS16 can still widen a narrow pictograph; VS15 keeps the existing width rule.
- A retained scalar with no glyph in an available supported face still renders
  the async owner's missing image. The fallback does not substitute another
  emoji for a missing first emoji or erase genuinely missing text.

`utf8_cluster_artwork` implements the projection. Both document layout paths
call `layout_cluster_glyph`, which reuses the existing saved-cluster buffer
after segmentation. Work and storage inherit `LAYOUT_WIN` (G: the existing
contiguous-cluster admission capacity), without a new buffer, arena growth,
heap allocation, atlas identity scheme, or worker mutation. Oversized clusters
retain the existing approximate-rendering refusal. A deferred glyph lookup
still replays the complete source cluster, not the shortened artwork key.

## Verification boundary

The layout test uses a deterministic covering callback for `👩`, so it proves
the exact `👩‍💻` request rule without depending on installed emoji fonts.
The production editor test verifies first-scalar slot identity for that
sequence and for a covered `😀` ZWJ sequence, all skin tones, both variation
selectors, invisible-only input, preserved document bytes, and a genuine
missing scalar. It covers initial and scalar-by-scalar injected input in both
wrap modes. A saved-cluster/deferred-callback test exercises in-place filtering
across decode slices and replay. Release allocation guards cover typing through
submit; sanitizers cover the font, Unicode, layout and editor paths.

Full shaping, colour/bitmap emoji support and changing font discovery are
outside this decision. `👩` still legitimately renders as missing on a machine
whose only covering face is outside the monochrome runtime's supported faces.
The raw font-cache API retains its scalar-compositing semantics; this decision
applies to document cell assignment. Chrome's direct cluster callbacks have
not been changed in this bead.
