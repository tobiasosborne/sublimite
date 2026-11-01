# edit-177r.30 — clipboard reads, close ownership, notices and smaller fonts

Native clipboard reads use the transport's increasing operation identity, shared
with publications, and the same UI deadline as Round 4 of edit-177r.9. (E) The
envelope is one acquisition tolerance plus one foreign-handler tolerance:
`2 * GetDoubleClickTime() * 1,000,000 ns/ms`. A zero OS interval uses the documented
`SetDoubleClickTime(0)` default of 500 ms, giving `2 * 500 ms = 1 s`. This is the
inherited responsiveness policy, not a measured performance gate. The deadline
starts at admission, so a read behind an expired publication also expires while
the native worker is still blocked. The existing editor wait deadline includes
both operation kinds. Expiry invalidates the read ID and retained terminal event,
leaves the text and selection intact, and shows “clipboard read timed out; text
kept”. Only the matching current read can supply paste data. Native bytes and
transport resources remain worker-owned through detached teardown.

The close prompt already retains `saving`, its tab owner, through Save As.
Windows window-close handling now observes that owner together with the active
Save As prompt. It cancels the window-close boundary and reopens input admission
while the filename still needs an answer. Repeated close does the same. Accepting
the filename follows the existing save-and-close continuation; Escape leaves the
modified tab open and recoverable.

Windows recovery notices may first compose after the accepted input FIFO has
resumed editing. An unshown notice is therefore eligible regardless of its
original mutation counter. Its first eligible status composition records the
current counter; its shown flag is acknowledged only after the backend accepts
the CPU snapshot. A refused/retried composition leaves it unshown. The existing
next-mutation clearing then applies. Save status
and panel chrome retain priority. This change is Windows-guarded in the shared
overlay; Linux notice behavior is unchanged.

## Grid preparation and memory

Choose growth during font preparation before adoption. Startup keeps Round 4's
reservation. When a prepared font needs more columns or rows for the existing
maximum pixel surface, reserve `ceil(max pixels / prepared cell pixels)`, retaining
the existing minimums, dimension cap and overscan row. Wait for the old frame,
highlight snapshots, both font versions and startup probe to relinquish their
storage. Allocate replacement UI arrays, retire/reinitialize the backend through
its existing exclusive lifecycle lane with the enlarged cell capacity, then adopt
the arrays and font. Pixel surface, atlas and minimap capacities do not grow.
Later smaller fonts can grow again; larger fonts reuse the reservation. Ordinary
typing and window resize allocate nothing. The replacement arena is released at
shutdown, and a subsequent growth releases its preceding replacement arena.

The original UI arrays share the editor's startup arena and remain reserved until
shutdown. That cost is included below. Allocating replacement storage before
retiring live resources also preserves the old storage if allocation fails;
backend initialization failure attempts to restore the previous configuration
before returning its error. Native presentation allocation failure retains the
existing CPU-editing/parked-recovery behavior.

For the brief's (P) 5120×1440 display at 96 DPI, embedded 10 pt has em
`round(10*96/72)=13` and cells 8×15. At 9 pt, em is 12 and cells are 7×14.
The column reservation grows from `ceil(5120/8)=640` to `ceil(5120/7)=732`;
the actual visible grid has `floor(5120/7)=731` columns. The inherited 300-row
minimum still wins over `ceil(1440/14)=103`. Including overscan, cells grow from
`640*301=192,640` to `732*301=220,332`, a difference of 27,692.

(P) Current ABI sizes, corroborated by the fixture: hl_span=24 B, editor_hit=24 B,
render_cell=20 B, D3D instance=48 B, D3D strip=16 B, layout_wrap_row=72 B,
indent_range=16 B, render_strip=8 B. Thus the logical UI cell-storage delta is
`27,692*(3*24+24+2*20)=3,766,112 B`; D3D CPU instances/strips add
`27,692*48 + (27,692/2)*16=1,550,752 B`; GPU instances add
`27,692*48=1,329,216 B`. Their sum is `6,646,080 B`, or about 6.34 MiB
(`bytes/1024^2`).

Because the startup arena cannot release individual arrays, the actual added UI
reservation is the whole replacement:

```
220,332 * 136                         = 29,965,152 B  cell arrays
301 * (2*72 + 8 + 4)                  =     46,956 B  wrap and row metadata
300*16 + 150*8                        =      6,000 B  whitespace and strips
5 * (2 + 4096) * 8                    =    163,920 B  grid/glyph row bits
payload                              = 30,182,028 B
14 allocations * 16 B alignment slack =        224 B
arena request                        = 30,182,252 B
```

Here (P) `5=ceil(301/64)` bitmap words and 4096 glyph slots are inherited font
capacity; 14 is the allocation count and 16 B is this ABI's max_align_t alignment.
The fixture measures 30,182,032 B used after actual alignment. Consequently the
first zoom adds requested storage of
`30,182,252+1,550,752+1,329,216=33,062,220 B`, about 31.53 MiB. Allocator page
rounding and native driver granularity are excluded, as in Round 4. On subsequent
growth the transient peak additionally contains the preceding replacement UI
arena until adoption; it is then freed. The original startup arena stays intact.

Reserving for the smallest admissible positive integer cell (1×1 pixels) at
startup would require `5120*(1440+1)=7,377,920` cells. Just the cell-storage delta
versus Round 4 would be `(7,377,920-192,640)*240=1,724,467,200 B`, about
1644.58 MiB, before row-dependent growth. Growth on font preparation avoids that
startup cost without allocating during typing.

The persistent bookkeeping is (P) an edit_arena descriptor (one pointer and two
size_t fields: `3*8=24 B` on this ABI) and the clipboard read's two uint64_t
identity/deadline fields (`2*8=16 B`), excluding containing-struct padding. There
is no new per-keystroke storage.

Linux retains its existing initial-window/minimum grid reservation. For this
example, a 60%-width startup reserves `floor(5120*60/100/8)=384` columns; after
9 pt adoption those columns cover `384*7=2688` pixels. That equivalent Linux
limitation is reported and unchanged. Every modified shared C translation unit
has identical Linux preprocessed tokens to the starting revision.
