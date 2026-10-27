# Editor drop opening

`PLAT_EV_DROP` reads native bytes through `plat_drop_data` and parses them in a
UI-owned `edit_arena`, reserved lazily and reset after each dispatch. Its bound
is `IPC_MAX_PATHS * IPC_PATH_CAP` (P: the parser's documented maximum copied
paths including NUL). Editor teardown frees it. Headless injected events borrow
bytes from private editor fields through dispatch; native events always use the
platform accessor. The typing path and its allocator guard are unchanged.

Use `stat` to follow file-manager symlinks, skip directories (including directory
symlinks), and report folders or failed metadata reads through the existing
status notice. Compact accepted paths into the parser's absolute-path
`ipc_request`; its cleared cwd is safely NULL. Ignore empty results to avoid
creating an untitled tab. Supply the local hostname without DNS lookups.

Keep the parser's retained prefix and report overflow, as the detailed brief
permits: 128 paths (P: `IPC_MAX_PATHS`), rather than extending the parser's API
for batching. The overflow fixture has 130 entries (P: the brief's requested
case, `IPC_MAX_PATHS` plus its explicit pair). Send the request to
`editor_open_request`, sharing CLI/second-instance identity matching, retained
views, and last-path activation. If its transaction refuses the request, retry
the retained paths individually so accepted files and later existing tabs still
open/select. Capacity, parse, storage and file failures remain notices; they do
not propagate to the input loop's `fail()`. Combined notices retain each
applicable failure category and clear after a successful drop or document edit.

Close the palette and active menu, release MRU/key prefixes, break the undo
burst, and clear tab/text/minimap capture before dispatch. Preserve a pinned
menu bar. The detailed brief and edit-cxho.2/edit-cxho.3 govern find behavior:
retain panel, query and field/document focus, and run the existing find source
service after opening. There is no platform bring-forward/focus API, so this
bead adds no focus request.

Other numeric literals in the scoped code/tests are structural (P): zero is
success, absence or the first index; unit steps advance an entry or remove a
C terminator. Test lengths derive from literals, and geometry reuses the
existing duplicate-path fixture (E). No new timing or allocation budget.
