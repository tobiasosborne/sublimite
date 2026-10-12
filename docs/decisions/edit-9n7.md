# edit-9n7: restore the raster fuzz transport

Keep the repair in `fuzz/raster_fuzz.c`. Its ownership transport casts its
owner to an XCB connection; a native request on that pointer reaches libxcb
with an invalid connection layout. Margin painting added by edit-u82 needs
`xcb_change_gc` before the rectangle fill. Rectangle fill and flush already
have transport replacements.

Model the foreground update with a transport-local function matching the XCB
signature. Require a live transport, the foreground mask, and a value pointer.
Record the update and require it before every margin fill, consuming the flag
after that fill. The existing guaranteed publication schedule exercises this
contract even for empty input. This prevents a no-op replacement from silently
discarding the operation and preserves the existing server-ownership assertion
on native painting. The mock uses no allocation and returns the transport's
successful dummy cookie.

The edit-cee worktree was compared through `diff -u` on the fuzz harness and
raster backend, respecting the binding prohibition on git. Its changes update
the deadline fuzz operation and completion timer. This repair touches only the
transport declarations, helper, and macro mapping, so its hunks are separate.
The mock patch passed an exact-context dry run against the neighboring harness.
The combined harness and pending backend also compiled and completed the bounded
fuzz run in scratch copies inside this worktree. The neighboring worktree and
production raster backend remain untouched.

The bounded run repeats the prior edit-cee measured duration of 60 seconds
((M)[AC], BAT0 `Not charging`; supplied worker report), using that duration as
an (E) verification sample rather than inventing a new budget or asserting a
performance gate. Input count and actual duration come from libFuzzer output
with a fresh BAT0 stamp. The reproduction uses empty input (P: no bytes are
needed to exercise the unconditional ownership schedule).
