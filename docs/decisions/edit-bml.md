# edit-bml: paste preserves the destination line ending

Document paste uses `file_prefix_info_of(...)->dominant` for file-backed
buffers. In-memory buffers have no file prefix metadata, so buffer creation
scans their initial bytes up to the existing `FILE_PREFIX_MAX` bound with
`file_prefix_scan` and caches the dominant ending. This preserves the detected
style even when a cut empties the buffer. Ties and buffers without separators
default to LF. Detection occurs before typing. The style
is captured in the clipboard job before worker submission; the worker does
not inspect the live editor or piece tree.

Normalization removes CR only when followed by LF, then emits each LF using
the captured destination style. Lone CR, UTF-8 and invalid bytes are preserved.
Looking ahead in the complete received input makes CRLF pairs that straddle
worker slices behave like pairs inside a slice. Find-field paste retains its
existing separate handling.

(P) An LF byte can expand into the CR and LF bytes of CRLF. Receive staging
therefore reserves `len * (sizeof "\r\n" - sizeof "")` bytes for CRLF output,
and `len` for LF output. The arena reservation derives from that same maximum
expansion applied to the existing protocol admission bound, plus the existing
owned transport reservation and recycler/header allowance. No new admission
budget, scheduling duration or work-slice bound is introduced.

Regression byte lengths and caret offsets derive from literal strings or the
existing view scan bound (P). The save wait reuses the test helper's existing
scheduling bound (E); it is not a new performance target. Release allocation
guards cover clipboard normalization and mutation.
