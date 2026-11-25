# Wave 2 platform contract for X11 and Wayland EGL

**Proposal for advisor signature.** Freeze the platform side of PLAN §§2 and 4.6. L5 sees the platform through `plat.h` and `plat_native.h`. It continues to use the public render service. The signed render, font and viewport contracts govern their own bindings.

## Signature report

| Decision | Proposed rule |
|---|---|
| Q1 | Admit a complete translated key in ingress order, backpressure the producer at the ordinary FIFO limit, and merge prepaid terminal events by the same sequence before L5 dispatch. |
| Q2 | A successful submission or dequeue moves a single buffer obligation, refusal leaves it with the caller, and each accepted peer operation ends by a fixed absolute deadline even if bytes keep trickling. |
| Q3 | Collapse only consecutive, unused whole surface snapshots at the consumer. Input, an acquired candidate or a frame seals the earlier mapping. Scale-only changes receive fresh serials. |
| Q6 | The window connection has one off-UI protocol owner. The raster presentation owner exclusively drives its separate X connection, including flush, completion and teardown. UI submit publishes prepaid memory. |

**Bounds and cost.** The proposed FIFO admits 256 ordinary records, one paused producer record and nine reserved terminal records, or 266 records at peak across events and completions. Transfer admission allows 64 MiB per payload and 128 MiB of charged capacity in aggregate. One transport turn moves at most 256 KiB. Each peer exchange has five seconds of monotonic lifetime, with one second total for the X manager exit handoff. UI work gets at most 64 constant-size records or the remaining share of a 0.5 ms turn, whichever runs out first. The derivations and separate metadata/scratch charges appear below. The production estimate is 4,650 comment-free lines against the harvest’s measured 4,847. No runtime measurement is claimed.

## Invariants

| Structure | Invariant |
|---|---|
| Platform instance | One live window incarnation owns one selected backend and retains its native resources until every lease on that incarnation has physically retired. |
| Event FIFO | The consumer receives the lowest outstanding admitted sequence exactly once, with every sequence and credit owned by one location at a time. |
| Keyboard | One selected keymap, compose state and focus generation determine all fields of an emitted key, including its inline text and ingress time. |
| Surface | One immutable incarnation and serial identify one complete logical-to-buffer transform and its effective font DPI. |
| Selection | Each selection names at most one current local publication whose claim result and subsequent ownership loss are distinguishable by identity. |
| Transfer | An admitted operation owns capacity, a nonreused identity, a terminal credit and an absolute expiry until its result is committed. |
| Transfer buffer | Exactly one public handle owns each move obligation, and all native readers of its sealed bytes retain separately accounted immutable leases. |
| Drop | Every accepted drop binds its own source, negotiation, surface/position and raw URI-list result to the same transaction. |
| Native binding | Only the designated presentation owner can resolve a live binding into native resources and issue presentation requests. |
| Flight | One depth-one flight owns its original frame identity and feedback object until display retirement and independent device retirement or quiescence. |
| App intake | Each admitted user action keeps its sequence, target and input stamp through the ordered app command FIFO until applied or visibly refused. |

## Public headers as text

The declarations below specify C value layouts and calls. The function inventory is `plat_open/window/ready_fd/poll_reply/poll/em_px/buf_new/buf_bytes/buf_release/clip_set/clip_request/cancel/close` in the first header and `plat_native_bind/target_get/arm/ready_fd/poll/cancel/unbind` in the second (each slash starts another name with the indicated prefix). Tagged unions contain only the named arm. Identifiers are nonzero and never reused within their owning lifetime. Exhaustion stops admission and requires quiescence before a new incarnation. All output owners must be empty on entry. A result carries the exact platform reason and, for a native failure, the backend and native error. Setup copies its configuration. UI calls on a live instance are nonblocking. Native handles never appear in `plat.h`.

### src/plat/plat.h

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { PLAT_MOD_SHIFT = 1u << 0, PLAT_MOD_CTRL = 1u << 1,
       PLAT_MOD_ALT = 1u << 2, PLAT_MOD_SUPER = 1u << 3,
       PLAT_MOD_CAPS = 1u << 4, PLAT_MOD_NUM = 1u << 5,
       PLAT_MOD_ALTGR = 1u << 6, PLAT_WHEEL_UNIT = 256, PLAT_UTF8_MAX = 8 };
typedef struct plat plat;
typedef struct plat_transfer_buf plat_transfer_buf;
typedef enum { PLAT_AUTO, PLAT_X11, PLAT_WAYLAND } plat_backend;
typedef enum { PLAT_OK, PLAT_EMPTY, PLAT_BUSY, PLAT_UNSUPPORTED,
               PLAT_NO_DISPLAY, PLAT_NO_EGL, PLAT_NO_FEEDBACK, PLAT_CAPACITY,
               PLAT_STALE, PLAT_CANCELLED, PLAT_TIMEOUT, PLAT_INVALID,
               PLAT_IO, PLAT_STOPPED } plat_code;
typedef struct { plat_code code; plat_backend backend; int native_error; } plat_result;
typedef enum { PLAT_CLIPBOARD, PLAT_PRIMARY } plat_selection;
typedef enum { PLAT_PRESS, PLAT_REPEAT, PLAT_RELEASE } plat_key_action;
typedef enum { PLAT_VISIBLE, PLAT_SUSPENDED } plat_drawable;
typedef struct { uint64_t incarnation, serial; } plat_geometry;
typedef struct {
    plat_geometry id;
    uint32_t logical_w, logical_h, pixel_w, pixel_h, scale120;
    double dpi;
    uint32_t work_w_px, work_h_px, max_w_px, max_h_px;
    uint64_t refresh_ns;             /* zero if unknown */
    plat_drawable drawable;
} plat_surface;
typedef enum { PLAT_TEXT_NONE, PLAT_TEXT_INLINE, PLAT_TEXT_TOO_LONG } plat_text;
typedef struct {
    uint32_t code, keysym; uint16_t mods;
    plat_key_action action; plat_text text; uint8_t len, utf8[PLAT_UTF8_MAX];
} plat_key;
typedef struct {
    int32_t x_px, y_px, dx_q8, dy_q8;
    uint32_t button, buttons; uint16_t mods; bool down, smooth;
} plat_pointer;
typedef enum { PLAT_READ, PLAT_CLAIM, PLAT_OWNER_LOST, PLAT_DROP } plat_op;
typedef struct {
    uint64_t id; plat_op op; plat_selection selection; plat_result result;
    plat_transfer_buf *buf;          /* moved by poll_reply on READ or poll on DROP, on success */
    int32_t drop_x_px, drop_y_px;    /* saved at drop; selection unused for drop */
} plat_transfer;
typedef enum { PLAT_EV_KEY, PLAT_EV_KEYMAP, PLAT_EV_FOCUS, PLAT_EV_BUTTON,
               PLAT_EV_MOTION, PLAT_EV_WHEEL, PLAT_EV_LEAVE, PLAT_EV_SURFACE,
               PLAT_EV_EXPOSE, PLAT_EV_BLINK, PLAT_EV_TRANSFER, PLAT_EV_CLOSE,
               PLAT_EV_LOST } plat_event_kind;
