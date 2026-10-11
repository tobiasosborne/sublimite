# The state of design, October 2026, for a fast editor

Research note for Tobias, 2026-10-11 (Opus). Scope: what makes software look and feel modern and fast in 2025–2026, and what that means for sublimité. Bracketed numbers point to the References (§8). Evidence tags follow CLAUDE.md law 4: (M) measured, (P) follows from definitions or arithmetic, (E) my estimate, and a bare [n] means the number comes from source n. Where sources disagree, the text says so.

---

## 1. The one-paragraph answer

In order of effect: **(1) Text set at the right physical size and rendered crisply.** A good typeface at the wrong size or with blurry edges looks cheap, whatever else you do. **(2) Nothing moves unless it has to.** The design engineers people copy today (Emil Kowalski, Rauno Freiberg) say keyboard-driven actions should *never* animate [1][3]. A launcher or palette that opens in zero frames feels faster than one that fades in over 200 ms, even when the machine is equally fast. **(3) A quiet, near-monochrome surface with one accent colour.** Linear's March 2026 refresh is the reference: a warmer, less saturated grey, a dimmer sidebar, softer dividers, fewer and smaller icons, and the rule "structure should be felt, not seen" [10]. **(4) Depth comes from small steps in lightness and hairline borders, not from shadows, glass or gradients.** Apple's Liquid Glass is the counter-example everyone watched. Apple had to add a "Tinted" option within months (iOS 26.1) and a clear-to-tinted slider plus darker edges in iOS 27 [14][15]. **(5) Colour built in a perceptual space (OKLCH)**, so that all your greys and accents step evenly [9][12][13]. **(6) Density with breathing room:** compact tab bars, no chrome that isn't working, and a calm empty state. Everything else (springs, 3D icons, "expressive" shapes) belongs to consumer phones and marketing sites, not to a tool you use hundreds of times a day.

---

## 2. Typography

**Definitions.** *Monospace*: every glyph has the same width, which code needs. *x-height*: how tall the lowercase letters are compared with the capitals. A taller x-height looks bigger at the same point size. *Variable font*: one file that holds a continuous range of weights (and sometimes other axes). *Optical size (opsz)*: the letter shapes change with size (tighter and finer for headlines, looser and sturdier for small text). *Hinting*: nudging outlines onto the pixel grid. *Grayscale vs subpixel antialiasing*: smoothing glyph edges with grey levels, or with the separate red, green and blue subpixels.

### Which monospace faces matter now

| Face | Status 2026 | Licence for a statically linked editor |
|---|---|---|
| **JetBrains Mono** | Safe modern default. It is the font built into Ghostty, as one variable font plus a separate Nerd Font symbols font [31]. Maximised x-height at normal width, "since each symbol occupies more pixels" [42]. | OFL (open licence), embeddable [42] |
| **Geist Mono** (Vercel) | The fashionable choice. Geist Sans/Mono is Vercel's identity, and a pixel-grid sibling, **Geist Pixel**, shipped on 2026-02-06 [7]. Mono was designed first, for code editors and terminals [8]. | OFL [8] |
| **IBM Plex Mono / Lilex** | Zed's default. Zed dropped its "Zed Plex" fork because the name broke the OFL, and now ships **Lilex** (Plex Mono plus ligatures and better box-drawing characters) for code and IBM Plex Sans for the UI [34][49]. | OFL |
| **Commit Mono** | The designer's neutral face, "anonymous and neutral" by design [44]. Its "smart kerning" uses contextual alternates to shift glyphs inside their cells, so it only works if you shape text with OpenType features [44]. | OFL [44] |
| **Monaspace** (GitHub Next) | Five matching families. "Texture healing" swaps in narrower or wider glyph variants (also via contextual alternates) to even out the texture without breaking the grid [45]. Clever, but a bit of a novelty. | OFL |
| **Iosevka** | The power-user favourite. Very narrow by default (one user found 13 pt matched Input Mono at 12 pt [46]). You build your own variant from a config file, and each build job needs more than 1 GB of RAM [46]. | OFL |
| **Berkeley Mono** (U.S. Graphics) | The taste signal of 2023–24. v2 (120 styles) is current [43]. The vendor's terms reportedly do not allow IDEs, terminals or text editors without contacting them first [43]. Treat it as **not shippable** for sublimité unless you negotiate a licence. | Commercial, restricted [43] |
| **SF Mono** | Looks great on a Mac, but it is licensed for Apple platforms only. The Swift community recommends IBM Plex Mono or similar off Apple platforms [48]. **Not an option** on Linux. | Apple-only [48] |

