Found four regressions and one remaining eviction-policy gap:

1. **Hidden close prompt can discard text.** [src/editor/input.c:1006](/home/tobias/Projects/sublimite/.wt/edit-2ye3-s10/src/editor/input.c:1006)  
   With the menu visible, show the close prompt, then use the mouse to choose File → Save As. The Save As row covers the close prompt, but `close_prompt_key` still owns input. Typing `d` as part of the filename closes the document. If the closed stack contains only modified tabs, it drops the document outright.  
   **Severity: data loss. Confidence: high.**

2. **Wide-character filenames can leave the keyboard stuck behind an invisible prompt.** [src/editor/paint.c:544](/home/tobias/Projects/sublimite/.wt/edit-2ye3-s10/src/editor/paint.c:544)  
   Modify `中.txt`, leave the find panel closed, and press Ctrl+W. The wide continuation cell has `RENDER_NO_SLOT` with nonzero `glyph_index`, violating the render contract. Rejection handling overwrites the invalid row with its notice before repair; repair then finds nothing to fix and drops the frame. The prompt remains unstamped, so subsequent keys—including Esc—are swallowed.  
   **Severity: hang. Confidence: high.**

3. **Closing a restore placeholder can terminate the editor on capacity refusal.** [src/editor/buffers.c:581](/home/tobias/Projects/sublimite/.wt/edit-2ye3-s10/src/editor/buffers.c:581)  
   Reopen a deferred restore placeholder, let restoration remain deferred by capacity, and fill the closed stack with modified tabs. Ctrl+W on the placeholder bypasses prompting, but its newly set modified flag makes `tabs_close` refuse. `EDITOR_ERR_CAPACITY` propagates through `editor_step` as a fatal error, ending `editor_run`.  
   **Severity: crash (fatal editor exit). Confidence: high.**

4. **The prompt inherits document scrolling and minimap overpainting.** [src/editor/paint.c:526](/home/tobias/Projects/sublimite/.wt/edit-2ye3-s10/src/editor/paint.c:526)  
   With wrapping off and a fractional scroll offset, Ctrl+W paints the prompt inside the active text-scroll region. The renderer shifts/crops its text and samples document overscan beneath it. With a long title, the rightmost choices also fall beneath the opaque minimap region. The source-change prompt explicitly disables those render layers; this overlay does not.  
   **Severity: wrong text. Confidence: high.**

5. **Peer saves still defeat the eviction guarantee.** [src/tabs/tabs.c:148](/home/tobias/Projects/sublimite/.wt/edit-2ye3-s10/src/tabs/tabs.c:148)  
   Close clean tab A, then save different text through a same-target peer B. Save completion marks A’s buffer `saved_lost`, but updates modified flags only for live tabs. Once the closed stack fills, this scan still considers A unmodified and evicts its now unique text. The underlying flag staleness predates this commit; the new eviction policy inherits the gap.  
   **Severity: data loss. Confidence: high.**

`tabs_test` passed. The editor test could not initialize in this environment, so these scenarios are code-traced rather than runtime-reproduced. No added typing-path malloc found. No files changed.