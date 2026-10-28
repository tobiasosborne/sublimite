# Design council: round-3 target images, 2026-10-28

Absolute mode against docs/design/rubric.md. Members: Codex gpt-6.1-sol xhigh, Muse muse-wonder-full-1.0 high, Claude Opus. Images: docs/design/targets-2026-10-28/ (T1 = 01-periodical-dark, T2 = 02-porcelain-light, T3 = 03-ink-dark, S = 04-states-dark); `current` = the installed build's 03-find-dark scene (round 2), downscaled. The targets are generated proposals, graded on the design they show. Advisory (CLAUDE.md law 1).

Note: `current` scores 1.77 here in absolute mode; round 2's 2.43 was an A/B score. The two modes are not comparable.

| design | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8 | C9 | C10 | mean |
|---|---|---|---|---|---|---|---|---|---|---|---|
| current | 2.33 | 2.67 | 2.33 | 1 | 1.67 | 1.67 | 1.67 | 1 | 1 | 2.33 | 1.77 |
| T1 | 3.67 | 3.67 | 4 | 3.33 | 3.67 | 3.33 | 3.33 | 4 | 2.67 | 3 | 3.47 |
| T2 | 3.33 | 3.67 | 3.33 | 3.33 | 3.33 | 3.33 | 2.33 | 3 | 2.67 | 3 | 3.13 |
| T3 | 3.33 | 4 | 4 | 4 | 3.33 | 3.33 | 2.33 | 3 | 3 | 3 | 3.33 |

Rankings: codex: T1 > T2 > T3; muse: T3 > T1 > T2; opus: T1 > T3 > T2. States sheet S on C9: codex 2, muse 2, opus 3.

## codex

None reaches publication standard. Totals are current 21/50, T1 34/50, T2 33/50 and T3 32/50; the target ranking is narrow. T1 leads through query alignment and more explicit interaction structure, T2 offers the strongest light frame, and T3 offers the quietest reading plane but removes too much articulation. The three largest publication gaps, ranked, are: 1) incomplete text-state grammar and ambiguous focus; 2) inconsistent geometric relationships between page, tabs and find row; 3) minimap treatment that has not reconciled quietness with a faithful, legible viewport. C10 scores are explicitly inferred, and C9 scores reflect the states demonstrated rather than assuming missing treatments exist. No deduction uses apparent physical font size, generated glyph irregularities or skipped line numbers. Cost notation: c is cell width, H row height, G gutter width, N cells dirtied by an edit, ΔH added row pitch, and b·ΔA the bytes for added atlas pixels at b bytes per texel. Zero-byte estimates assume replacement of existing tokens or geometry and reuse of existing UI state and caches. All proposals use existing primitives, require no typing-path allocation and no keyboard animation; atlas work must happen beforehand. These are derived bounds, not measured latency results, so changes that enlarge damage still require performance verification before shipping.

**Synthesis**