For UI text the default is still **Inter**. Inter 4.0 added an optical-size axis, with display cuts exposed as "Inter Display" [47], and Linear uses Inter Display for headings and Inter for everything else [9]. Be honest about this, though: "Inter everywhere" is now one of the tell-tale signs of generic AI-generated interfaces, together with indigo-to-purple gradients [68]. A dedicated sans next to a mono (Geist Sans with Geist Mono, Plex Sans with Plex Mono) looks more deliberate.

### Sizes and line heights editors actually ship

- **VS Code**: 14 px on Linux and Windows, 12 px on macOS. Default line height = 1.35 × size on Linux and Windows, 1.5 on macOS. Linux font stack: "Droid Sans Mono", monospace [37].
- **Zed**: buffer 15 px, UI 16 px. Line height "comfortable" = **1.618**, "standard" = 1.3, and comfortable is the default [32].
- **Ghostty**: 12 pt on Linux, 13 pt on macOS ("purely subjective") [30].
- **Sublime Text**: `font_size` 10 in the base settings, with per-platform overrides. The Linux default face has been reported as Monospace 10 [40] (forum source, not official docs).

So the range of default line heights is **1.3 to 1.62**, and the sources disagree: Zed bets on loose spacing, VS Code on tight. For mixed prose and code, roughly 1.45–1.5 is the middle (E).

**Physical size at 243 DPI (P).** One device pixel is 25.4/243 = 0.105 mm. At 2× scale, Zed's 15 px becomes a 30 device-px em, which is 3.14 mm or about 8.9 pt on the glass. VS Code's 14 px becomes 28 px, or 2.93 mm.

### Rendering on this OLED

- **Use grayscale antialiasing, not subpixel.** Subpixel (ClearType-style) rendering assumes an RGB stripe, but OLED panels use other subpixel layouts and show colour fringes, especially for white text on dark backgrounds [50][51]. RTINGS notes that higher pixel density makes fringing less visible [50]. At 243 DPI it isn't needed anyway. Apple dropped subpixel AA in macOS Mojave on the same reasoning (cost in glyph caches, compositing, and needing to know the pixel geometry) [52].
- **Blend in linear space with a correction step.** Ghostty's Linux default is `linear-corrected`. Plain linear blending removes dark fringes but "makes dark text look much thinner … and light text much thicker", and the correction brings the weight back [30]. Zed added "perceptual gamma / contrast correction" in September 2025, with a default gamma of 1.8 on Linux [35].
- **Hinting: light or none.** FreeType's "slight" hinting snaps only vertically. Stem darkening (slightly emboldening thin stems) was designed for low-DPI screens [53]. Reports from high-DPI users favour unhinted or light hinting, but the evidence is anecdotal [53].

---

## 3. Surfaces: flat, shadows, glass, borders

**Where 2026 has landed:** hairline borders plus small lightness steps, and shadows only on things that genuinely float (menus, palettes, dialogs).

- **Glass is in retreat.** Liquid Glass (iOS 26, 2025) drew legibility criticism almost at once: text over busy content, low contrast, unclear tap targets [17][18]. Apple added Clear/Tinted in iOS 26.1 [14]. At WWDC 2026 it reduced default transparency, added a clear-to-tinted slider, gave glass elements a darkened edge, and made the material diffuse busy content more strongly [15][16]. The executive behind it, Alan Dye, left for Meta in December 2025. Stephen Lemay, a career interaction designer, replaced him, and Gruber reported Apple staff were "giddy" about it [19][20]. That is one commentator's view, but the direction of the iOS 27 changes matches it.
- **Flat is over in consumer apps.** Airbnb's 2025 relaunch brought in dimensional, animated icons, and Bloomberg called it "a death blow" for flat design [24][73]. This is a consumer, brand-led trend. Tools went the other way: Linear removed coloured icon backgrounds and *softened* dividers [10].
- **The tool rule.** Vercel's Web Interface Guidelines say: layered shadows when you use them (ambient plus direct light), semi-transparent borders for crisp edges, nested corner radii that stay concentric, and borders and shadows tinted toward the surface hue [5]. Geist's colour system reserves three steps for borders (default, hover, active) and three for fills [6].
- **Floating panels failed.** Figma's UI3 beta floated its panels. Testers said they cramped the canvas and "slowed people down", and Figma went back to fixed, resizable panels [25].