typedef struct {
    uint64_t seq, t0_ns; plat_geometry geometry; plat_event_kind kind;
    union { plat_key key; plat_pointer pointer; plat_surface surface;
            plat_transfer transfer; plat_result loss; bool focused; } u;
} plat_event;
typedef struct {
    plat_backend backend; const char *title; uint32_t initial_w_px, initial_h_px;
} plat_config;
typedef enum { PLAT_MAP, PLAT_ACTIVATE, PLAT_THEME, PLAT_CURSOR,
               PLAT_BLINK_TIMER, PLAT_KEY_REPEAT } plat_window_op;
typedef enum { PLAT_CURSOR_DEFAULT, PLAT_CURSOR_TEXT } plat_cursor;
typedef struct {
    plat_window_op op;
    union { struct { const char *token, *startup_id; } activation;
            bool dark; plat_cursor cursor; uint32_t blink_ms;
            struct { uint32_t delay_ms, rate_hz; } repeat; } u;
} plat_window_cmd;

plat_result plat_open(plat **out, const plat_config *cfg);
plat_result plat_window(plat *p, const plat_window_cmd *cmd);
typedef enum { PLAT_ORDERED_READY, PLAT_REPLY_READY } plat_interest;
int         plat_ready_fd(plat *p, plat_interest interest);
plat_result plat_poll_reply(plat *p, plat_transfer *out);
plat_result plat_poll(plat *p, plat_event *out);
uint32_t    plat_em_px(double points, double dpi, uint32_t max_px);
plat_result plat_buf_new(plat *p, size_t len, plat_transfer_buf **out,
                         uint8_t **writable);
const uint8_t *plat_buf_bytes(const plat_transfer_buf *buf, size_t *len);
void        plat_buf_release(plat_transfer_buf **buf);
plat_result plat_clip_set(plat *p, plat_selection which,
                          plat_transfer_buf **inout, uint64_t *id);
plat_result plat_clip_request(plat *p, plat_selection which, uint64_t *id);
plat_result plat_cancel(plat *p, uint64_t id);
plat_result plat_close(plat **inout);
```

`plat_open` is cold setup and may fail with a partially connected display. Unwind in reverse order. AUTO chooses a nonempty DISPLAY first, then WAYLAND_DISPLAY (honor an explicitly supplied Wayland socket), else NO_DISPLAY. An explicit choice reports that backend’s diagnosis. With a zero requested dimension use the donor’s default for that axis. Publish initial SURFACE before input. Map only after setup has a native window suitable for the chosen renderer. `plat_window` copies the selected small command on admission and returns BUSY without effect at capacity. Activation copies bounded token/startup strings using the landed IPC_ACTIVATION_CAP convention. X sends _NET_ACTIVE_WINDOW with startup time or a server/last input time. Wayland uses xdg-activation and a seat-bound serial if obtaining a token. OK means issued or queued. Theme is an advisory decoration hint. Suppress duplicate cursor changes, disarm blink while unfocused and disable repeat for rate zero. Blink/expose are level-like redraw requests. Expose covers the full affected client.

`plat_ready_fd` is borrowed until final close. Register the requested source in the app root `os_waitset`. The ordered source covers a deliverable head. One reply fd covers all completed READ and CLAIM requests and close progress independently of ordinary intake. `plat_poll_reply` moves the next completed transfer, including its request ID, or returns EMPTY. `plat_poll` delivers DROP and OWNER_LOST as ordered transfer events. The small fixed readiness set belongs to plat. Only plat consumes its notification. `plat_poll` moves one complete event or returns EMPTY immediately. It reads only prepaid records, never a display fd. There is no second application pump or callback reentry. One outer wait with a finite deadline implements the donor's bounded run. The same wait with the earliest service deadline, or OS_DEADLINE_NEVER when idle, implements normal run. The app continues its signed ordinary work drain through `work_pool_wake` and `work_mailbox_drain`; readiness of either source never consumes the other. With an app FIFO at capacity suppress ordinary intake until space returns; continue servicing existing requests and independent completion, shutdown and work sources. At close reject new admissions, drain accepted dispositions and release public buffers. Repeated `plat_close` advances shutdown, returning BUSY while obligations remain and OK with a null pointer only after physical retirement. A close-progress wake also covers the last outstanding lease. No public buffer is revoked by close.

`plat_buf_new` reserves writable storage, off the typing path, for exactly the declared length. The writable view lasts until the successful move or release. `plat_buf_bytes` borrows a sealed result with explicit length, including empty success. `plat_buf_release` consumes and clears the handle; on UI it returns credit by bounded handoff, leaving any reclamation to the owner. Producer fill can run on an assigned worker under an exclusive lease. Only a returned job and the owning caller can transfer the completed handle. A BUSY/CAPACITY/UNSUPPORTED refusal leaves the handle and identity output untouched. `plat_cancel` addresses a live read, negotiating drop or pending claim. A terminal result already committed wins the race. A cancelled claim may need an asynchronous native relinquish fenced against a newer publication. OWNER_LOST names a previously successful publication.

### src/plat/plat_native.h

```c
/* Includes plat/plat.h. No XCB, GL, EGL or Wayland headers. */
typedef struct plat_native_binding plat_native_binding;
typedef struct { uint64_t epoch; uint32_t frame; } plat_frame_key;
typedef enum { PLAT_NATIVE_XSHM, PLAT_NATIVE_GL33 } plat_native_path;
typedef struct {
    plat_backend backend;
    union { struct { const char *display_name; uint32_t window, visual;
                     int screen; uint8_t depth; bool argb; } x11;
            struct { void *display, *egl_window; } wayland; } u;
} plat_native_target;
typedef enum { PLAT_FRAME_READY, PLAT_PRESENTED, PLAT_DISCARDED,
               PLAT_PRESENT_LOST } plat_notice_kind;
