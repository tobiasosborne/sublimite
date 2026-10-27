# edit-cxho.12 — incremental find from the panel origin

Opening the document Find/Replace panel saves its selection and viewport.
Refocusing the open panel retains that origin; switching documents captures
the new document's view. Successful query edits and option changes request a
preview. When counting completes, findui resolves the first match whose start
is at or after the saved cursor, wrapping to the first match. Its immutable
compressed plan is searched on the worker, including results beyond the
prefix cache. Neither the input thread nor ordinal retrieval rescans source
bytes, and all request storage remains prepaid.

The preview uses the existing result selection and resident scroll bridge.
A visible result keeps its viewport; a nearby offscreen result moves just
enough to reveal it. A result at least a viewport beyond its nearest edge is
centred using half the live rows (P: viewport geometry, no new distance or
allocation budget). Wrapped positioning reuses visual-row continuations.
Near wrapped results are proved with at most the live viewport's rows (P),
so distance within a long logical line uses visual rows. An EOF-clipped byte
seek keeps the resolver's viewport so centring cannot hide the match below it.
Empty and completed no-match queries restore the saved selection and view;
saved selection endpoints are clipped if a document edit shortened the file.
Escape keeps the current selection. Pending Enter advances after the origin
preview resolves; an uncached ordinal is selected exactly once.

This supersedes edit-g02's Enter-commits-first-match rule in the supplied
research/brief: typing now commits the origin preview, so Enter means next
and Shift+Enter means previous. The existing edit-g02 tests already describe
advancement from findui's internal preview; that advancement is preserved.
Find in Files retains its explicit search action.

The reported mutation/highlight gap is in this path: source service can run
before layout finishes, leaving no later cache refresh before painting.
Decoration preparation now refreshes visible find ranges against the completed
layout. Worker selection replies retain the visible cache instead of appending
duplicate highlights or truncating the cached prefix.
Find in Files results retain their separately owned highlight marks.

Numeric literals in the implementation are structural (P): zero is success,
absence or the first byte/ordinal; unit steps advance a record, remove a C
terminator or convert a row count to its last index; division by two bisects
a search interval or centres the live viewport. The integration fixture uses
the brief's 300 lines and target 250 (P: supplied acceptance inputs); its
origin is the midpoint, and offsets derive from formatted line lengths.
Geometry reuses the existing panel fixture (E). A minimal nonzero copy
threshold forces the mmap path (P). Unit-test offsets/counts derive from their
literal fixtures; zero allocations derives from the binding typing rule (P).
