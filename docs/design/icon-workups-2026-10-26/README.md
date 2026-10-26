# Icon workups, 2026-10-26

Tobias: "the icon sucks, stupid single lower case s. Please suggest me 5 graphical design ideas, based on improvisation on the readme." The current icon (`tools/sublimite.svg`) is one near-black italic s on a transparent ground: it disappears on a dark panel (row "current" in `contact-sheet.png`).

All five share the editor's surface: a rounded tile in #222226 (Mint-Y-Dark wm_bg, the dark theme since edit-29y2), cream #efeae0 (the title-font paper) and Mint-Y-Dark-Aqua #1f9ede as the single accent. Glyphs are Bodoni Moda Italic (the chosen title face, OFL), drawn as paths so the SVGs need no font. `contact-sheet.png` shows each at 256 px and at 64/32/16 px on a dark and a light panel.

| # | Name | Reading of the README | Small sizes |
|---|---|---|---|
| 01 | Empty quotation „ \| “ | „Man sah einander tief in die Augen und rief: »Quelle sublimité!«": the quote, with nothing in it but a caret. "A tool named after the exclamation, with nothing in it to exclaim about." | the caret carries it; the quote marks become specks at 16 px |
| 02 | Gaze » ´ « | "looked deep into each other's eyes": the guillemets of »Quelle sublimité!« turned into two faces gazing, the é's acute as a raised eyebrow | reads as a shape to 32 px, mush at 16 px |
| 03 | Caret exclamation | the exclamation mark reduced to a text caret over a full stop: it states, it does not exclaim. Also "tiny, very fast": the caret is the whole product | strongest at every size |
| 04 | é | the letter that makes the name French and ironic; Bodoni italic like the wordmark, the accent in the system accent colour | holds to 16 px |
| 05 | Glass | "Editing, tabs, a minimap, nothing else": the window in miniature, one surface with text, a caret and the minimap viewport | literal, the most "app-like"; busy below 32 px |

## Round 2: pictures of things

Tobias on round 1: "e acute... maybe. I was thinking of graphical things, you know pictures of things." `contact-sheet-pictures.png` puts five drawn objects next to 04 for comparison (`tools/gen_pictures.py`, pycairo only; a salon gold #d9b45a is added to the palette).

| # | Object | Reading | Small sizes |
|---|---|---|---|
| 06 | Two profiles (Rubin's vase) | "Man sah einander tief in die Augen": two heads nose to nose; the space between them, with a dot below, is the exclamation mark | faces read to 32 px; at 16 px a blue hourglass |
| 07 | Wanderer above the sea of fog | *sublimité* is the sublime: Friedrich's wanderer on a peak before a pale sun; the fog banks are left-aligned like lines of code | strong silhouette to 32 px |
| 08 | Peacock feather | the Jugendstil eye of the "Zeitalter der Ausrufe" salon (title-font research §1): gaze and ornament in one object | the eye survives to 16 px |
| 09 | Champagne coupe | „Quelle sublimité!" said over a glass; the rising bubbles stack into an inverted exclamation | clearest object shape at 16 px |
| 10 | Lorgnette | the salon looking; one lens sees text lines, the other the caret | reads as glasses to 32 px |

Regenerate: point fontconfig at a directory holding `BodoniModa-Italic[opsz,wght].ttf` (github.com/google/fonts, ofl/bodonimoda) and run `FONTCONFIG_FILE=<conf> python3 tools/gen.py <out>`, then `python3 tools/sheet.py <out> contact-sheet.png [current-256.png]`. Only pycairo and PIL are needed.

Not done until one is picked: the hand-tuned 16/24/32 px variants, a path-only `tools/sublimite.svg` replacement, and the hicolor sizes `make install` writes.
