# Config parsing and reload

The requested config path is `$XDG_CONFIG_HOME/sublimite/config.toml`.
An unset or relative XDG root falls back to `$HOME/.config/sublimite`.
The runtime creates the parent directory on its worker so an initially absent
file can still be watched. Removing the file restores defaults. Invalid input
preserves the last complete settings generation and exposes its error offset.

The parser is a bounded, allocation-free TOML subset visitor. It supports
tables, dotted bare keys, quoted keys within the bare-key namespace, basic and
literal single-line strings, decimal signed integers, finite decimal floats,
booleans, and arrays of strings with multiline whitespace and comments.
Unicode escapes and input UTF-8 are checked; embedded NUL, invalid scalars,
duplicate definitions, overflow, nested arrays, inline tables, dates, base
prefixed integers, multiline strings and nonfinite floats are rejected.
Unknown keys are ignored only after syntactic validation. The settings wrapper
builds a fresh default snapshot and commits it only after a complete valid
parse; removing an override therefore restores its default.

All bounds have structural derivations in `config.h`: the action enum plus
the scalar settings determine entry/binding capacity; the longest action path
determines decoded key capacity; Linux PATH_MAX determines decoded value and
file path capacity. The source bound multiplies those schema capacities by
the longest Unicode escape expansion. It is an explicit subset limit, including
comments and whitespace, rather than a performance promise. Tab width is
bounded by the existing indentation consumer's `INDENT_MAX_WIDTH`.

Keymap values are standalone strokes such as `Ctrl+Y`; arrays provide
alternative strokes for an action. Modifier/key canonicalization is shared
with the defaults, including locks, AltGr and shifted punctuation. An override
replaces the action's defaults, and a stroke collision is a config error.
Action arguments come from an existing standalone default, preferring a
working binding over an unimplemented platform-key alias. Multi-stroke custom
prefix sequences and new parameterized action arguments are outside this
subset. Existing default prefix sequences keep their existing behavior.

The file module's existing process inotify service gains caller-owned path
members. Files and config paths share its descriptor, parent watches and
existing registry reservation. Dispatch coalesces path notices without
allocating. A path member does not open/map untrusted config text through the
editor's large-file acquisition pipeline. Closing either kind of member
preserves its peers' parent watches. Every registry operation retains the file
service's single UI-owner contract.

A config runtime has an independent worker pool so a blocked editor bulk job
does not serialize its reads. Reads use a preallocated base arena, refuse
nonregular/symlink targets, bound file size, and validate both descriptor and
pathname identity. The worker publishes an immutable staging result through
`src/work`; only mailbox receipt changes the UI snapshot. Another reload cannot
reuse staging until receipt and physical lease completion. Cancellation and
shutdown join the worker before releasing storage. There is no debounce timer
or added frame delay.

The editor polls the shared descriptor and config eventfd at its existing I/O
boundary. Once a valid snapshot is delivered it adopts the snapshot before
the next layout/submit, waiting only for an already active view mutation or
undo replay to finish safely. Theme, keymap, tabs and wrap changes go into the
current frame generation (or the immediate successor if idle). The headless
integration test observes publication at the I/O boundary and checks the
first containing submit against that generation. This is the bead's **one
frame generation (G)**: an already open frame has delta zero; an idle frame has
delta one, from `render_frame_begin`'s successor rule. It is not a claim that
arbitrary disk I/O or OS scheduling completes within a refresh interval.

`font.size` stays in typographic points and feeds edit-m3g's
`editor_config.font_points`; the display-derived `font_dpi` converts it to a
true em. Native startup precedence is `EDIT_FONT_SIZE_PT`, then the config
file's `font.size`, then `EDITOR_FONT_DEFAULT_PT` (10 pt). An explicit nonzero
`editor_config.font_points` is the same override seam used by native main.
The environment is read at startup, and its override remains authoritative
across file reloads and deletion. Invalid environment values retain main's
startup argument error behavior.

Initial config I/O and parsing stay on the config worker; startup waits for
its snapshot before building the first font atlas. Invalid or absent config
retains defaults. Reloaded sizes update the configured zoom/reset baseline
and request edit-m3g's existing worker-built replacement atlas. Theme, keymap,
tabs and wrap keep their containing-frame application contract; font geometry
is adopted when the atlas worker finishes and the existing frame/mutation
retirement guards permit it. Removing `font.size` or deleting the file restores
the environment override or the default size. A theme-only reload preserves
interactive zoom. Legacy explicit-pixel callers (`font_dpi == 0`) keep their
pixel sizing. Sizes outside the prepaid atlas bound keep the current font on
reload; startup uses edit-m3g's existing size validation.

`font_family` remains exposed through `editor_preferences`; edit-m3g uses the
embedded font and does not yet implement family selection.

`docs/design/visual-spec.md` is absent in this worktree. The dark and light
tables therefore have explicitly provisional token names as well as placeholder
RGB values. Existing dark editor colors supply the dark placeholders; light
counterparts supply the other table. Text, gutter, caret, selection, chrome,
minimap, bracket/error decorations and syntax roles share these tables. The
coordinator must reconcile the names against the approved spec when available.

Verification and remaining integration are recorded in the worker report.

Worker parsing polls the work lease while validating and scanning untrusted
bytes, using the existing view byte-work boundary. Cancellation leaves the
settings transaction uncommitted. The public visitor has no worker control;
both paths share the same parser and source limits.