- [T3] Use the generous reading inset and largely unframed page. *The text gains presence through proportion rather than brighter chrome; carry the same origin into the find field. Buildable with cell-based padding: 0 B, 0 additional typing-dirty pixels.*
- [T1] Keep the open text rhythm and common monospace voice across document and chrome. *One typographic voice makes the controls belong to the page. Buildable by choosing an integer row pitch; retaining the atlas costs 0 B, while increasing pitch adds N·c·ΔH damaged pixels per keystroke.*
- [T2] Develop the light palette independently around a warm page. *Its light canvas has its own balance rather than resembling an inverted dark screen. Replace existing theme tokens: 0 B and 0 additional typing-dirty pixels.*
- [T1] Use amber as the dark palette's single focus accent. *The match is immediately identifiable without colouring the rest of the document; use the same accent for the focused caret. Existing token replacement costs 0 B and 0 additional typing-dirty pixels.*
- [S] Combine a short active-tab underline with a localized hover rectangle. *These give persistent activity and temporary pointer feedback separate forms without boxing every tab. Existing hairlines and cell rectangles suffice: 0 B and 0 additional ordinary typing-dirty pixels.*
- [T1] Align the find query with the document text and keep the count at the far end. *The field becomes a continuation of the page's measure rather than a separate toolbar. Coordinate changes cost 0 B and add no typing-dirty pixels.*
- [T3] Keep the minimap small and remove its permanent enclosure. *The miniature becomes subordinate texture instead of a competing panel; preserve sufficient resolution for its viewport. Reuse the existing map region: 0 B and no increase in typing-dirty area.*
- [S] Reveal a low-tone viewport shape during minimap interaction. *The pointer example makes the map operable without a permanently bright frame; derive its extent from the actual visible fraction. Existing flat rectangles suffice: 0 B, with additional viewport-change damage bounded by the map area.*
- [S] Use fine bar carets and a flat current-match fill. *These are the sheet's strongest text-state treatments; show only the focused caret and add distinct selection and ordinary-match treatments. Existing pixel and fill primitives suffice: 0 B, caret movement damage at most 2H pixels.*
- [S] Confine command-row feedback to the row's own rectangle. *Localized flat fills make palette navigation readable without adding containers; selected rows still need a clearer distinction from hover. Reusing row state costs 0 B and adds no ordinary document-typing damage.*

**Avoid**

- T1's bright nested minimap outlines: they promote a secondary instrument into the text hierarchy.
- A shallow viewport marker when the entire document is visible: the miniature must represent actual document geometry.
- T3's removal of viewport articulation: quietness must retain a legible interaction shape.
- T2 and T3's separate query origin: it weakens the connection between the field and the reading measure.
- Mechanically equal tab slots: label lengths need balanced cell padding and a defined overflow treatment.
- S's nearly identical hover and enabled toggle fills: temporary feedback and persistent state need different forms.
- S's simultaneous text and find carets if shown in one live window: keyboard focus must be singular.
- Reusing the current-match fill for selection or ordinary matches: those states must remain distinguishable.
- Current's edge-touching labels, bracketed controls and block caret.

**States sheet (C9 2)**: S demonstrates an exact bar caret and a restrained amber current match, but it does not demonstrate selection, ordinary matches or the current line. Its text and find carets appear simultaneously, leaving keyboard focus ambiguous if read as one live state. Tab, toggle and command-row examples do not establish the four text states required by C9. Highest-impact change: complete an explicit four-state text grammar with only the focused field showing a caret. Reusing existing state and fixed colour tokens adds 0 B; recolouring existing selection and match bounds adds 0 typing-dirty pixels, while lifting the current gutter number adds at most 2GH pixels on line transitions.

## muse

Ranking by summed scores: current 14/50; T1 34/50; T2 28/50; T3 34/50. T3 wins the tie because the rubric privileges tonal restraint, felt-not-seen chrome and calm states; its faults are omissions correctable with one hairline and a stronger viewport, whereas T1 adds a bordered object and saturated block. The three decisive gaps for every design are: an anonymous monospace face rather than typography as design; missing current-line lift and incompletely separated selection/other-match/current-match states; and a minimap not yet simultaneously faithful, legible and subordinate. No target is publishable yet.

**Synthesis**

