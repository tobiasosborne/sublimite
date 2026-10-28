# Design rubric — the council's scale (edit-cxho.22)

The rubric the design council (`docs/design/council.md`) uses to grade still screenshots of sublimité. It has ten criteria, each scored from 1 to 5, with the anchors written out below. Source: the ten criteria in `docs/reviews/use-findings-2026-10-27/research.json` (`best_design`), recalibrated on Tobias's instruction of 2026-10-27.

## The standard

> "I am aiming for the absolute pinnacle of design here … it should look like the elegant understated design that Apple occasionally reached (but they suck ass now). It should be graded against the epitome of design championed by the most elegant periodicals … we want to breathe rare air here, that is the standard, and we must suffer to get there." — Tobias, 2026-10-27

The grader takes the role of the design editor of the most exacting periodicals: the kind that publishes the Braun and Vitsœ work of Dieter Rams, Massimo Vignelli's grids, Swiss typography, Apple's most restrained hardware and software, and Muji. The question for each criterion is whether this screen would earn a place in that editor's pages as an example of software made with total care.

- **Scores are absolute.** They are not on a curve, and the work is never compared with other code editors. Other editors are not the bar and are not named here.
- **Improvement earns nothing.** A screen that is better than before still gets the score its own pixels earn.
- **The reference is Apple at its most restrained,** never its decorative phases (glass, gloss, skeuomorphic texture, translucency for its own sake).
- **The test is "less, but better".** In Rams's sense, every pixel has a job. Restraint, exact type and calm proportion score; ornament, novelty and "modern" mannerisms do not.
- **No score is given for features or speed.** Speed is a precondition, not a grace note (below).
- **The bar is the ultimate design, not any picture of it.** Generated target images (`docs/design/targets-*/`, persona workups) are for ideation and calibration only. Tobias, 2026-10-28: "The targets are not the end state … The council should be grading against the platonic ultimate design; the generated targets are there for calibration/ideation." Resemblance to a target earns nothing; a target is graded on this scale like any build, and a build may surpass every target.

### Fixed constraints (not graded, never traded)

**Perf never suffers** (Tobias, 2026-10-27). A design change ships only with a cost line:

- bytes of memory, derived;
- extra dirty pixels per keystroke, derived;
- no allocation on the typing path (CLAUDE.md law 2);
- no animation on keyboard actions.

A change that costs typing latency or frame time is rejected whatever its score. Graders do not reward effects that would need such a cost (blur, shadows, translucency, animation); the council prompt states this.

The architecture is settled (CLAUDE.md law 5): a cell grid of equal-height rows, plus a single pixel region for the minimap and caret details. A proposal may ask for a new primitive, but it must say so and price it.

### Scale

- **5:** publishable. It has the restraint, proportion and exactness of the finest objects of the Rams–Vignelli–Apple lineage. Nothing could be removed and nothing is missing. A design editor would hold it up as the standard.
- **4:** refined. It has one visible compromise that a trained eye names at once.
- **3:** a polished commercial product. It is competent and orderly, but generic: nothing an editor would publish.
- **2:** functional but unconsidered. Defaults show through, and the hierarchy is accidental.
- **1:** a janky Linux toolkit app. Characters do the work of UI, the greys are defaults, and chrome competes with text.

The anchors at 2 and 4 lie between their neighbours. Every number below is (P), computed as WCAG 2.2 contrast ratios or by the arithmetic shown, or it points to `docs/design/workups-2026-10-21/README.md`, which derives the ratios of the current tokens.

## Criteria

### C1 Typography

What is graded: the face, its size, the rendering and the line pitch.

