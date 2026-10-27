# Drop parser

`dnd_parse_uri_list` fills `ipc_request` and `dnd_counts` using the caller's
`edit_arena`. Paths are copied and borrowed until the arena is reset. IPC's
one-based origin supplies line/column (P: existing IPC contract); other request
fields are cleared because absolute paths need no working directory. An empty
result must be ignored by the editor caller: an empty open request creates an
untitled buffer. No filesystem or platform calls occur here.

Accept the brief's `file:///path`, `file://localhost/path`,
`file://<local-hostname>/path`, and bare `/absolute/path` forms. Scheme and host
comparisons ignore ASCII case; the caller supplies its hostname, with no DNS
lookups or hostname aliases. URI percent escapes decode exactly once; bare
paths preserve percent signs literally. Valid UTF-8 passes through. Reject
foreign authorities, malformed escapes, decoded NUL, invalid UTF-8, raw CR,
and URI query/fragment components rather than opening an ambiguous filename.
Encoded punctuation remains filename data. Directories pass through for the
editor bead to check.

The decoded byte length, including its NUL, must fit `IPC_PATH_CAP`, and at
most `IPC_MAX_PATHS` entries are retained (P: existing editor/IPC limits).
Every rejected or overflow entry increments `skipped`; `overflow` separately
counts otherwise valid entries beyond the request limit. Blank/comment lines
are not entries. Raw NUL invalidates the payload. API errors and arena
exhaustion clear outputs and restore the entry arena mark, so callers cannot
accidentally dispatch a partial request after a storage failure.

Linux's existing source/test/fuzzer globs discover the module. Windows needs
only the module-list addition; its build is not verified on this Linux lane.
