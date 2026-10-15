# edit-hgd: opt-in trailing whitespace

The coordinator decided that `whitespace.trailing` accepts `none` (default),
`all`, or `selection`. It uses the existing config parser and immutable
preferences snapshot, including dotted-key and table syntax and live reload.
There is no additional launch override. The existing theme colour keys
`error.foreground` and `error.background` remain unchanged.

An invalid spelling or TOML type accepts the field as `none` and sets
`config_settings.trailing_warning`. The parser still succeeds, preserving its
transactional return-code contract for actual parse failures. The runtime
publishes this fallback along with the other valid settings and reports
`CONFIG_ERR_VALUE` through its existing `config_runtime_error` diagnostic.
This also turns an already enabled marker off on an invalid reload. A valid
reload or removal clears the warning.

Paint skips `indent_trailing_ws_ranges` entirely for `none`. Enabled modes use
the existing editor arena reservation. Selection mode intersects each range
with the normalized half-open byte selection; the usual selection background
retains precedence and the marker's underline remains visible inside it.

The trailing run ending at the active caret is suppressed, including an entire
run after the typed word. This applies whether other lines have trailing
whitespace or not. Within a longer run, the byte immediately before the caret
is excluded from decoration. A tab at that position excludes its expanded
cells together. Moving the caret away restores markers in enabled modes; no
typing timer, new allocation, or change to the indent range API is needed.

Regression geometry uses (P) the existing tab-strip slot plus minimap width,
and a chrome row plus the text rows in the literal fixtures. Offsets and
trailing-space counts derive from the literal prefixes with `sizeof` or
`strlen`; zero denotes absence/first index, and one removes a NUL or advances
a byte/cell. The disabled-scan regression supplies an invalid range output
pointer that would make indent return an error if called. Allocation checks
require (P) zero typing allocations, derived from CLAUDE's allocation law.
