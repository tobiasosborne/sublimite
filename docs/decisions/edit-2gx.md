# edit-2gx: acknowledge unsupported panels without changing input state

The baseline regression establishes that Ctrl+F, Ctrl+H, F3, Ctrl+Shift+F, Ctrl+P, and Ctrl+G already consume their bindings, increment the unimplemented-action statistic, preserve the selection, and leave no prefix behind. After releasing the modifiers, typing `alpha` performs the ordinary insertion at the cursor. The observed query insertion comes from the missing panel, rather than corrupt input state.

The existing unimplemented-action branch now requests a frame and remembers its generation for these actions. Composition writes `not implemented` across the bottom display row. The editor currently has tab chrome and document rows but no reserved status row; a temporary bottom-row overlay supplies the requested status line without changing layout or selection. Its colors and ASCII atlas slots reuse the existing source-change prompt palette and glyph mapping.

The notice lasts for (G) one frame generation, directly required by the brief: its saved generation must equal the current render-grid generation. Composition retries in that generation retain it; the next generation restores the document row, including when it would otherwise be a caret-only update. No timer or additional follow-up frame is scheduled. Until another generation occurs, the submitted notice remains visible. The source-change prompt retains priority.

The tests compare exact document bytes, literal-derived cursor positions, the complete saved bottom row (including gutter), and the unimplemented-action count (G) increment of one = one bound shortcut consumed. The original selection-chord tests remain in place. The actual panel remains edit-73j work. See the [worker report](../worker-reports/edit-whl.md).