typedef enum { PLAT_TIME_UNKNOWN, PLAT_TIME_MONOTONIC } plat_clock;
typedef struct {
    plat_notice_kind kind; plat_frame_key key; plat_geometry geometry;
    plat_clock clock; uint64_t ns, msc; plat_result reason;
} plat_notice;

plat_result plat_native_bind(plat *p, plat_native_path path,
                             plat_geometry geometry, plat_native_binding **out);
plat_result plat_native_target_get(plat_native_binding *b, plat_native_target *out);
plat_result plat_native_arm(plat_native_binding *b, plat_frame_key key,
                            plat_geometry geometry);
int         plat_native_ready_fd(const plat_native_binding *b);
plat_result plat_native_poll(plat_native_binding *b, plat_notice *out);
void        plat_native_cancel(plat_native_binding *b, plat_frame_key key);
plat_result plat_native_unbind(plat_native_binding **inout);
```

L5 passes a binding through render setup and handles diagnoses. Bind on the setup/control path validates the backend, path and geometry, funds one exclusive lease and returns BUSY or STALE without borrowing on refusal. Target-get, arm, native poll, cancel and unbind are presentation-owner operations. Target-get borrows only within that lease. X11 supplies immutable server/window addressing; the presentation owner opens its own connection to that server/screen and owns all its Present subscriptions. GL matches the actual visual. A Wayland target is EGL-only. The presentation owner holds the `wl_egl_window` and controls resize, buffer scale, viewport destination, per-commit callback/feedback requests and EGL swap in one serialized commit sequence. The platform dispatch owner handles xdg/seat/selection on a distinct event queue. Use queue-specific proxies/wrappers and libwayland's prepare-read / cancel-read protocol when EGL and the dispatcher share a display; serialize teardown after all readers and the context have stopped. No concurrent dispatch of the same queue. Initialize and warm the single core GL 3.3 path via dlopen on a worker; transfer exclusive ownership before use.

Arm before the identified swap, on the exact surface, with one callback and one presentation-feedback object; clean up both on failed issue. The Wayland lease exposes a borrowed wake/aggregate fd to the presentation owner's root wait set. Arm/recheck on the owning side so a read by another Wayland queue that already enqueued feedback also wakes the owner. Kernel display-fd readiness alone can miss that case. Native poll is nonblocking. On Wayland it returns a pacing wake or the matching display result. On X11 the presentation driver matches Present events from its own connection and emits the signed render SPI notices directly; the Wayland arm/poll pair is unsupported there and native_ready_fd returns −1. X11 GL setup failure may choose XShm only after quiescent rebinding. Device fences and actual issue acknowledgements belong to render on both systems. A READY callback requests scheduling even if feedback is still outstanding. A destroyed or superseded callback cannot wake a new incarnation as if it had displayed. Cancel is idempotent for the named flight and detaches sources; unbind returns BUSY until the owner has quiesced GPU/server users, destroyed callbacks and released the EGL/native lease. Render’s sole UI adapter consumes worker results and advances the signed FSM; there is no duplicate T6 via the input FIFO.

## Ordering, pressure and the app turn

The producer assigns increasing sequence at observation to every ordered event, including synthetic repeat and terminal events. Use monotonic physical input time when the native clock can be mapped, unwrap its millisecond rollover and cap at ingest; otherwise use first local observation. Compare sequences, never native timestamps, to order input. XKB state uses physical code, effective keysym and normalized Shift/Ctrl/Alt/Super/Caps/Num/AltGr. Follow the pin for filtered command modifiers, AltGr text, dead-key prefixes and their swallowed releases, compose completion and cancellation. Release emits zero text. An xkb result longer than the inline capacity has no partial text; retain the key and report the unrepresentable compose result to the consumer (len zero, distinguished by PLAT_TEXT_TOO_LONG). Focus/seat loss clears down, repeat, compose, pointer/grab and seat serial state at ingress. A keymap change orders its snapshot before translation resumes. Held keys cannot cross the boundary under a different map. Drain already-read physical transitions before synthesizing at most one due repeat, and advance the timer past missed periods. Wheel deltas use donor Q8 notches (256 units per notch), preserve smooth XI2 axes, suppress emulated core duplicates and carry the current button mask. PLAN confines Wave 2 text entry to committed inline key text and local xkb compose; it excludes IME preedit and key/text pairing.

**Admission proof.** The ordinary queue credits are shared end to end between the producer, transport and undelivered FIFO. A transport buffer or mailbox never grants a second batch. Stamp and hold one indivisible producer record if ordinary space runs out. Stop native event consumption before another ordinary record can be materialized. On X stop polling queued events. On Wayland a dispatch-pending call can invoke several listeners. At a filled credit window suspend the dedicated producer at the current listener/emit cursor until space is returned, before returning to libwayland for the next callback. A callback yielding multiple logical transitions emits one at a time. Never block the UI or a shared work-pool lane on that wait. Paused dispatch is cancellable and services the private deadline/space/stop wait set; it can retire local transfer fds/deadlines into their prepaid terminal records without invoking recursive display dispatch. The display library and kernel may retain unread protocol bytes. The event cap counts normalized platform records, not those external buffers. Disconnect produces a terminal loss, never an invented release or silent continuation. Peers that require arbitrarily slow UI consumption eventually encounter transport backpressure.

Reserve separate terminal cells at intake for one read of each selection and one drop, one claim per selection, one ownership-loss obligation per current selection, and the one close and one connection loss of the incarnation. The four READ and CLAIM cells hold EMPTY / OWED / READY(completion) and publish only to the reply channel, without an event sequence. The other five cells hold EMPTY / OWED / READY(seq,event). A newly completed drop may move its record directly into an ordinary FIFO credit at the instant its sequence is assigned. This returns the drop terminal credit immediately, and allows multiple completed drops to queue before the first dequeue. If ordinary credits are exhausted, publish in the reserved cell instead. Once READY there, release only on move into its funded consumer destination. Prevent re-admission/replacement while the old identity still needs that cell. The paused ordinary record already owns its sequence. Poll picks the minimum among it (or its FIFO successor location), the ordinary head and READY event cells. OWED with no observed event has no sequence. Publication and selection of the minimum must see a consistent prefix; a later terminal cannot become visible past an unpublished earlier stamp. Focus-out, key-up, keymap, leave and configure are lossless ordinary transitions under backpressure. CLOSE and LOST latch once when observed, including when the ordinary queue is full. A close still unread behind a paused native callback becomes observable as ordinary credits return. The contract cannot bound when a close buried behind unlimited native input will be read. Stop requests and display hangup readiness have an independent wake. They take their observed position. Cleanup/peer acknowledgements may start immediately, while user-visible close and command effects wait their ordered turn. Later repeated terminal notifications for the same identity are inert. A parked producer wakes on space or close and may finish an ordered shutdown prefix; destruction never waits for a new native input to free a cell.

Use a single sequencer for ordered events. One feasible transport is prepaid, single-producer FIFO credits with value-only commands/credit returns over nonblocking `os_stream_pair`. Register its readable side or a level-correct aggregate wake in the app wait set. Account for partial records with a cursor, return space on consumption, signal after publication, drain before rechecking readiness and rearm if records remain. Only immutable, exclusively moved buffer leases cross with a record. Worker jobs publish shared results via `work_publish`; if full retain the leased result, call `work_continue`, and return. UI routes once by handle/generation, including cancellation, and consults `work_handle_finished` before freeing the argument. Keep platform dispatch and dependency completions able to run while an L5 command head is waiting. Before issuing a READ or CLAIM, the app reserves its destination. The issuing action fixes the edit order. On reply readiness the app routes each completion by ID into that destination, even with the command FIFO full. Later input keeps its place while the head waits. Render retirement, save/open completion and close progress likewise have independent readiness. Waiting for more command space to deliver the result that frees that space would deadlock.

L5 reserves an input destination before consuming an actionable head, then moves the entire key or transfer, with resolved buffer target and presented-view identity where relevant, exactly once. A command match consumes inline text with the key. Otherwise one insertion consumes the printable bytes. If OPEN is still acquiring, typing waits for that buffer and Save stays behind typing. Failed acquisition refuses the affected action visibly. Continue bounded work, transfer and reply service while dispatch is blocked. One exhausted input source never entitles another source to jump an already admitted app action.

**Simplest alternatives.** Adopt one FIFO order and one FIFO reader with a separate completion channel for READ and CLAIM. Dropping newest loses the release after a down burst. A priority close queue can close before an earlier Save. Reserving a fixed number of “important key” slots cannot cover arbitrarily many focus changes or key releases. Producer backpressure preserves each transition that has been read and gives an honest limit on how far ahead dispatch can run. Coalescing key repeats or pointer drags can remove an observable edit or hit. Keep them whole.

## Transfer ownership, deadlines and drag-and-drop

| Axis | Transition and obligation |
|---|---|
| Publication | EMPTY → CLAIMING(id, sealed bytes) → SERVING or FAILED. Queueing a claim reserves both the result record and eventual loss credit. Refusal takes neither. |
| Replacement | Before a local replacement, arbitrate already observed external loss for the outgoing identity. If its loss record is READY, leave the candidate with the caller and return BUSY, or fail an already queued claim visibly. Otherwise supersede the old local source and transfer its unused loss credit to the candidate in the same owner step. An accepted replacement can subsequently fail to claim. Old wire cancellation after local supersession is stale; old bytes survive for accepted native readers until they finish. |
| Receive | FREE → NEGOTIATING → RECEIVING → READY(result) → MOVED. Cancellation, timeout, limit, unsupported type and disconnect all release native resources and enter READY with a typed reason and no bytes. |
| Serve | FREE → SENDING → DONE / REFUSED / CANCELLED / TIMED_OUT. Each peer gets a separate cursor, immutable publication lease and terminal wire response or closed fd. Serving failures leave the current selection ownership intact. |
| Drop | IDLE → OFFERED → ACCEPTED → RECEIVING → FINISHED, with source change, leave before acceptance, rejection, expiry and teardown as explicit cancellation exits. |
| Lifecycle | OPENING → LIVE → DRAINING → CLOSED. Native loss enters DRAINING with a diagnosis. Quiescent backend restart may preserve a live platform window; reconnecting the display requires a new platform incarnation. |

Use exhaustive enum switches on each axis. A stale cookie, property event, offer, seat serial or worker completion cannot revive a finished transaction. Retire before counter wrap. Reuse of an X property/tuple needs a fresh requestor identity or a proven server-order boundary and drained stale replies; a local generation alone cannot authenticate a wire event lacking it. Notify peers exactly once on the protocol paths that support acknowledgement.

* **X11.** Keep independent PRIMARY and CLIPBOARD, asynchronous SetSelectionOwner plus GetSelectionOwner confirmation, timestamp checks and stale-time retry within the original deadline. Defer or refuse local reads while a claim is unconfirmed. A confirmed local read leases the same immutable bytes. Match requestor, selection, property, target and timestamp; verify type/format, declared size, bytes-after, and conversion expansion. Support TARGETS, TIMESTAMP, UTF8_STRING/text-plain, STRING with bounded Latin-1 conversion, MULTIPLE/ATOM_PAIR and ICCCM INCR with property-delete handshake and zero-length terminator. Fail individual bad MULTIPLE pairs correctly. A failed claim is distinct from SelectionClear for a live publication. During orderly exit, if the clipboard manager exists, request SAVE_TARGETS for owned CLIPBOARD, continue serving until the manager replies or the exit deadline wins, then release resources. PRIMARY is independent.
* **Wayland.** Track the current seat, valid input serial, selection offer and source identities. Use data-device for CLIPBOARD and the primary-selection protocol when advertised. Publish with a valid seat serial, offer text/plain;charset=utf-8 and text/plain, prefer UTF-8 on receive, distinguish lack of an offer from lack of protocol. A claim result on this protocol means the source was installed locally and the request flushed successfully; Wayland supplies no X-style ownership query. A compositor cancellation for that same source reports OWNER_LOST. Ignore retired-source callbacks. Missing PRIMARY returns UNSUPPORTED, so an automatic selection claim leaves the editor selection intact and a middle click never substitutes CLIPBOARD. Seat removal invalidates dependent operations and serials. Keyboard focus loss stops repeat and invalidates focus-bound offers/requests; a still-valid independently owned X selection can keep serving across focus loss.
* **Pipe and property service.** Register nonblocking endpoints with the owner's `os_waitset`. On AGAIN, wait for read or write readiness; advance only by actual bytes. HUP may accompany final readable bytes; drain before accepting EOF. Keep split conversions at their byte cursor. A write-side broken pipe yields one failed serve and cannot terminate the process via SIGPIPE. Remove registrations before close. Use `os_stream_read/write` for adopted Unix sockets. Wayland receive/send descriptors can be pipes; landed `os_stream_adopt` rejects non-sockets. Use the backend's nonblocking protocol-fd handling with the borrowed fd in the OS wait set and a SIGPIPE-safe write on the owner, without passing pipes to the local-stream API. No UI bulk allocation, conversion, copy or synchronous wait.
* **Deadlines.** Start at admission, including MIME negotiation, confirmation, deferred local reads, INCR handshakes and EOF; inherited phases take the minimum of their own expiry and the parent's. Arm the earliest deadline even if the queue is full, a peer never writes, or a socket stays perpetually writable. At expiry unregister, close or cancel once, detach the offer/observer and record TIMEOUT in its committed credit. Cleanup needing a physical worker holds only its prepaid lease until returned. Use one absolute lifetime. A progress-only deadline lets a byte just before every expiry retain an fd and a whole receive bank forever. An absolute policy may refuse a slow legitimate maximum-size exchange, yielding a retryable user-visible timeout. No automatic unbounded retry.
* **Xdnd and Wayland DnD.** Negotiate copy of text/uri-list. On X match source window, negotiated version/type or type list, action, position/status, drop timestamp and dedicated selection conversion, including INCR. On Wayland associate data-offer, enter serial, action and surface-local coordinates; accept the offered MIME, receive into this transaction, finish when supported and destroy the matching offer. Leave after drop releases hover state only; the accepted receive owns its offer until terminal. Retain the geometry/position at drop even if a configure intervenes before EOF. A second drop can occupy the receive slot as soon as the first completion moves to an ordinary FIFO credit or the consumer. Both payloads remain independently valid. Reject or backpressure a concurrent unadmittable offer using the protocol before promising success. A failed accepted drop has an ID and reason. It cannot dispatch an open.
* **Parsing boundary.** At ordered app intake bind the drop to its target and hand the immutable span to the landed `dnd_parse_uri_list` in bounded off-input-path work with a dedicated caller arena. Transfer completion reports receipt of the selected wire type. Parse result separately controls editor open. Pass the local hostname and the true length, including embedded NUL; on INVALID/NO_SPACE consume nothing. On OK preserve accepted path order and skipped/overflow counts. Open only when count is positive; an empty IPC request would launch an unintended untitled buffer. Keep the arena until the app FIFO owns its copied paths. Cancellation retires the worker before either buffer is reclaimed. The app uses the public header of the landed L4 `dnd` parser at intake (`mk/layers.mk`, LF-1). Neither L3 backend imports a parser or an upper layer.

A shared last-drop pointer fails with drop A, dequeue A, drop B, deferred parse A. Here the dequeue moves ownership, so B cannot mutate A. Adopt one common sealed buffer type for both backends. A synchronous copy in the key callback would allocate and scale with selection size. Publishing a builder by move allows large clipboard snapshots to be filled off path and submitted in constant time.

## Coherent surface, scale and frame retirement

The initial probe reports the work area and reachable maximum physical extents per axis over active outputs; zero means unknown, use the work area for initial estimation. Treat reported maxima as evidence for cold reservation and check every real configure against the admitted backend caps. The donor default initial client is floor(60% of work width) by floor(70% of work height), an (E) launch preference from editor/display_size.c. X uses actual client pixels for both coordinate spaces (scale120 = 120). Effective X font DPI prefers a finite positive Xft resource, then physical diagonal pixels × 25.4 / diagonal mm on native X, falling back to 96. On XWayland without Xft use 96 × validated desktop scale. The ratio of 25.4 mm/inch is (P); 96 logical DPI and the missing-data fallback are donor desktop conventions (E). Font DPI can change without a pixel resize. Refresh is the measured/reported mode interval in ns when available, zero otherwise.

Wayland applies xdg_toplevel's pending size when the serial-bearing xdg_surface configure completes. Zero configured dimensions delegate that axis to the client. Acknowledge the matching configure at the protocol-required boundary before its first buffer commit. Stage native resize with its matching commit so an older accepted packet cannot acquire newer buffer geometry. Hold/retire or explicitly quiesce an incompatible pending swap before acknowledging a binding that excludes it. Use current output integer scale, with swapped axes for quarter-turn transforms. With viewporter and fractional-scale together use the preferred scale in units of 1/120 (P, protocol denominator), buffer scale one and logical viewport destination. Otherwise use integer buffer scale. An output/scale-only transition produces a full snapshot. Use checked wide arithmetic and round positive buffer extent to nearest, floor((logical × scale120 + 60)/120), minimum one for a drawable axis; reject extents unrepresentable in the native signed range. Effective DPI = 96 × scale120/120. For signed wl_fixed coordinates use floor(raw × scale120/(256 × 120)) with checked wide intermediate; floor also governs negative coordinates. X coordinates are already client pixels. Preserve the last **logical** pointer position for a button/wheel with no coordinates of its own and transform it under that event's snapshot. Clipping and Q8 hit inversion belong to the saved render/layout transform. Zero drawable area suspends presentation without inventing a drawable pixel.

A change of incarnation, logical extent, buffer extent, mapping or effective DPI advances the serial even when rounding gives the same integer pixel dimensions. Use a fresh serial if an output change alters reported surface metadata. The FIFO can simply carry every snapshot. After dequeue, L5 may replace an unused pending SURFACE only with its immediately consecutive successor, before handing it to a font/layout/candidate reader. Any other event, candidate use, accepted frame or binding handoff seals it. Waiting font requests can still supersede obsolete preparations by request identity. Never mutate a dequeued value or search backwards in the queue to merge configures. In configure A → click → preferred scale B, applying B to the click selects a different physical pixel.

Compute em pixels from configured points × DPI / 72 (P, points per inch), round nearest, clamp to [1,max_px] for positive representable settings and return zero for invalid inputs or zero cap. The font service supplies its admitted maximum. At most the active and one preparing/retiring font bank coexist (G, signed two-bank transaction). While both are occupied remember only the newest copied settings and serial, cancel stale work, and wait for physical retirement. Admit new physical target, metrics, font sheet, runtime reserves and render caps before committing. Reflow from the byte anchor. A returned earlier bank, including after A→B→A, cannot install by matching dimensions alone. Preserve the previous admitted picture on MORE/BUSY/refusal or report unavailability. Every candidate carries the platform ID plus the full render/layout binding (font metrics, text/view, fixed regions, saved Q8 origin). Only successful full rebind and accepted render submit publish the matching layout hit serial. Validate a pointer against the accepted picture, transform and target; stale hits leave selection unchanged.

Wayland has one EGL path. Absence of EGL/core context, usable EGL window or wp_presentation gives a specific startup diagnosis. Register the frame callback and presentation feedback on the actual swap commit. A callback may pace later work with either axis pending. The swap's authenticated feedback resolves PRESENTED (preserve native sequence and converted monotonic time if valid), DISCARDED (untimed) or LOST (unrecoverable device/connection, invalidated binding or missing terminal evidence at watchdog). The signed render owner keeps ISSUED/T4, DEVICE_DONE/T5 and terminal display distinct, buffers early evidence until actual issue and clamps a correlated displayed time to T4. Unknown clock reports timing unavailable. On discard, retain the scene/atlas, complete the real fence in either order and allow the next frame to display. On LOST stop admission, invalidate the epoch, cancel observers, wait for physical quiescence, then rebind at depth one and require a full frame. Missing evidence never manufactures T5/T6. Repeated/foreign object, epoch or frame notifications are ignored. Use the donor GL completion watchdog of three seconds (G proposed duration from gl/renderer.inc GL_COMPLETION_TIMEOUT_NS), an absolute monotonic alarm after issue. It diagnoses a missing obligation even with a full input FIFO. Callback-only timing would incorrectly report an occluded or discarded frame as displayed.

## Connection ownership and the typing guard

The X window owner performs all xcb window, XKB/keymap, XI2, cursor-shape, EWMH, RandR, clipboard and Xdnd I/O. The presentation owner opens the independent X connection for the same window, exclusively issues raster XShm/Present requests and matches PIXMAP/window/request serial, mode and MSC on that connection. GL’s presentation owner has the equivalent exclusive context/display lease. XShm worker kernels only write disjoint output rows. One owner per connection applies through error and teardown. No X pointer or display fd is available to the UI submit interface. Even margin-only repaint, inline raster cursor pixels, region-only upload, idle/fence query and flush are immutable backend commands. Native window cursor shape is a distinct window command. No duplicate Present subscription on the input connection.

W0.9 traced cpu_paint_margins → xcb_flush → _xcb_conn_wait → read_packet. Two queued packets each caused 36 + 16 allocated bytes (M, pin W0.9 [AC], starting load 6.54/4.11/5.47). Moving just polling or privatizing a connection while flushing on UI reproduces the same stack. Adopt the single presentation owner from signed render Q6. The UI handoff consists of a bounded prepaid packet plus wake. Retain a result on publication refusal and retry publication only.

Under proposed A31 the release guard attributes allocations by thread. From ingest of prepaid records through mutation, layout and successful submit, the UI thread must have zero counted allocations. Record and report counts for other threads over the same window. The design permits no worker job that a key’s frame must wait for before submit. Any such job introduced later joins the enforced set. Preserve the original ingress stamp and include contention and real native issue in G1. Only real issue counts as T4. Evidence must include pending packets and real concurrent traffic with the release guard active.

## Admission and service bounds

P denotes a structural or protocol bound, G a proposed enforced admission rule or a named signed gate, M a cited measurement, and E an estimate. Counts from the pin establish compatibility choices; fitting a fixed allocation proves the capacity limit, not an event-rate or latency guarantee. Check all additions, products, conversions, time arithmetic and native narrowing before admission.

| Resource | Bound and derivation |
|---|---|
| Event and completion storage | G: 256 prepaid ordinary envelopes, matching X11_QUEUE_CAP and WL_EVENT_CAP at the pin. Under producer backpressure, 256 is a tuning constant because the producer preserves every admitted transition at any queue depth. P: one additional paused listener emission; 3 read/drop + 2 claim + 2 ownership loss + 1 close + 1 disconnect terminal cells = 9. Thus at most 262 ordered records and four completions are retained, including transport credits. Cap an envelope at 128 bytes on the supported ABI (G: 32 bytes of sequence/time/geometry + kind/alignment + the largest inline union, rounded up to the next power of two). Enforce by static size check before implementation admission; record storage ≤34,048 B = 266 × 128, plus fixed indices/credit states. A returned app action occupies its separately funded app FIFO slot. A move never duplicates credit. |
| Input and timers | G: eight inline bytes as in v1 PLAT_UTF8_MAX, enough for two maximum four-byte UTF-8 scalars (P). Whole result or typed overlength. One repeat key and one blink timer for the focused surface (P). Emit one due tick after draining already available physical input. Arm only while eligible. |
| Terminal reservations | P: each accepted read/drop/claim gets exactly one disposition. Ownership loss additionally has one credit per currently installed selection. A replacement must wait if the outgoing loss would occupy that credit; settle/reclaim it before accepting a new loss obligation. User-produced reads at slot saturation return BUSY; unadmitted peers get protocol refusal. P: one native flight owns at most one pending pacing callback and one terminal display result independently of the FIFO; render reserves its separate device result. Quiesce or destroy a superseded pacing callback before arming its successor. |
| Scope of byte cap | G: 64 MiB = donor X11_CLIP_DATA_MAX/WL_CLIP_MAX per sealed result, builder, source or receive; 128 MiB = two maximum payloads, matching both donor aggregate budgets. Charge actual capacity including slack and both allocations during growth, converted data, queued and popped buffers, UI builders, in-flight immutable sources, pending results and deferred release. Charge a shared allocation once until its last physical lease. Known local reads can share immutable bytes while moving distinct release obligations. Refuse before taking ownership when an additional allocation will not fit. Size-zero success still needs a descriptor. |
| Metadata and wire | G: at most 256 live public buffer handles per instance, one descriptor credit per ordinary FIFO slot, independent of byte length; a local alias consumes another handle credit. Three active receive lanes (the two selections plus the negotiated drop), 16 outgoing cursors (X NSERVE and WL_CLIP_SENDS), two claiming and two source records, eight X MULTIPLE jobs of at most 64 pairs each (pin NJOB/MAX_PAIRS). No deferred peer/waiter lists; BUSY/refusal before acceptance. Per MULTIPLE job ≤512 B of atom pairs (P: 64 × 2 × 4), plus per-pair bounded status/cookies. Charge at most one same-size reply representation again if decoded separately. Private pools prepay actual sizeof each descriptor × cap, including slot generation and release credit. |
| Transport bytes | G: one quantum ≤256 KiB (donor X11_CLIP_CHUNK_MAX) and at most one nonblocking payload I/O/owner service quantum, rotating among ready peers. Also limit by remaining byte budget, free output capacity, checked conversion expansion and native maximum request length after headers; use that smaller limit even on non-INCR offers. Negotiate INCR where needed. Reserve one shared 256 KiB conversion scratch and at most one separately outstanding 256 KiB payload reply/read staging area across peers (P: serialized chunk grant). Account for both separately from the 128 MiB pool, for ≤128.5 MiB platform-controlled payload/staging bytes. No eager multiplications of 256 KiB by peer count, whole-property fetches or extra transport copies held in pending jobs. External display/socket queues and driver buffers are separate from this owned-memory limit. UI handles one envelope and zero payload bytes per transfer handoff. |
| DnD parser storage | P: the landed parser can retain IPC_MAX_PATHS × IPC_PATH_CAP = 128 × 4,096 = 524,288 arena bytes including terminators, plus its fixed request/count outputs and one IPC_PATH_CAP scratch path. At most one decode grant per admitted app drop job; cost O(raw length) per completed parse. Hold the original capped span and budget the destination separately. Keep a 64 MiB malformed parse off the UI and the foreground composition lane. |
| Timeouts | G: five seconds absolute from admission of a peer operation, using the X donor's 5,000 ms timeout **once** across all phases. A 64 MiB item must average at least 12.8 MiB/s over that interval (P); progress never extends it. G: one second total from X exit handoff start, donor save_ms = 1,000. On close every child deadline is bounded by the exit deadline. For an operation admitted at t, the first scheduler opportunity at or after expiry retires peer resources before another byte or wait. This bounds the protocol lease despite a silent peer; elapsed wall time can additionally include descheduling or physical quiescence of already submitted work. A blocked display/server is connection loss under native supervision. A hung driver never grants permission to reuse backing. |
| UI and worker slices | G: min(64, available records, credits, remaining wall-time grant), with 64 and 500,000 ns taken from WORK_DRAIN_MAX_MESSAGES and WORK_DRAIN_BUDGET_NS. Share one remaining 0.5 ms UI slice across callbacks and platform handoffs; do not start a second full mailbox slice with the first slice's spent budget. Poll/input check between slices; time each constant-size operation, reserve its measured worst cost before starting it, and record individual overruns. Native protocol translation, keymap rebuild, large buffer filling/parsing and driver calls are outside that UI service. For work-pool jobs the landed cancellation interval is ≤5 ms CPU (G, work.h); choose chunk sizes within it or split the job. The landed one-shot DnD parser in a bulk job must retain its lease through return; do not promise interruptibility inside that function. |
| G1 | G: 1 ms p50 and 2 ms p99 physical ingress to real T4 for the accepted picture containing the edit (signed render, perf §0.2). Record native ingress, app wait, accepted frame identity, issue, matched device completion and display separately. A saturated FIFO can impose arbitrary queuing delay if the app is stopped. No capacity formula proves G1. Measure ordinary, after-idle and full fixtures with power/load and active release allocation count. Within G1, report the time from native key ingress to the UI wake as a separate measurement, both for ordinary input and for the first key after idle. |

INIT preallocates and touches hot pages. Reserve the ordinary control-command mailbox separately with at most one in-flight value per public window command kind, six cells derived from the six tagged operations, and return BUSY on an occupied kind. Preserve accepted command order across kinds. The activation cell owns at most two IPC_ACTIVATION_CAP strings; length and NUL are checked before moving. Render command storage, third-party library memory, OS socket limits, app FIFO storage and font's two banks are accounted by their owners. The platform allowance covers only the listed records and native adapter storage. Treat fd count, capacity and setup failures as ordinary typed errors. The dispatch owner services ready directions fairly, does not poll a full queue for space and rechecks the monotonic deadline after every wake.

## Integration witnesses for the signature

These are implementation acceptance sequences. Source inspection establishes the donor behavior and the failures to discriminate. Mock, real compositor, real X, and guarded-release observations are distinct evidence.

| Boundary and pin witness | Discriminating sequence and required observation |
|---|---|
| Platform → app order, A9 and W0.6h load/save control (v2-he7.15) | OPEN an acquiring file, type, Save, then finish or fail open. Saved bytes include the edit or both dependent actions visibly fail on the original target. Also fill the app command FIFO while the head owns a paste dependency; terminal reply/timeout and work drains still make progress. |
| Atomic translation and pressure, x11_input_test, x11_order_test, x11_stall_test, x11_input_fuzz, wayland_test | Fill to the last ordinary credit, then inject down, repeat, map change, focus-out, up, configure and close during a blocked app turn. Cross a Wayland dispatch batch boundary. Verify exact sequence, text, modifiers, one close, no post-focus repeat and no split text; with a drop expiry while paused, verify ordered terminal merge and credit return. Repeat with compose/dead key, cancelled compose, layout change and swallowed release. |
| Pointer input and activation, x11_xi2_test, x11_activate_test, x11_live_test, wayland_test:activation_test | Smooth scroll plus emulated wheel does not double-count; extended button, modifiers, pointer leave, cursor change, and IPC activation reach the intended surface. Stale seat serial or old activation-token callbacks cannot activate a new window. |
| Surface → font/layout, window_size_test, x11_font_size_test, wayland_test:fractional_test/output_test/editor_fractional_test; signed font scale transaction | Change Xft.dpi only, rotate an output, split xdg role/configure and preferred scale, send a click between scale changes (including negative coordinates). Delay the older font bank across A→B→A, exercise one-byte-short capacity and in-flight resize. Check exact physical dimensions, DPI, admission and current hit serial; failed rebind leaves the last viable picture. |
| Layout → pointer and minimap, R11 a7fo_click_contract, pointer_overscan_contract, R12/R13 minimap_grab_contract/minimap_text_columns | Click/drag wrapped ASCII/wide rows at fractional origin under the saved transform. Keep minimap grab offset and gutter/map separation through reflow; a stale pointer leaves the selection untouched. |
| Input → visible edit, R1/R2/R24/R25 n6090_typing_top, vp4m_return_bottom, cxho25_typing_jump, c26_restart_rows/c26_follow_truth; R6–R8 starved_follows/near_end_repro/navigation.py | Associate the original Return, repeat, jump or PageUp with the accepted visible frame. Check top and caret under wrap restart and occupied bulk work; a previous picture or byte-zero placeholder cannot satisfy G1. |
| X selections, x11_clip_test and x11_clip_review_test | Claim/fail/steal independently per selection, read while confirming, replace while serving, match stale replies, INCR at the cap and one byte over, MULTIPLE saturation, handoff at exit with responding and silent manager. Hold A across B and call release in reverse order. Byte ceiling includes both. Timeout, loss and empty success have distinct outcomes. |
| Wayland selection, wayland_test:primary_plat_test/primary_editor_test, wayland_mock.h, R34 and real compositor | Copy via public moved buffer and paste in a separate real client, then reverse direction. Exercise foreign/local PRIMARY, missing protocol, no offer, valid seat serial, source cancellation, send blocked, short reads/writes, EOF and trickle. Regular clipboard survives each independent PRIMARY transition. Stop the app drain during expiry and verify fd release and one eventual terminal. |
| DnD → app, R26 x11_dnd_test, dnd_test/dnd_fuzz, editor drop cases and Wayland data-device sequence | Change source mid-negotiation, attempt unsupported action, leave after accepted drop, complete A and B before consuming A's bytes, interleave clipboard. Check both unique spans and geometry, exact Finished/cancellation and opened path order. Foreign URI, decoded/raw NUL, overflow, empty result and arena exhaustion cannot produce unintended opens. |
| Q6, W0.9 finding 8/A12, raster_test/fuzz, review_chrome_damage, raster_blink_damage | Queue Present packets on the raster connection before margin, strip, cursor-only and region-only submits. Attribute all native calls to their owner, assert active release guard and zero counted allocations over the full UI path; record T4 for the key-bearing frame. Exercise window commands simultaneously. |
| Frame callback/feedback, wayland_startup_test, wayland_gl_test, gl_gate_test/gl_bench_test, render_test | Missing EGL/feedback gives the precise startup reason. Send callback without feedback, early feedback during issue, discard before fence and after fence, then display the next frame. Exercise unknown clock, stale epoch, duplicate, missing feedback and lost device. Exactly one real issue, no T6 on discard, no synthetic T5, recovery only after physical quiescence. Mock absence must fail its required lane. |
| Render → platform restart, signed render pressure/lease witnesses; real Wave 2 display exit | Park a worker publication on a full mailbox, cancel, restart and deliver an old acknowledgement. Release only after physical completion. Verify real X11 GL, X11 XShm and Wayland EGL pictures, and a displayed frame following compositor discard. A mock frame or frame callback alone cannot meet the real-display exit. |
| Adapter boundaries and cost, xcursor_fuzz, wayland_fuzz, x11_bench, wayland_bench and editor_bench/editor_large_bench | Fuzz parser/cursor and seat/offer/configure lifetimes through public seams or link wrappers. Verify the include DAG, isolated whole-archive link, committed Wayland XML generator --check, timeout on stalled fixtures, and no silent skip. Stamp per-step and whole-operation timing, ordinary and maximum fixtures, power and load. |

R5 binary-tab inkless fallback and R14 closed-kept ownership in the harvest’s integration table belong to font/render and tabs/session respectively. The platform supplies the ordered activate/close event and frame identity to those witnesses; it owns neither policy.

## Footprint and donor replacement

M = 4,847 comment-free production lines at oracle pin 6fec2e8, from the harvest's disjoint file inventory (X11, Wayland and editor/display_size.c). Source count is independent of power and load. E below uses the same accounting perimeter, including public/private headers and excluding generated protocol outputs, tests, benches and fuzz fixtures. Credit an obligation to one owner only.

| Production group | Estimated lines and derivation (E) |
|---|---:|
| Public values, calls and native lease front door | 150 event/config/transfer + 110 native types/dispatch = 260 |
| Common ordering and app readiness | 175 credit/terminal merge + 160 pump, wait, close + 125 command and result routing = 460 |
| X window, keyboard, focus and setup | 340 setup/activation/probe + 360 keyboard/compose/timers = 700 |
| X pointer and display helpers | 215 cursor + 210 XI2 + 135 DPI/RandR sizing = 560 |
| ICCCM and Xdnd | 570 selection/INCR/MULTIPLE + 140 exit/ownership + 190 drop negotiation = 900 |
| Wayland window, seat and output | 250 connection/xdg + 260 seat/input + 170 output/fractional sizing = 680 |
| Wayland selections and DnD | 280 source/offer/PRIMARY + 140 pipe service + 85 drop completion = 505 |
| Wayland EGL binding and feedback | 90 commit/resize lease + 100 callback/feedback matching + 60 cancel/quiesce = 250 |
| Common transfer pool and lifetime | 140 accounting/move + 115 deadline/disposition + 80 cancellation/retirement = 335 |
| **Total** | **4,650**, or **197 fewer** than 4,847 (E delta). |

New lines buy backpressure at a listener boundary, independent transfer ownership, terminal credits, geometry serials and typed EGL retirement. Common buffer accounting, deadline service, app readiness and geometry replace parallel backend versions and the Wayland shm path. The line estimate prices the public native lease and Wayland protocol glue here. Render's driver SPI, raster X connection implementation, atlas/scene, GL draw and the L5 app adapter retain their signed owners' budgets. Reconcile actual counts by this perimeter when the implementation lands.

| Retired donor structure | Replacement |
|---|---|
| x11/clip.c ↔ wayland/dispatch.c mutual private.h includes and backend-specific clipboard buffer | A common platform type/credit layer below two sibling backend implementations; the selected typed adapter receives only its own private state. |
| Xdnd shared last-drop bytes and Wayland drop-as-leave | One leased buffer carried by each successful drop, with a separate hover lifetime and URI-list parsing at app intake. |
| Wayland drop-newest on a full FIFO | Backpressure on ordinary emissions, independent prepaid terminal cells, one sequenced merge. |
| Compositor discard through wayland_frame_result → gl_fail | Untimed terminal display result on the matching flight with independent real device retirement. |
| Editor-owned DPI math and UI X flush | Platform-owned coherent geometry and an exclusively off-UI owner per native connection. |

**Evidence.** PLAN §§2 and 4.6, CLAUDE laws 2/4/5, the Wave 2 L3/L4 harvest, Wave 1 app intake, signed viewport/layout, signed render and font at af0e95e (A30), and the advisor’s wave2-render-errata R-A through R-C. The matching font/render revision is available as refs/remotes/origin/t3/wave2-font-render-r2. Lower contracts are src/os/os.h, src/work/work.h, src/dnd/dnd.h, src/ipc/ipc.h and LF-1-dnd. Donor evidence is the pinned X11/Wayland implementation, display_size.c and named witness functions above.

**Advisor signature** ____________________   **Date** ____________________