### Dark mode on OLED

- **True black vs near-black.** The power argument is weak. Measured savings of true black over dark grey are about 0.3 % [55], because OLED power follows average brightness. The case against pure black is *black smear*: pixels switching on from fully off lag behind during scrolling. The LookUp app swapped pure black for a near-black for this reason [56]. That evidence is from 2019 phones and anecdotal, and panels differ. Material's long-standing recommendation is #121212, not #000 [54]. Google Search went to true #000 [55]. **The sources disagree.** A near-black around L* 15–17 % looks black on this panel and sidesteps the issue (E).
- **Contrast.** Material quotes 15.8:1 and 4.5:1 targets for dark surfaces, and text at 87 % / 60 % / 38 % opacity for primary / secondary / disabled [54]. APCA, the newer perceptual contrast model, recommends Lc 90 as preferred for body text and Lc 75 as a floor [58], and Vercel says to prefer APCA over WCAG 2 [5]. Editors actually ship *less* contrast than 21:1: Zed One Dark's editor text on its background is **6.6:1** and its line numbers are 2.0:1 (P, computed from [33]). Pure white on pure black (21:1) makes letters appear to glow ("halation"), which is widely claimed to bother people with astigmatism. That claim traces back to weak sources [57], so treat it as plausible but unproven.
- **Light mode reads better for most people.** NN/g's review: with normal vision, light mode wins on acuity and proofreading, and the advantage grows as text gets smaller. Dark mode helps some low-vision users [57]. Ship both and follow the system setting. Zed defaults to `"mode": "system"` with One Light/One Dark [32].
- **OLED burn-in.** Long-term monitor tests found the main wear where static bright and dark areas meet (taskbar edges, the line between side-by-side windows) [61]. Static chrome should be dim and low-contrast against the editor. A bright minimap column is a burn-in edge (E).

---

## 4. Colour

**Definitions.** *OKLCH*: a colour space with three numbers, Lightness, Chroma (how colourful) and Hue. Equal changes in L look like equal changes in brightness, which is not true of HSL [13]. *Scale*: a numbered ladder of one hue, from background to text.