- [T3] Use T3's borderless, receding minimap texture instead of a framed minimap object. *Removes the loudest unnecessary rectangle and lets structure be felt through tone alone; enlarge/strengthen only the viewport enough to remain legible.*
- [S] Borrow S's pointer-revealed calm viewport block, while retaining a faint rest-state viewport. *The map stays subordinate during typing and becomes legible precisely when the hand asks for it, without losing place at rest.*
- [T1] Take T1's brighter active tab plus exact underline and optical label spacing. *It fixes T3's glanceability problem with one hairline and one tonal step rather than a tab-shaped container.*
- [T3] Use T3's space-first chrome: no filled tab/find bands and only the necessary bottom hairline. *This gives the window the inevitability of one surface instead of stacked strips.*
- [T1] Adopt T1's single-accent discipline for caret, focus and current match. *One accent family makes state immediately intelligible and prevents white, blue and sage signals from competing.*
- [T3] Execute that accent in T3's soft low-chroma tint for matches, reserving full strength for the thin caret. *A tint distinguishes current match and selection without T1's amber flood or T2's default-blue ambiguity; this replaces T1's hue rather than adding another accent.*
- [T1] Keep T1's composed find line: field aligned to the text column, typographic Aa/ab/.* toggles, underlined on-state and spaced tabular count. *It is the clearest panel and reads as part of the page grid rather than a terminal prompt.*
- [T2] Preserve T2's warm paper-and-ink light theme as an independently tuned equal to dark, discarding its blue accent and grey bands. *The off-white neutral family gives light mode its own material character instead of a photographic negative; the same soft accent logic supplies state colour.*
- [S] Take S's command-row selected state as a single flat tonal step, with hover expressed by a smaller text-only lift. *Flat tone keeps palette rows calm and legible without colour, border or shadow.*
- [T1] Adopt the targets' inset text column and right-aligned gutter measure from T1, adding a one-step lift to the current line number. *A stable measure makes the page feel typeset; the missing current-line lift is the one refinement needed to move gutter design beyond merely dim.*

**Avoid**

- Do not frame the minimap in a hard rectangle (T1/T2); it becomes a competing object instead of a receding texture.
- Do not mark the current match with a fully saturated opaque block (T1) or generic selection blue (T2); one shouts, the other is indistinguishable from selection.
- Do not rely on filled grey chrome bands (T2) or bracket-character toggles (current) to create structure.
- Do not make quietness mean illegibility (T3 minimap and brightness-only active tab); restraint still needs a legible viewport and unmistakable active state.
- From S, do not use the same filled chip for toggle hover and toggle-on, an underlined supposedly normal tab, or different visual languages for closely related states.

**States sheet (C9 2)**: Tab normal is already underlined, confusing it with active; hover uses a filled rectangle while active switches to an underline. Find-toggle hover and on use an identical fill, so state cannot be read at a glance. Command-row hover changes only text brightness while selected jumps to a full fill, and the minimap has no viewport at rest but obtains a grey block under the pointer. The fine orange caret and amber match are coherent, but the saturated match floods rather than tints. These contradictions cap S at 2.

## opus

