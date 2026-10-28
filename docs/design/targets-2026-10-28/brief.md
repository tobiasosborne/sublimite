# Target images for sublimité, design round 3

Use your built-in image generation tool. Write the final PNGs into this directory (the one you were started in) as 01-periodical-dark.png, 02-porcelain-light.png, 03-ink-dark.png, 04-states-dark.png, then write README.md here describing each in five lines (the concrete moves: margins, tones, type size, what changed versus the reference). Do not edit anything outside this directory.

## The standard

The editor is graded by a design editor of the most exacting periodicals, against Apple at its most restrained (never current Apple), Rams and Vignelli. 5/5 = publishable as an exemplar; 3/5 = a merely polished commercial app; 1/5 = a janky Linux app. Today it scores 2.4. Do not reference or imitate any other code editor.

## Content and scope (reference: 03-find-dark.png and 02-tabs-dark.png in this directory)

The real app at 3296x2060, 192 dpi: four tabs (hello.c, util.py, notes.txt, readme.md, readme.md active), the README text with line numbers, a small minimap at the upper right, and the find bar open at the bottom with the query `minimap`, one match on line 3. Keep exactly this scope: no sidebar, toolbar, status bar, icons, logo, AI, new features. Keep the document text literal (Markdown source, monospace, no formatted headings).

What you MAY change, and should: everything about the chrome. Today characters do the work of UI (`Find:` label, `[  ] [aa] [ ]` toggles, `1/1`), everything touches the window edges, the text starts one cell from the frame, the tab strip hugs the first line. Redesign margins, alignment, tab treatment, find bar (placeholder instead of a label; toggles as `Aa`, underlined `ab`, `.*` distinguished by tone; a count like `1 of 1`), caret (a thin bar is allowed), minimap framing.

## Implementation constraints (the image must be buildable as is)

- All text on ONE monospace cell grid, one face, one size (about 16 css px at 2x), one line pitch. No proportional type, no bold except where the reference already has it, no italics.
- Besides text cells, only opaque flat rectangles and 1-device-pixel hairlines (for rules, the caret bar, the minimap frame). No shadows, blur, gradients, transparency, rounded cards, floating overlays, icons.
- Flat, sharp, pixel-aligned. Landscape 16:10, edge to edge, no desktop, no window title bar, no captions or watermark.

## The four images

1. 01-periodical-dark: a Swiss/periodical grid. One strong left edge shared by tab labels, text and the find query; generous outer margins; the line numbers in a quiet column; one accent colour only for the caret and the match.
2. 02-porcelain-light: the light theme at its most restrained: near-white page, the tab strip and find bar as quiet tonal steps of the page, hairlines only where a boundary is needed.
3. 03-ink-dark: type first: the text column as the hero, chrome receding almost to nothing (tabs and find bar in low tones), the most air of the three.
4. 04-states-dark: a single sheet in the 01 language showing interaction states as labelled crops on one canvas (small captions allowed in this image only): tab normal / hover / active; find toggles off / hover / on; a command-palette row normal / hover / selected; the minimap viewport at rest and under the pointer; the caret bar in the text and in the find field.