- **OKLCH is now the default.** Tailwind v4 (January 2025) moved its whole palette from rgb to oklch to reach P3-gamut colours while keeping v3's balance [12]. Evil Martians' argument: HSL lightness changes with hue, so contrast breaks when you swap hues, while OKLCH keeps it stable and lets you generate palettes from a few inputs [13]. Linear rebuilt its theme generator on LCH. It needs just **three inputs (base, accent, contrast)** instead of 98 variables, and it handles surface elevation (background, panel, dialog) well [9].
- **Role-based scales.** Radix: steps 1–2 app backgrounds, 3–5 component fills (rest, hover, pressed), 11–12 text (low/high contrast) [11]. Geist: 100–300 fills, 400–600 borders, 700–800 strong fills, 900–1000 text [6]. For an editor this means about a dozen neutral greys plus one accent, each with a named role.
- **Accent discipline.** One accent, used for caret, selection, focus and current match. Raycast's stated principles are "fast, simple, and delightful" [62], and third-party analyses describe a near-black UI with a single red accent (`#FF6363`) [62]. Linear's 2026 refresh aimed for "a warmer gray that still feels crisp, but less saturated" and warned that "too warm … risks looking muddy" [10]. Avoid indigo/purple as the accent: it is now the AI-slop signature, and Tailwind's author reportedly apologised for the indigo default [68].
- **Semantic colour in editors (Zed One Dark as the current reference) [33].** Selection = the player's caret colour at **24 % alpha** (`#74ade83d`). Caret = accent blue `#74ade8`. Search match = blue at 40 % alpha. Active-line background = panel colour at 75 %. Line numbers are dim, and the *active* line number is bright. Errors `#d07277` and warnings `#dec184` are muted, not pure red or yellow.
- **Default syntax themes.** Zed: One Dark / One Light [32]. Sublime 4 is widely reported to default to Mariana with the Adaptive theme, though its own docs still use Monokai as the example [39]. Ghostty: background `#282C34` (One Dark's), white foreground [30]. "One" (from Atom) is still the most common default family. What is current is *restraint*: fewer hues, lower chroma, comments in a neutral grey rather than italic green.

---

## 5. Motion and feel

What makes software *feel* fast beyond frame time:

1. **Zero animation on anything the keyboard does.** Emil Kowalski: keyboard actions repeated hundreds of times a day "you should never animate them". Raycast has no opening animation, and "if it animated every time I opened it, it would be very annoying" [1]. Rauno: frequent, low-novelty actions (context menus, the macOS app switcher) appear without motion, and removing motion from a bookmarking UI made it feel faster [3].
2. **When something does animate, keep it short and fast at the start.** Under 300 ms. A 180 ms dropdown "feels more responsive than a 400 ms one" [1]. Use ease-out (fast start, slow finish) [2] and never ease-in. Only animate transform and opacity [2][5].
3. **Interruptible.** New input cancels or redirects motion at once and never queues behind it [2][3][5].
4. **Respond at the moment of input.** Apply the change immediately and animate only afterwards [3]. Vercel: update the UI optimistically and reconcile later [5].
5. **Springs: critically damped or none.** Material 3 Expressive (Android 16, September 2025) rebuilt Android's motion on springs [21][22]. Even its own developer guidance says to keep bouncy motion off high-frequency flows [22]. Material 3 Expressive is consumer phone design. 56 % of readers in one poll liked it, so it is popular [23], but it does not fit a tool.
6. **The caret.** VS Code ships `cursorSmoothCaretAnimation: off` and `smoothScrolling: false` by default [38]. JetBrains added a smooth caret in February 2026 but admits "excessive caret animations may give the impression of a delay when you're typing", so it offers a "Snappy" mode and a way back [41]. A gliding caret contradicts "outperform Sublime on latency".
7. **Latency thresholds.** About 100 ms is the classic "instant" limit for actions. Ink & Switch's survey puts typing thresholds "likely below ~100 ms … possibly well below", while finger and stylus *drags* need 2–16 ms [59]. The Doherty threshold (400 ms, 1982) is a historical rule of thumb for system response [60]. One 90 Hz frame is 11.1 ms (P). In practice, 0 frames versus 1 frame is visible for caret and scroll, and not for a palette fade that you shouldn't be doing anyway.
8. **Scroll.** Scroll the window under the pointer and don't let scroll leak between targets [3]. The marketing-site fashion of smoothed, hijacked scrolling (Lenis from darkroom.engineering, formerly Studio Freight [69]) is the opposite of what an editor needs.
9. **Instant on.** No splash screen, no fade-in. Restore the last state. Show a loading indicator only after a 150–300 ms delay, and keep it visible 300–500 ms so it doesn't flicker [5].

---

## 6. Chrome and density

**Definitions.** *Chrome*: everything that isn't your content (tabs, bars, panels). *Density*: how much content fits per unit of screen.

- **Tab bars.** Linear 2026: compact tabs that do not span the full width, rounded corners, smaller icons and text [10]. Warp went the other way, to *vertical* tabs, but for agent sessions [27]. For one window with a few tabs, compact horizontal tabs are right.
- **Minimaps.** Zed added one in May 2025 modelled on VS Code, and it is **off by default** (`show: never`, thumb `always`) [32][36]. VS Code's is on and renders characters [38]. Sublime draws a scaled-down text image, and its viewport border is off by default, visible on hover [39]. The current taste: if there is a minimap at all, it is a soft texture of the text, quieter than the text, with a translucent viewport.
- **Command palettes** are standard (Raycast, Linear, Vercel). cmdk, the React palette, lives in Paco Coursey's repository, and Rauno is also credited with it in profile sources [67][4]. Palettes appear instantly [1].
- **Status lines** are thin and grey, or gone. Linear dimmed its sidebar "a few notches" so the work area dominates [10].
- **"Native" as an aesthetic.** Ghostty's pitch is fast, feature-rich *and* platform-native, with no trade-off between them [29]. Ghostty's default window padding is just 2 (points) per side [30], so the content goes nearly to the edge.
- **Empty states.** Calm and typographic: a single line of text, not an illustration. The Bodoni wordmark (`docs/design/title-font/`) belongs exactly here.

### People and studios: who is rising, who is dated

| Rising / current | Why |
|---|---|
| **Linear** | The reference for calm, dense tools: 2024 LCH theme system [9], 2026 warm-grey refresh [10]. |
| **Rauno Freiberg** | Interaction designer working with Vercel; *Devouring Details* [4]; the "invisible details" essay is still the canon [3]. |
| **Emil Kowalski** | Sonner, Vaul, animations.dev. In March 2026 he released his design judgement as AI agent "skills" (MIT licence) [66]. The person to read on motion [1][2]. |
| **Vercel / Geist** | Font, colour system and interface guidelines [5][6][7]. |
| **Zed, Ghostty** | Fast-native credibility, tuned text rendering [29][35]. |
| **Apple under Stephen Lemay** | Walking glass back toward legibility [15][19]. |
| **Cursor (Ryo Lu, Jenny Wen)** | "Designers who ship code" [65]. Influence on how work gets done more than on the look. |
| **Dieter Rams revival** | WDO World Design Medal, September 2025 [72]. "Less but better" is the stated root of the calm-tool look. |

| Fading / dated | Why |
|---|---|
| **Glassmorphism, neumorphism, gradient blobs** | Liquid Glass's retreat [14][15]. Purple gradients read as AI output [68]. |
| **Arc / The Browser Company** | Arc development stopped in May 2025. Atlassian bought the company for $610 M to focus on Dia, an AI browser [26]. Arc's sidebar look is now a period piece. |
| **Warp's "pretty terminal"** | Rebranded as an "Agentic Development Environment" (Warp 2.0, June 2025) [27][28]. |
| **Material 3 Expressive in tools** | Fine on phones [21], wrong for an editor. |
| **Benji Taylor / Family** | Family's fluid motion set the 2022–23 crypto-wallet look. Taylor went to Aave and Base, then became head of design at X in March 2026 [63]. His work is still influential, but no longer on tools. |
| **Jordan Singer** | Now runs Mainframe, an AI-agent company. He matters for AI design tooling, not for visual style [64]. |
| **Studio Freight / basement.studio** | Award-winning *marketing-site* studios (WebGL, smooth scroll) [69][70]. Irrelevant to tool chrome. |
| **Lickability** | A solid NYC iOS agency [71], not a trend-setter. |
| **Mariana Castilho, Airfoil** | I could not verify either as a current design influence. Treat them as unconfirmed. |

---

## 7. Applied to sublimité

### What makes the current screenshot clunky (`build/astra/shot-full.png`)

Nothing is scaled for 243 DPI. The window is 960×600 device px, one ninth of the 2880×1800 screen. The line pitch is 15 px and digits are about 9 px tall, which is 1.6 mm and 0.9 mm on the glass (M, P). That reads like 4-point type, and no palette can rescue it. There is no hierarchy or breathing room either: the tab bar is a 15 px strip whose label touches the top edge, the active tab is a flat blue-grey box, and a missing-glyph "?" sits in the first heading. The loudest object on screen is the minimap: solid slabs of `#8091A8` at 4.8:1 against the `#20242C` background (M), with no viewport indication. So the eye goes to the right-hand edge instead of the text.

### Prioritised choices

1. **Scale everything from the display's DPI, with integer device pixels.** Default em = **30 device px** at 243 DPI (3.14 mm, about 8.9 pt, Zed's 15 px at 2×) (P, [32]). Snap glyph origins and line pitch to whole pixels. The default window should be about 70 % of the screen, or restore the last size.
2. **Line height 1.45 (44 px at a 30 px em)** (E). That sits between VS Code's 1.35 and Zed's 1.618 [32][37], which suits prose plus code. Make it a setting.
3. **Typeface: pick from a rendered shortlist at 30 px on this panel:** JetBrains Mono (safe, broad coverage, Ghostty's default), Geist Mono (most "2026"), IBM Plex Mono/Lilex (Zed), Commit Mono (most neutral, but smart kerning needs `calt` shaping). All are OFL and can be linked statically [8][42][44][49]. My lean is **Geist Mono**, with JetBrains Mono as fallback (E). Exclude Berkeley Mono and SF Mono on licence grounds [43][48]. Whichever you pick, add a fallback chain so "é" and "—" never render as "?".
4. **Rendering: grayscale AA, light or no hinting, linear blending with a weight correction** (Ghostty `linear-corrected`, Zed gamma 1.8) [30][35][50][53].
5. **Dark palette (OKLCH, neutral with a faint cool tint; contrast computed (P)):** background `oklch(16.5% 0.004 265)` = `#0D0E10`. Active line `#121314`. Raised chrome (tab bar) `#151618`. Hairline `#252629` (1.3:1). Text `#DCDEE2` (14.3:1). Muted text `#9C9EA4` (7.2:1). Comments `#7D8086` (4.9:1). Gutter `#56585D` (2.7:1, like Zed's dim gutter [33]) with the active line number `#BCBEC2`. This is near-black, not #000, for the smear reason in §3 [54][56].
6. **Light palette:** background `#FAFAF9`, raised `#F2F2EF`, hairline `#DFDEDB`, text `#1F2227` (15.3:1), muted `#5B5E63` (6.2:1), gutter `#96989D`, active number `#383B40`. Follow the system dark/light setting [32][57].
7. **One accent: blue,** dark `#6DB0F4` / light `#1570D1`. Use it for the caret, focus and current match only. Not purple [68].
8. **Selection = accent at 25–28 % alpha** (dark result `#283B50`, text on it 8.5:1). Zed uses 24 % [33]. Other matches get an outline or a 20 % amber fill (`#E6B55D`), and the current match uses the accent.
9. **Syntax: five low-chroma hues at equal lightness** (dark: strings `#99CE9A`, keywords `#BAA4E2`, numbers `#E9B082`, functions `#95CDE9`, comments neutral grey). Prose files stay monochrome apart from headings and code spans.
10. **Caret: 2 device-px bar in the accent colour, solid while typing.** Restart the blink after each keystroke. No smooth caret motion [38][41].
11. **Tab bar: compact, about 1.6× line height, in the raised grey.** Labels in muted text, with the active tab in full text over the editor background (it "joins" the page). A dot marks unsaved changes. Close buttons only on hover. No full-width tabs [10].
12. **Gutter:** right-aligned numbers, 1.5 cells of space before the text, no separator line. The current line is distinguished by number brightness plus the faint active-line fill.
13. **Minimap: off by default, or a soft text texture.** Like Zed [32], or render glyph strokes at 1–2 px per character in syntax colours at about 35 % opacity, at most about 110 device px wide, with the viewport as a 6 % white overlay and no border. It must never be brighter than the text, and it must never paint a stale overlay.
14. **Motion rules:** zero frames for every keyboard action, palette, tab switch and find [1][3]. Wheel and touchpad scrolling maps directly to pixels; page keys jump. The only permitted animation is ≤ 120 ms ease-out opacity on mouse-opened overlays (E, within [1][2]). The first frame is the background colour at window map, then text on the next frame.
15. **Never:** blur or glass, drop shadows on the editor surface, gradients, bouncing springs, colourful icons, a full-saturation red/green/yellow, pure #FFF on #000, scroll smoothing, a splash screen, an animated caret.

---

## 8. References

1. Emil Kowalski, "You don't need animations." https://emilkowal.ski/ui/you-dont-need-animations
2. Emil Kowalski, "Great animations." https://emilkowal.ski/ui/great-animations
3. Rauno Freiberg, "Invisible Details of Interaction Design." https://rauno.me/craft/interaction-design
4. Rauno Freiberg, home page. https://rauno.me
5. Vercel, Web Interface Guidelines. https://vercel.com/design/guidelines
6. Vercel, Geist colours. https://vercel.com/geist/colors
7. Vercel, "Introducing Geist Pixel" (2026-02-06). https://vercel.com/blog/introducing-geist-pixel
8. vercel/geist-font (OFL). https://github.com/vercel/geist-font
9. Linear, "How we redesigned the Linear UI" (2024-03-28). https://linear.app/now/how-we-redesigned-the-linear-ui
10. Linear, "Behind the latest design refresh" (2026-03-12). https://linear.app/now/behind-the-latest-design-refresh
11. Radix Colors, "Composing a palette." https://www.radix-ui.com/colors/docs/palette-composition/composing-a-palette
12. Tailwind CSS v4.0 (2025-01-22). https://tailwindcss.com/blog/tailwindcss-v4
13. Sitnik & Turner, "OKLCH in CSS," Evil Martians. https://evilmartians.com/chronicles/oklch-in-css-why-quit-rgb-hsl
14. MacRumors, iOS 26.1 transparency option. https://www.macrumors.com/2025/10/20/ios-26-1-transparency-option-liquid-glass/
15. MacRumors, "How Liquid Glass is changing in iOS 27." https://www.macrumors.com/2026/06/10/how-liquid-glass-is-changing-in-ios-27/
16. Wikipedia, Liquid Glass. https://en.wikipedia.org/wiki/Liquid_Glass
17. Infinum, "iOS 26 Liquid Glass: sleek, shiny and questionably accessible." https://infinum.com/blog/apples-ios-26-liquid-glass-sleek-shiny-and-questionably-accessible/
18. OSNews, "Liquid Glass is cracked." https://www.osnews.com/story/143522/liquid-glass-is-cracked-and-usability-suffers-in-ios-26/
19. 9to5Mac, Gruber on Alan Dye's departure. https://9to5mac.com/2025/12/04/gruber-apple-employees-giddy-about-alan-dyes-departure/
20. TechRadar, Apple's UI executive poached by Meta. https://www.techradar.com/pro/apples-ui-executive-poached-by-meta-and-three-other-executives-leave-in-apple-ai-exodus
21. 9to5Google, Android 16 Material 3 Expressive. https://9to5google.com/2025/05/13/android-16-material-3-expressive-redesign/
22. Android Authority, Material 3 Expressive deep dive. https://www.androidauthority.com/google-material-3-expressive-features-changes-availability-supported-devices-3556392/
23. Android Authority, Material 3 Expressive poll. https://www.androidauthority.com/material-3-expressive-poll-results-3558644
24. Bloomberg, "Big Tech Is Dealing Flat Design a Death Blow" (2025-06-13). https://www.bloomberg.com/news/articles/2025-06-13/apple-airbnb-ditch-flat-app-icons-for-new-3d-ui-design
25. Figma, "Our approach to designing UI3." https://www.figma.com/blog/our-approach-to-designing-ui3/
26. AlternativeTo, Atlassian acquires The Browser Company. https://alternativeto.net/news/2025/9/atlassian-acquires-the-browser-company-creator-of-the-arc-and-dia-ai-powered-browsers ; BetaNews, $610 M. https://betanews.com/2025/09/05/atlassian-moves-into-ai-browsers-with-610-million-purchase-of-the-browser-company/
27. SD Times, Warp 2.0. https://sdtimes.com/ai/warp-2-0-evolves-its-terminal-experience-into-an-agentic-development-environment/ ; 2026 vertical tabs: https://www.plushcap.com/companies/warp/blog/summaries/2026/04
28. Warp, "The block model behind Warp's ADE." https://www.warp.dev/blog/block-model-behind-warps-agentic-development-environment
29. Mitchell Hashimoto, "Ghostty is coming." https://mitchellh.com/writing/ghostty-is-coming
30. Ghostty source, `src/config/Config.zig` (font-size, alpha-blending, padding, colours). https://github.com/ghostty-org/ghostty/blob/main/src/config/Config.zig
31. Ghostty discussion #9257 (built-in JetBrains Mono). https://github.com/ghostty-org/ghostty/discussions/9257
32. Zed, `assets/settings/default.json`. https://github.com/zed-industries/zed/blob/main/assets/settings/default.json
33. Zed, One theme `assets/themes/one/one.json`. https://github.com/zed-industries/zed/blob/main/assets/themes/one/one.json
34. Zed commit "Use IBM Plex Sans / Lilex (#36084)." https://git.secluded.site/zed/commit/bd61eb08898e379ae1f093895738d805af726430
35. Zed 0.203.4 release notes. https://zed.dev/releases/stable/0.203.4 ; Linux gamma PR #38862: https://git.secluded.site/zed/commit/e72021a26bf3e7e31d736eaaea89d6914149a9bf
36. Zed 0.187.4 (minimap). https://zed.dev/releases/stable/0.187.4
37. VS Code source, `fontInfo.ts`. https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/config/fontInfo.ts
38. VS Code source, `editorOptions.ts`. https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/config/editorOptions.ts
39. Sublime Text, Color Schemes docs. https://www.sublimetext.com/docs/color_schemes.html
40. Sublime Forum, default font face. https://forum.sublimetext.com/t/what-is-sublime-texts-default-font-face/9202
41. JetBrains, smooth caret animation (2026-02). https://blog.jetbrains.com/platform/2026/02/editor-improvements-smooth-caret-animation-and-new-selection-behavior/
42. JetBrains Mono. https://github.com/JetBrains/JetBrainsMono
43. U.S. Graphics Company (Berkeley Mono v2; licence terms). https://usgraphics.com/ ; https://usgraphics.com/catalog/FX-600
44. Fontsource, Commit Mono. https://fontsource.org/fonts/commit-mono/about ; smart-kerning mechanism: https://github.com/mickeynp/ligature.el/issues/53
45. Monaspace. https://monaspace.githubnext.com/ ; https://github.com/githubnext/monaspace
46. Iosevka custom build. https://raw.githubusercontent.com/be5invis/Iosevka/main/doc/custom-build.md ; width note: https://lambdaland.org/posts/2022-08-01_a_new_font/
47. Wikipedia, Inter. https://en.wikipedia.org/wiki/Inter_(typeface) ; https://github.com/rsms/inter/releases
48. Swift Forums, SF Mono licence. https://forums.swift.org/t/revisiting-liberalization-of-the-san-francisco-mono-license/56233
49. Lilex. https://github.com/mishamyrt/Lilex
50. RTINGS, text clarity. https://www.rtings.com/monitor/tests/picture-quality/text-clarity
51. PC Monitors, QD-OLED and WOLED fringing. https://pcmonitors.info/articles/qd-oled-and-woled-fringing-issues/
52. Michael Tsai, "macOS 10.14 Mojave removes subpixel anti-aliasing." https://mjtsai.com/blog/?p=22090
53. FreeType, text rendering and hinting. https://freetype.org/freetype2/docs/hinting/text-rendering-general.html
54. Material Design 2, dark theme. https://m2.material.io/design/color/dark-theme.html
55. Wikipedia, Dark mode. https://en.wikipedia.org/wiki/Dark_mode ; Android Authority, true black. https://www.androidauthority.com/true-black-dark-mode-1003537 ; Tom's Guide, Google Search true black. https://tomsguide.com/news/finally-google-search-gets-a-true-dark-mode-heres-how-to-activate-it
56. MacStories, "Designing a dark theme for OLED iPhones." https://www.macstories.net/linked/designing-a-dark-theme-for-oled-iphones/
57. Raluca Budiu, NN/g, "Dark Mode vs. Light Mode" (2020). https://www.nngroup.com/articles/dark-mode/ ; astigmatism-claim provenance: https://www.levelaccess.com/blog/accessibility-for-people-with-astigmatism/
58. APCA introduction. https://github.com/Myndex/apca-introduction
59. Ink & Switch, "Slow Software." https://inkandswitch.com/slow-software/
60. Laws of UX, Doherty threshold. https://lawsofux.com/doherty-threshold/
61. TechSpot, OLED burn-in test. https://www.techspot.com/article/3098-oled-burn-in-test/
62. Raycast, "A fresh look and feel." https://www.raycast.com/blog/a-fresh-look-and-feel ; third-party palette analyses: https://app.byq.supply/styles/brand/raycast ; https://design.hagicode.com/designs/raycast/
63. Crypto.jobs, X appoints Benji Taylor head of design. https://crypto.jobs/news/x-appoints-former-aave-cpo-benji-taylor-as-head-of-design-ahead-of-payment-product-launch ; https://www.iq.wiki/wiki/benji-taylor
64. Mercury, founder feature: Jordan Singer. https://mercury.com/blog/jordan-singer
65. a16z podcast, Ryo Lu. https://a16z.com/podcast/ryo-lu-cursor-ai-turns-designers-to-developers/ ; Jenny Wen at Cursor: https://digg.com/ai/5zxybsaf
66. AI Heartland, emilkowalski/skills (2026-03). https://ai-heartland.com/explain/emilkowalski-skills-guide/
67. cmdk (Paco Coursey). https://github.com/pacocoursey/cmdk ; Rauno profile crediting cmdk: https://yespress.io/rauno-freiberg.md
68. "Why your AI keeps building the same purple gradient website." https://prg.sh/ramblings/Why-Your-AI-Keeps-Building-the-Same-Purple-Gradient-Website
69. Lenis README (darkroom.engineering). https://cdn.jsdelivr.net/gh/darkroomengineering/lenis@1.3.26/README.md
70. basement.studio. https://basement.studio/index.md
71. Lickability. https://lickability.com/
72. WDO, Dieter Rams 2025 World Design Medal. https://wdo.org/dieter-rams-named-2025-recipient-of-wdo-world-design-medal/
73. It's Nice That, Airbnb app redesign (2025). https://www.itsnicethat.com/articles/airbnb-app-redesign-140525