Current is a 1–2 screen; it totals 18/50, held down by edge-glued chrome, a terminal-style find prompt, bracket toggles and a block caret. T1 comes closest to the pages (36/50) because of its typeface and its grid-true find line. It is held back by a solid amber match, an underlined active tab and a boxed minimap. T3 (34/50) has the most restrained frame, with tabs by tone alone on the text column, but its type is anonymous and its minimap has no viewport. T2 (33/50) shows the calmest match tint and a working light ladder, but its grey bands and off-grid field make it read as a commercial product. The three gaps that keep any target out of the pages: (1) the match and other states are shouted as fills rather than whispered as tints, and current versus other match is never shown; (2) edges are not all on the text column, since the find field, tabs and minimap each drift in at least one target; (3) the minimap is either boxed or vanishes, and never recedes and returns. The synthesis (T1 type, accent and find line; T3 tabs and gutter; T2 tint and light parity; S's minimap and state rules, corrected) needs nothing beyond cells, flat fills and 1-device-pixel hairlines. Its only non-zero cost is one font file's bytes in the static binary.

**Synthesis**

- [T1] T1's typeface (a humanist mono of the Plex kind) at T1's pitch, about 1.45 em rounded to a whole pixel, one weight for code and chrome alike. *The type is the design. It is the single largest gap between current and every target, and T3's heavier tab label shows what goes wrong without one voice.*
- [T3] T3's tab strip: no strip fill, no rule under the tabs, the active tab marked only by a brighter label tone, and the first label on the text column. *This is the only tab treatment where the active tab is continuous with the page and structure is felt rather than drawn. It removes T1's underline and T2's band.*
- [T1] T1's find line: one inset hairline above, the field starting exactly on the text column, then Aa, ab, .* and the count in tabular figures, set to the right of the page measure. *T1 is the only target whose field sits on the text grid; T2 and T3 both drift left of it.*
- [T2] T2's match treatment: a low-chroma tint under text that keeps its own colour, drawn in T1's amber hue, with the current match one tint step stronger than the other matches. *It keeps the accent singular without the solid amber or sage block that vibrates in T1 and T3, and it gives the missing current-versus-other distinction.*
- [S] S's minimap behaviour: no frame, viewport hidden at rest and shown as a flat fill on pointer, with the map at T1's scale and placed on the page margins. *The map recedes until it is needed, which is the C7 anchor-5 wording, and it fixes T1's box inside a box and T3's viewportless smudge.*
- [T1] One accent (T1's amber) for the caret in both the page and the find field, for focus and for the match, and nothing else. *T3 splits the accent's job with a white caret. One accent with one job set is what C3 and C9 grade.*
- [T3] T3's gutter measure: right-aligned numbers about three cells off the text, with the current line's number lifted one tone step. *It has the clearest measure of the three. The lift is the missing anchor-5 detail in all targets.*
- [T2] T2's light theme as an equal partner: an off-white page with its own ladder and the same amber accent retuned for light, with T2's grey bands removed and only hairlines kept. *C3 asks for light and dark as equals, not inversions. T2 shows a light ladder that already works once the bands are removed.*
- [S] S's toggle and row language, corrected: hover changes label tone only, the on state gets a flat fill, the selected command row gets a two-step fill, and the permanent underline on 'ab' is dropped. *S already defines the states. Separating hover from on and from selected makes every state distinct at a glance without any becoming loud.*

**Avoid**

- T1/S: a saturated solid amber match block with dark inverted text, which vibrates against the text and becomes the loudest thing on screen; T3's mid-tone sage block is the same fault, milder.
- T1/T2: a hairline frame drawn around the whole minimap on top of the viewport box, a box with no job.
- T2: grey fill bands behind the tab strip and the find line, which make structure seen and make the panel look bolted on.
- T1/T2/S: an underline bar marking the active tab (ornament), and in S a dim underline in the normal state that confuses normal with active.
- T2/T3: a find field starting left of the text column (x≈60 and x≈82), off the page grid.
- T3: a white caret instead of the accent, which splits the accent's job.
- T3: a microscopic minimap with no viewport.
- T3: a heavier weight on the active tab label, which breaks the single typographic voice.
- T3: a tightly set count ('1of1'); use tabular figures with word spaces.
- S: toggle hover identical to toggle on, and command-row hover nearly identical to selected.
- S: a permanent underline on 'ab' that reads as a state it never carries.
- T2: a generic system-blue accent; choose a hue instead.
- Any proposal for shadows, blur, translucency or animated state changes: rejected by the perf constraint whatever their score.

**States sheet (C9 3)**: What works: the minimap viewport hidden at rest and appearing as a flat fill on pointer is exactly right. The amber bar caret is fine and exact. Tab hover as a fill is legible. What fails 'distinct at a glance': find toggle hover and on are the same filled block (only the label brightness differs, barely), and command-row hover and selected differ by about one grey step. 'ab' is underlined in every state, so the underline reads as a state it never carries. 'tab / normal' shows a dim underline under util.py, which confuses normal with active. The match is a saturated amber block with dark text that vibrates, and a gap opens before the period. Selection, the current line, and current versus other matches are not on the sheet. Fixes, 0 B each: hover changes tone only and on gets the fill; selected gets the accent hairline or a two-step fill; drop the permanent underlines; add the missing four states.