- **1** Glyphs are under 3 mm tall on the glass (P: 0.3 cm × 192 px/in ÷ 2.54 cm/in ≈ 23 px em at the harness's 192 DPI), or they are blurry, have uneven stems, show missing-glyph boxes, or use a face picked by default.
- **3** Legible and even, at a sensible size. The face is fine but anonymous, and the line spacing is merely adequate.
- **5** The type is the design. A face of real quality is set at the size its designer drew it for. Stems are crisp with no fringes, and the line pitch is a whole number of pixels with deliberate leading. Chrome labels and code share one voice, so the page reads like a finely set book.

### C2 Tonal hierarchy

What is graded: how lightness orders the elements of the screen.

- **1** Chrome, the minimap or the gutter is brighter than the text, or the eye lands on something other than the text first.
- **3** Text is at least 4.5:1 against its background (P: WCAG 2.2 AA), and chrome sits below the text.
- **5** There is a strict, even ladder: text, then muted text, gutter, minimap, and hairline last. The steps are perceptually even, as if set in a uniform space such as OKLCH. Comments stay readable at 4.5:1 or more (P: WCAG 2.2 AA). Only the caret and the current match are saturated. The eye rests on the text and goes nowhere else.

### C3 Colour

What is graded: the palette as a whole.

- **1** More than one accent, or pure primaries (red, green, yellow), or pure white on pure black, or a "programmer theme" palette.
- **3** One accent and distinct syntax hues. Tidy, but off the shelf.
- **5** The palette looks chosen by one hand. All the neutrals share a single tint, and there is one accent, used only for the caret, focus and the current match. Syntax colour is a whisper, at most five low-chroma hues of equal lightness. Light and dark are equals: neither is the other inverted.

### C4 Chrome and proportion

What is graded: the frame around the text.

- **1** Heavy borders, a bright tab strip, labels touching the edges, misaligned elements.
- **3** Chrome is flat and dimmer than the page, and the alignment is mostly consistent.
- **5** Structure is felt, not seen. Every edge sits on one grid, and margins follow one proportion system. Separation is done by space, or at most a single hairline. Nothing in the frame lacks a job. The window looks inevitable rather than assembled.

### C5 Tabs

What is graded: the tab strip.

- **1** Separators or markers drawn as characters (`|`, `*`), or no clear active tab.
- **3** The active tab is clear and the labels are readable.
- **5** The strip is quiet and compact. The active tab is continuous with the page and marked by shape or tone, not ornament. Unsaved state is shown by a small, exact sign. Label spacing is optical, not arithmetic. Overflow has a defined, calm behaviour.

### C6 Gutter and status

What is graded: line numbers, and position or status information.

- **1** Line numbers are as bright as the code or collide with it, or the status area shouts, flickers or lacks what the user needs.
- **3** The numbers are dim, and position is available.
- **5** Numbers sit in a deliberate measure, with the current line's number lifted a step. Status shows only what is useful and not default, is readable in one glance, and uses chrome's tonal family. Whenever possible it is absent.

### C7 Minimap

What is graded: the minimap and its viewport.

- **1** The map is the loudest column, a barcode of stripes, or its viewport is a hairline or does not follow the pointer.
- **3** A quiet texture with a visible viewport.
- **5** The map is a faithful miniature that recedes until it is needed, quieter than any text. The viewport is a calm, legible shape. Proportion and margins belong to the same system as the page.

### C8 Panels

What is graded: the find, replace, go-to and palette minibar.

- **1** The panel hides text, shifts the layout, looks like a terminal prompt, or crams its labels.
- **3** Clear field, visible count, readable options.
- **5** The panel is a single composed line: a field, a quiet count and quiet toggles, aligned to the page's grid. The current match is emphasised. It reads as part of the window, not something bolted on. Palette rows set shortcuts as a typographer would set tabular figures.

### C9 States

What is graded: the caret, selection, find matches and the current line.

- **1** States are indistinguishable, or the caret is a crude block, or a selection floods the text.
- **3** All four states are distinct.
- **5** Every state is distinct at a glance and none is loud. The selection is a soft tint of the one accent, the current match differs from the other matches, and the caret is a fine, exact bar. Nothing vibrates against the text.

### C10 Empty and edge states

What is graded: the empty window, large files, and states that change or are left behind.

- **1** A broken layout in an empty tab, large gaps, stale overlays, or debug-looking text.
- **3** A blank page with its chrome intact.
- **5** The empty state is composed: calm, centred on the page's proportions, perhaps a single quiet line. Large-file and transient states keep the same composure.

## What the council returns

The council returns, per criterion:

- a score from 1 to 5;
- one sentence of evidence that points at pixels, never at intent;
- the single change that would most raise the score, with its rough perf cost (0 B / some bytes / needs a new primitive).

It also returns an overall ranking of the three gaps that keep the screen out of the pages.
