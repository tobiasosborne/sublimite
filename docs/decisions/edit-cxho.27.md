# edit-cxho.27: view_wrap_default decides on the basename

`view_wrap_default` (src/view/view.c) took `strrchr(path, '.')` over the
whole path and wrapped only on `.md`, `.txt`, `.tex`. So an extension-less
name (README, notes, LICENSE) opened unwrapped, contrary to
docs/decisions/edit-6090.md ("wrapped ... is the default for .md, .txt, .tex
and extension-less paths"), and a dot in a directory (`dir.v2/notes`) was
read as the file's extension.

## Choices

- **Basename only.** The extension is looked up after the last `/` or `\`.
  The function is platform-independent C shared with the Windows port, so
  `\` is a separator too. On Linux a `\` inside a file name is legal but rare;
  the cost is that `a\b` is judged by `b`, which only matters when the part
  after the backslash has a different extension class than the whole name.
- **Leading dot is not an extension.** The dot search starts at the
  basename's second byte, so `.bashrc`, `.gitignore`, `.profile` count as
  extension-less and wrap. No existing code or doc gave a reason to treat
  them otherwise; the brief's default was taken. (Sublime's `auto` wrap
  would not wrap a `.bashrc` because it maps to the Bash syntax; if Tobias
  wants dotfiles unwrapped, that is a one-line change here.)
- **Trailing dot** (`notes.`): the extension is `.`, which is not a text
  extension, so it does not wrap. Unchanged from before.
- **Empty basename** (`dir/`): treated like untitled, wraps. Not reachable
  from open (a directory is not opened as a buffer).
- **Mappings unchanged:** `.md`, `.txt`, `.tex` wrap, case-sensitive as
  before; every other extension does not; NULL/empty (untitled) wraps.

## Callers

All callers pass the buffer path straight through, so the fix reaches each:
open (`src/editor/buffers.c` initial state, `view_wrap_file` at activation),
preferences reload (`src/editor/preferences.c`, two sites, only when the
user has not set `wrap`). Session restore applies the journaled per-buffer
wrap flag (`src/editor/restore.c`, `JOURNAL_VIEW_WRAP`), so a restored
session keeps the mode it was saved with; only fresh opens see the new
default. Save As does not re-evaluate wrap (it keeps the buffer's current
mode), which is unchanged. `wrap_mode` (+1/-1) still overrides the
default.

The edit-cxho.25 typing-jump test in tests/editor_test.c uses an
extension-less temp file as one of its six modes; that file now opens
wrapped (comment updated). Unwrapped coverage remains through `.c` and the
`wrap_mode -1` row. tests/editor_p4_test.c `ipc_positions` checks horizontal
scroll on an extension-less `mkstemp` file; it now sets `wrap_mode = -1`
so its setup is the same as before.
