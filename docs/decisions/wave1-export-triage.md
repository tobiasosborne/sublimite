# Wave 1 export triage

The release archives have **105** unreferenced, unreserved definitions in the ten requested modules. This review proposes **71 reservations**, identifies **27 dead exports** and leaves **7 exports for the advisor**. The 1.22 baseline was 111. All six fewer diagnostics come from base and scan.

| Module | At 1.22 | Current | RESERVE | DEAD | DECIDE |
|---|---:|---:|---:|---:|---:|
| base | 13 | 9 | 3 | 4 | 2 |
| ipc | 20 | 20 | 15 | 5 | 0 |
| trace | 16 | 16 | 15 | 1 | 0 |
| work | 19 | 19 | 11 | 4 | 4 |
| scan | 2 | 0 | 0 | 0 | 0 |
| utf8 | 13 | 13 | 7 | 5 | 1 |
| indent | 5 | 5 | 5 | 0 | 0 |
| find | 21 | 21 | 13 | 8 | 0 |
| config | 1 | 1 | 1 | 0 | 0 |
| dnd | 1 | 1 | 1 | 0 | 0 |
| **Total** | **111** | **105** | **71** | **27** | **7** |

## Basis for the decisions

The checkout had no release objects. `make -j2 check-exports` built the six release archives and loose `main.o` and passed for `os`. Running the same Python checker with the ordered `.members` lists and layer map, omitting `--module os`, produced the individual diagnostics. Exit status 1 is expected for this inventory. Test, fuzz and bench objects do not contribute references. [Gate policy](v2-0fv.21.md) · [Checker](../../tools/check_exports.py)

In the tables, the Plan or signed work identifies the future feature and owner. Where the Plan names a capability, the accompanying section identifies how that caller uses the export or maps the capability to its current C spelling. A harvest API inventory by itself does not establish a caller. A filed measurement or standalone tool can justify an explicit reservation even though its own object is outside the release scan. The app document labels its header sketches a proposal. Its rows below rely on the independently planned Wave 1 loop and buffer owners, and the file brief where applicable. The optional mailbox routing choice stays with the advisor. [PLAN §§4.7, 5–6](../PLAN_V2.md) · [Application contract](wave1-app-spec.md) · [File brief](../briefs/L2-1-file.md)

DEAD applies to the **exported interface**. Several implementations still have callers inside the same module. In particular, IPC needs its wire codec, and find needs its literal engine and core regex compiler. Some of these calls cross translation units. Removing their definitions, or merely moving declarations to a private header, would either break linkage or leave the diagnostic in place.

## Per-module decisions

### base

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `base_arena_reset` |  | No planned owner needs a whole-arena rewind. Mark rollback and arena release already have callers. |  |
| `base_cpu_has_popcnt` |  | Find counts mask bits with fixed-width lane sums. No planned kernel selects a POPCNT implementation. |  |
| `base_malloc_guard_active` | The release Law 2 observer must check that interposition is live. [wave1-edit-spec.md](wave1-edit-spec.md), Proof obligations for the implementer. [PLAN](../PLAN_V2.md) §6 Wave 1, live release malloc guard. |  |  |
| `base_malloc_guard_begin` | The production-path observer opens the counting window at input ingress. [wave1-edit-spec.md](wave1-edit-spec.md), Proof obligations for the implementer. [PLAN](../PLAN_V2.md) §6 Wave 1, live release malloc guard. |  |  |
| `base_malloc_guard_end` | The observer closes the window at actual submit and reads the allocation count. [wave1-edit-spec.md](wave1-edit-spec.md), Proof obligations for the implementer. [PLAN](../PLAN_V2.md) §6 Wave 1, live release malloc guard. |  |  |
| `base_malloc_guard_exempt_begin` |  |  | Does Wave 2 X11 need a post-submit library exemption during another active guard window? The signed edit proof forbids exemptions through submit. |
| `base_malloc_guard_exempt_end` |  |  | Keep paired with the opening exemption until the X11 owner identifies a concrete library-call interval. |
| `base_pool_count_live` |  | The piece reserve tracks extent ownership and free slots. No filed owner asks base for its diagnostic live-block count. |  |
| `base_version` |  | The L0 dossier §2.2 gives the version test as the reason to keep the string. No planned command consumes it. |  |

### ipc

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `ipc_args_fini` | Main releases the parsed request and its arena after launch or handoff. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_args_read_stdin` | The CLI reads the stdin buffer for a dash operand before handing it to the editor. PLAN §3 also names stdin buffers. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_args_set_activation` | The launcher copies activation data into the forwarded request. L0-6 explicitly retains this handoff. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_client_send` |  | Convenience call passing a null display to ipc_client_send_display. No separate general-endpoint client is specified. |  |
| `ipc_client_send_display` | A secondary graphical launch forwards its request to the primary for the selected display. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_launcher_pair` | Main creates the launcher/UI pair for a primary or isolated --wait launch. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_launcher_wait` | The launcher waits for the UI to report closure or disconnect. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_parse_args` | Main parses launch arguments including the file:line:col request. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_result_string` |  | The L0 dossier §5.6 assigns result-string cases to IPC conformance. No planned CLI diagnostic requires this formatter. |  |
| `ipc_server_adopt_wait` | The UI adopts the launcher's wait channel and associates its token with the initial request. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_server_drain` | The UI services a ready endpoint and dispatches the buffered requests. The W1 headless command stream is a separate transport. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_server_fd` | The application loop registers the EDIP server's readiness descriptor for incoming open requests. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_server_fini` | The primary or isolated UI releases the endpoint and client resources at shutdown. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_server_init` |  | Convenience call passing a null display to ipc_server_init_display. The primary and isolated launch modes have their own entries. |  |
| `ipc_server_init_display` | The graphical primary claims the endpoint for the same selected display. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2 and §7 L0-6. [PLAN](../PLAN_V2.md) §§1, 4.7 and §6 Waves 1/4, namespaced launcher handoff and IPC path jumps. |  |  |
| `ipc_server_init_isolated` | A --new-instance UI accepts its own launcher channel without binding a shared endpoint. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_server_report_closed` | L5 completes the retained --wait token once the opened members have closed. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_server_token_live` | L5 sweeps wait associations after clients disconnect. The token-live obligation is explicit in §5.2. [wave1-L0-harvest.md](wave1-L0-harvest.md), §5.2, §5.6 and §7 L0-6. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 4, launcher and session parity. |  |  |
| `ipc_wire_decode` |  | The IPC server decodes a wire request before invoking L5 with an ipc_request. Decoding remains necessary inside IPC. |  |
| `ipc_wire_encode` |  | The IPC client encodes the request passed by main. No planned external caller constructs EDIP frames. |  |

### trace

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `trace_dump` | The editor writes the EDTRACE1 timing and input dump consumed by the planned replay and latency tools. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_fmt_dump_free` | The offline tools release the loaded dump and its owned arrays. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_fmt_latencies` | The G1 analyzer pairs per-frame timestamps into latency samples. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_fmt_load_dump` | The offline tools load a validated dump before replay or measurement. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_fmt_pct` | The G1 analyzer reports the p50 and p99 of those samples. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_input_clipboard` | The UI ingress log preserves clipboard event metadata for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_filechange` | The UI ingress log preserves file-change notifications for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_focus` | The UI ingress log preserves focus transitions for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_key` | The UI ingress log preserves key input for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_pointer` | The UI ingress log preserves pointer motion and buttons for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_resize` | The UI ingress log preserves surface-size changes for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_input_wheel` | The UI ingress log preserves wheel deltas for replay. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_record` | The GUI pipeline records G1 timing events at the current monotonic time. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_record_at` | The GUI pipeline records an event with its original timestamp, including presentation feedback. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 L0-4. [PLAN](../PLAN_V2.md) §3 and §6 Waves 2/4, preserve trace/replay and measure G1. |  |  |
| `trace_replay` | The replay tool delivers the recorded input log to its sink. [wave1-L0-harvest.md](wave1-L0-harvest.md), §3.2 and §7 “Out-of-scope / handed-off items”. [PLAN](../PLAN_V2.md) §3 and §6 Wave 4, trace/replay and G1 p50/p99. |  |  |
| `trace_reset` |  | The planned capture and replay paths need dump and load. No filed session requires clearing live history in place. |  |

### work

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `work_cancel` | The buffer owner cancels stale mapped-count work on close or reload. [wave1-app-spec.md](wave1-app-spec.md), app_buffers.h, Acquisition and counting. [PLAN](../PLAN_V2.md) §6 Wave 1, L5 buffer counting. |  |  |
| `work_cancel_time_ns` |  | No planned worker makes a scheduling or retirement decision from the cancellation timestamp. |  |
| `work_continue` | A count job yields after a chunk and resumes on the same lease. [wave1-app-spec.md](wave1-app-spec.md), app_buffers.h, Acquisition and counting. [PLAN](../PLAN_V2.md) §6 Wave 1, L5 buffer counting. |  |  |
| `work_handle_finished` | Two-phase file close polls physical completion before releasing a job's backing. [L2-1-file.md](../briefs/L2-1-file.md), Scope, Close bullet. [PLAN](../PLAN_V2.md) §6 Wave 1, file acquisition and close. |  |  |
| `work_mailbox_bind` |  |  | The app spec allows registered per-handle receivers as an alternative to its ordinary dispatcher. Does any owner require registration? |
| `work_mailbox_drain` | The UI routes ordinary mailbox completions once per turn through its bounded dispatcher. [wave1-app-spec.md](wave1-app-spec.md), app_loop.h. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 1, shared dispatcher and lease retirement. |  |  |
| `work_mailbox_drain_bounded` |  | The ordinary drain already calls this implementation with the standard limits. No planned external caller sets a separate message count and deadline. |  |
| `work_mailbox_pending` | Before sleeping, the root loop checks remaining mail alongside runnable work and finished handles (also “Runnable work, fairness and wakes”). [wave1-app-spec.md](wave1-app-spec.md), app_loop.h. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 1, shared dispatcher and lease retirement. |  |  |
| `work_mailbox_receive` |  |  | The file brief requires dossier §0.1, which directs file to use selective receive. Does file still call it under the app’s ordinary single-dispatcher delivery? [File dossier §0.1](wave1-L2-L5-harvest.md) · [app_loop.h](wave1-app-spec.md) |
| `work_mailbox_receive_bounded` |  | A private selective scan with independent limits has no planned caller in the shared-dispatch model. |  |
| `work_pool_init` | Headless app creation initializes the shared pool for bulk file, journal and count jobs. L0 dossier §4.2 names the basic constructor. [wave1-app-spec.md](wave1-app-spec.md), app_loop.h. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 1, shared dispatcher and lease retirement. |  |  |
| `work_pool_init_foreground` | The GUI enables the dedicated foreground lane for navigation while bulk work is occupied. §4.3 also names copied-file jumps. [wave1-L0-harvest.md](wave1-L0-harvest.md), §4.2 and §4.3. [PLAN](../PLAN_V2.md) §6 Wave 2, navigation during occupied bulk work. |  |  |
| `work_pool_shutdown` | App destruction retires and joins the owned pool. L0 dossier §4.2 names the consuming operation. [wave1-app-spec.md](wave1-app-spec.md), app_loop.h. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 1, shared dispatcher and lease retirement. |  |  |
| `work_pool_wake` | Main registers the pool wake descriptor in the root wait set. [wave1-app-spec.md](wave1-app-spec.md), app_loop.h. [PLAN](../PLAN_V2.md) §4.7 and §6 Wave 1, shared dispatcher and lease retirement. |  |  |
| `work_pool_worker_id` |  | No filed owner needs the worker identity diagnostic. Ownership and retirement use handles. |  |
| `work_prioritize` |  |  | Wave 2 requires navigation under bulk load. A19 removed the lineidx builder that upgraded an existing lease. Does the GUI still upgrade a live job? |
| `work_publish` | A count worker publishes its immutable chunk result to the owning UI. [wave1-app-spec.md](wave1-app-spec.md), app_buffers.h, Acquisition and counting. [PLAN](../PLAN_V2.md) §6 Wave 1, L5 buffer counting. |  |  |
| `work_submit` | The mapped-open owner submits a WORK_BULK job with its retained backing. [wave1-app-spec.md](wave1-app-spec.md), app_buffers.h, Acquisition and counting. [PLAN](../PLAN_V2.md) §6 Wave 1, L5 buffer counting. |  |  |
| `work_submit_batch` |  |  | Q7 cites batch admission for capacity arithmetic. Single-job submit already uses it internally. Does a Wave 2 raster owner require atomic multi-job admission? |

### scan

Zero would-fail exports. `nm -u build/rel/src/piece/piece.o` reports both `scan_count` and `scan_find_nth_newline`. Piece counts line feeds and locates the requested line feed in a span.

### utf8

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `utf8_artwork_step` | The font fallback walks a cluster's scalars to obtain its unshaped artwork projection. [wave1-L1-harvest.md](wave1-L1-harvest.md), §2.2. [PLAN](../PLAN_V2.md) §§2–3, 4.5–4.6 and §6 Wave 2, viewport, layout and font. |  |  |
| `utf8_break_certain` |  |  | Layout's long-line checkpoints need a valid segmentation restart. Will they test adjacent units for a certain break or retain segmentation state? |
| `utf8_cell_width_table` |  | Table lookup implements the inline scalar width fallback. The named layout contract is cluster width. Preserve linkage if another module later inlines utf8_cell_width. |  |
| `utf8_cluster` |  | The one-shot length-plus-width path underlies the module's width wrapper. No filed owner selects an unbounded scan for whole clusters. |  |
| `utf8_cluster_artwork` | The GUI prepares a bounded artwork key from the bytes of a complete cluster. §2.2 names the whole-cluster projection. [wave1-L1-harvest.md](wave1-L1-harvest.md), §2.2. [PLAN](../PLAN_V2.md) §§2–3, 4.5–4.6 and §6 Wave 2, viewport, layout and font. |  |  |
| `utf8_cluster_emoji` | Layout classifies the cluster before handing its complete bytes to glyph lookup. [v2-0fv.11.md](v2-0fv.11.md), Blast radius, every consumer of gcb_of, audited. [PLAN](../PLAN_V2.md) §§2–3, 4.6 and §6 Wave 2, layout and GSUB with the ©/® fix. |  |  |
| `utf8_cluster_step` | Wrapped layout segments and measures clusters in bounded increments across input spans. [wave1-L1-harvest.md](wave1-L1-harvest.md), §2.2. [PLAN](../PLAN_V2.md) §§2–3, 4.5–4.6 and §6 Wave 2, viewport, layout and font. |  |  |
| `utf8_cluster_width` |  | No filed owner needs the separate one-shot width-only pass. The layout step already returns cluster width. |  |
| `utf8_emoji_class` | Font GSUB checks a scalar's emoji class when selecting whole-cluster artwork. [v2-0fv.11.md](v2-0fv.11.md), Blast radius, every consumer of gcb_of, audited. [PLAN](../PLAN_V2.md) §§2–3, 4.6 and §6 Wave 2, layout and GSUB with the ©/® fix. |  |  |
| `utf8_grapheme_next` |  | The one-shot forward walker serves other UTF-8 routines. The planned UI repair has a resumable entry. |  |
| `utf8_grapheme_prev` | Cursor-left motion finds the previous cluster start on an already bounded span. §2.2 names backward grapheme segmentation. [wave1-L1-harvest.md](wave1-L1-harvest.md), §2.2. [PLAN](../PLAN_V2.md) §§2–3, 4.5–4.6 and §6 Wave 2, viewport, layout and font. |  |  |
| `utf8_grapheme_step` | Viewport motion and grapheme repair segment a cluster with a resumable byte budget. The edit spec assigns repair to the caller after the proposed edit.h. [wave1-L1-harvest.md](wave1-L1-harvest.md), §2.2. [PLAN](../PLAN_V2.md) §§2–3, 4.5–4.6 and §6 Wave 2, viewport, layout and font. |  |  |
| `utf8_prev` |  | Backward scalar stepping serves utf8_grapheme_prev internally. No filed external command moves by a single decoded scalar. |  |

### indent

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `indent_bracket_match` | GUI paint locates the bracket at the cursor and its mate in the visible window. §3 identifies paint as the consumer. [v2-0fv.12.md](v2-0fv.12.md), §§2–3. [PLAN](../PLAN_V2.md) §3 and §6 Waves 1/2, verbatim indent behavior and wrapped Return. |  |  |
| `indent_detect` | The buffer owner detects indentation and line-ending style on open, then supplies it to typing commands. [v2-0fv.12.md](v2-0fv.12.md), §§2–3. [PLAN](../PLAN_V2.md) §3 and §6 Waves 1/2, verbatim indent behavior and wrapped Return. |  |  |
| `indent_on_close_brace` | The closing-brace command computes its one-stop dedent before submitting the replacement. [v2-0fv.12.md](v2-0fv.12.md), §§2–3. [PLAN](../PLAN_V2.md) §3 and §6 Waves 1/2, verbatim indent behavior and wrapped Return. |  |  |
| `indent_on_enter` | The Return command copies the leading run and the detected line ending. The W1 edit harvest §4.3 names the typing operation. [v2-0fv.12.md](v2-0fv.12.md), §§2–3. [PLAN](../PLAN_V2.md) §3 and §6 Waves 1/2, verbatim indent behavior and wrapped Return. |  |  |
| `indent_trailing_ws` | GUI paint obtains trailing-blank ranges for the visible text window. L1 harvest §3.2 names the range query, and the landed §3 gives its current spelling. [v2-0fv.12.md](v2-0fv.12.md), §§2–3. [PLAN](../PLAN_V2.md) §3 and §6 Waves 1/2, verbatim indent behavior and wrapped Return. |  |  |

### find

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `find_lit_init` |  | Private literal plan construction links find's regex and literal objects. The private literal.h declares it. |  |
| `find_lit_seek` |  | Private literal-plan seeking links the same objects. No L4 caller is specified. |  |
| `find_literal` |  | No planned client asks for the fixed offset-array literal result. Unified visit covers counting and selecting hits. |  |
| `find_literal_next` | The skip-next-occurrence plan needs the first following literal match at or after an offset. L1 harvest §7.2 names this single-match API. [wave1-L2-L5-harvest.md](wave1-L2-L5-harvest.md), §4.3, view_skip_next_occurrence. [PLAN](../PLAN_V2.md) §§3–4.2, harvest edit command plans. |  |  |
| `find_literal_visit` | Find session and fif stream selected literal matches with native case and word options. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_bytes` | The caller funds opaque program storage before compiling a Wave 3 query. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_captures` | A replacement transaction obtains capture ranges by re-running the expression anchored at a chosen match. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_compile` |  | The option-aware compiler calls this core in the same module, including for a case-sensitive query. No second public compiler caller is filed. |  |
| `find_regex_compile_options` | The fif worker compiles an option-aware regex against the unified find header. [wave1-L2-L5-harvest.md](wave1-L2-L5-harvest.md), §8, fif row. [PLAN](../PLAN_V2.md) §6 Wave 3, fif on unified find. |  |  |
| `find_regex_group_count` |  | Capture results already contain the group count. No filed consumer needs a separate pre-match query. |  |
| `find_regex_next` |  | No filed caller asks for this standalone unfiltered one-match regex scan. The planned session uses visit and continuation. |  |
| `find_regex_next_budget` | Wave 3 filtered regex enumeration carries the remaining work allowance across successive candidates. §7.3 names the caller-budgeted driver. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_prefix` |  | Find owns its regex prefix filter. No filed fif or session prefilter requires the compiled prefix to escape. |  |
| `find_regex_scratch_bytes` | The replacement caller sizes the single-call workspace for anchored captures. [v2-0fv.19.md](v2-0fv.19.md), Public contract. [PLAN](../PLAN_V2.md) §2 and §6 Wave 3, caller-funded streaming visit. |  |  |
| `find_regex_search` |  | No planned client asks for the fixed offset-array result in addition to streamed results. |  |
| `find_regex_stream_begin` | The find worker begins a resumable regex visit with a caller-funded workspace. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_stream_scratch_bytes` | The find worker sizes storage for the resumable regex visitor. [v2-0fv.19.md](v2-0fv.19.md), Public contract. [PLAN](../PLAN_V2.md) §2 and §6 Wave 3, caller-funded streaming visit. |  |  |
| `find_regex_stream_step` | The worker resumes that visit at NFA byte boundaries until completion. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_visit` | The Wave 3 consumers visit regex matches through the promoted visitor. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_word_visit` | Whole-word regex visits perform boundary filtering inside the shared matcher. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |
| `find_regex_work_budget` | The shared request budget bounds successive filtered regex retries. §7.3 names the budget initializer alongside its consuming driver. [wave1-L1-harvest.md](wave1-L1-harvest.md), §§7.2–7.5. [PLAN](../PLAN_V2.md) §§2–3 and §6 Wave 3, find_session, find_panel, replace_txn and fif. |  |  |

### config

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `config_toml_parse` | Wave 3 settings glue consumes the transactional TOML visitor. See the same-module qualification below. [LF-2-toml.md](LF-2-toml.md), The unit in one paragraph and opening scope. [PLAN](../PLAN_V2.md) §2 and §6 Wave 3, transactional TOML parser and settings glue. |  |  |

The Plan places settings glue in `config`. If it calls the parser from another `src/config/*.o`, the gate will still report the export because both objects belong to `config`. The reservation covers the explicitly planned parser capability. Recheck the public boundary once the settings caller exists.

### dnd

| Export | RESERVE — caller and authority | DEAD — basis | DECIDE — advisor question |
|---|---|---|---|
| `dnd_parse_uri_list` | The GUI drop handler decodes a text/uri-list payload into an IPC open request. [LF-1-dnd.md](LF-1-dnd.md), The module in one paragraph. [PLAN](../PLAN_V2.md) §§3, 4.6 and §6 Wave 3, owned drop payload and verbatim DnD. |  |  |

## Proposed additions to tools/export-reservations.txt

Each second field is an existing decision or brief filename stem. The reason gives the section and consuming work. The Plan supplies wave authority in the tables above. Its filename alone would fail the reservation parser. A read-only check with these additions appended in memory to the 22 existing reservations removed exactly 71 release diagnostics. The remaining 34 in scope are the 27 DEAD and seven DECIDE entries.

```text
base_malloc_guard_active wave1-edit-spec # Proof obligations for the implementer. The release Law 2 observer must check that interposition is live.
base_malloc_guard_begin wave1-edit-spec # Proof obligations for the implementer. The production-path observer opens the counting window at input ingress.
base_malloc_guard_end wave1-edit-spec # Proof obligations for the implementer. The observer closes the window at actual submit and reads the allocation count.
ipc_args_fini wave1-L0-harvest # §5.2 and §7 L0-6. Main releases the parsed request and its arena after launch or handoff.
ipc_args_read_stdin wave1-L0-harvest # §5.2 and §7 L0-6. The CLI reads the stdin buffer for a dash operand before handing it to the editor. PLAN §3 also names stdin buffers.
ipc_args_set_activation wave1-L0-harvest # §5.2 and §7 L0-6. The launcher copies activation data into the forwarded request. L0-6 explicitly retains this handoff.
ipc_client_send_display wave1-L0-harvest # §5.2 and §7 L0-6. A secondary graphical launch forwards its request to the primary for the selected display.
ipc_launcher_pair wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. Main creates the launcher/UI pair for a primary or isolated --wait launch.
ipc_launcher_wait wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. The launcher waits for the UI to report closure or disconnect.
ipc_parse_args wave1-L0-harvest # §5.2 and §7 L0-6. Main parses launch arguments including the file:line:col request.
ipc_server_adopt_wait wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. The UI adopts the launcher's wait channel and associates its token with the initial request.
ipc_server_drain wave1-L0-harvest # §5.2 and §7 L0-6. The UI services a ready endpoint and dispatches the buffered requests. The W1 headless command stream is a separate transport.
ipc_server_fd wave1-L0-harvest # §5.2 and §7 L0-6. The application loop registers the EDIP server's readiness descriptor for incoming open requests.
ipc_server_fini wave1-L0-harvest # §5.2 and §7 L0-6. The primary or isolated UI releases the endpoint and client resources at shutdown.
ipc_server_init_display wave1-L0-harvest # §5.2 and §7 L0-6. The graphical primary claims the endpoint for the same selected display.
ipc_server_init_isolated wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. A --new-instance UI accepts its own launcher channel without binding a shared endpoint.
ipc_server_report_closed wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. L5 completes the retained --wait token once the opened members have closed.
ipc_server_token_live wave1-L0-harvest # §5.2, §5.6 and §7 L0-6. L5 sweeps wait associations after clients disconnect. The token-live obligation is explicit in §5.2.
trace_dump wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The editor writes the EDTRACE1 timing and input dump consumed by the planned replay and latency tools.
trace_fmt_dump_free wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The offline tools release the loaded dump and its owned arrays.
trace_fmt_latencies wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The G1 analyzer pairs per-frame timestamps into latency samples.
trace_fmt_load_dump wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The offline tools load a validated dump before replay or measurement.
trace_fmt_pct wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The G1 analyzer reports the p50 and p99 of those samples.
trace_input_clipboard wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves clipboard event metadata for replay.
trace_input_filechange wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves file-change notifications for replay.
trace_input_focus wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves focus transitions for replay.
trace_input_key wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves key input for replay.
trace_input_pointer wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves pointer motion and buttons for replay.
trace_input_resize wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves surface-size changes for replay.
trace_input_wheel wave1-L0-harvest # §3.2 and §7 L0-4. The UI ingress log preserves wheel deltas for replay.
trace_record wave1-L0-harvest # §3.2 and §7 L0-4. The GUI pipeline records G1 timing events at the current monotonic time.
trace_record_at wave1-L0-harvest # §3.2 and §7 L0-4. The GUI pipeline records an event with its original timestamp, including presentation feedback.
trace_replay wave1-L0-harvest # §3.2 and §7 “Out-of-scope / handed-off items”. The replay tool delivers the recorded input log to its sink.
work_cancel wave1-app-spec # app_buffers.h, Acquisition and counting. The buffer owner cancels stale mapped-count work on close or reload.
work_continue wave1-app-spec # app_buffers.h, Acquisition and counting. A count job yields after a chunk and resumes on the same lease.
work_handle_finished L2-1-file # Scope, Close bullet. Two-phase file close polls physical completion before releasing a job's backing.
work_mailbox_drain wave1-app-spec # app_loop.h. The UI routes ordinary mailbox completions once per turn through its bounded dispatcher.
work_mailbox_pending wave1-app-spec # app_loop.h. Before sleeping, the root loop checks remaining mail alongside runnable work and finished handles (also “Runnable work, fairness and wakes”).
work_pool_init wave1-app-spec # app_loop.h. Headless app creation initializes the shared pool for bulk file, journal and count jobs. L0 dossier §4.2 names the basic constructor.
work_pool_init_foreground wave1-L0-harvest # §4.2 and §4.3. The GUI enables the dedicated foreground lane for navigation while bulk work is occupied. §4.3 also names copied-file jumps.
work_pool_shutdown wave1-app-spec # app_loop.h. App destruction retires and joins the owned pool. L0 dossier §4.2 names the consuming operation.
work_pool_wake wave1-app-spec # app_loop.h. Main registers the pool wake descriptor in the root wait set.
work_publish wave1-app-spec # app_buffers.h, Acquisition and counting. A count worker publishes its immutable chunk result to the owning UI.
work_submit wave1-app-spec # app_buffers.h, Acquisition and counting. The mapped-open owner submits a WORK_BULK job with its retained backing.
utf8_artwork_step wave1-L1-harvest # §2.2. The font fallback walks a cluster's scalars to obtain its unshaped artwork projection.
utf8_cluster_artwork wave1-L1-harvest # §2.2. The GUI prepares a bounded artwork key from the bytes of a complete cluster. §2.2 names the whole-cluster projection.
utf8_cluster_emoji v2-0fv.11 # Blast radius, every consumer of gcb_of, audited. Layout classifies the cluster before handing its complete bytes to glyph lookup.
utf8_cluster_step wave1-L1-harvest # §2.2. Wrapped layout segments and measures clusters in bounded increments across input spans.
utf8_emoji_class v2-0fv.11 # Blast radius, every consumer of gcb_of, audited. Font GSUB checks a scalar's emoji class when selecting whole-cluster artwork.
utf8_grapheme_prev wave1-L1-harvest # §2.2. Cursor-left motion finds the previous cluster start on an already bounded span. §2.2 names backward grapheme segmentation.
utf8_grapheme_step wave1-L1-harvest # §2.2. Viewport motion and grapheme repair segment a cluster with a resumable byte budget. The edit spec assigns repair to the caller after the proposed edit.h.
indent_bracket_match v2-0fv.12 # §§2–3. GUI paint locates the bracket at the cursor and its mate in the visible window. §3 identifies paint as the consumer.
indent_detect v2-0fv.12 # §§2–3. The buffer owner detects indentation and line-ending style on open, then supplies it to typing commands.
indent_on_close_brace v2-0fv.12 # §§2–3. The closing-brace command computes its one-stop dedent before submitting the replacement.
indent_on_enter v2-0fv.12 # §§2–3. The Return command copies the leading run and the detected line ending. The W1 edit harvest §4.3 names the typing operation.
indent_trailing_ws v2-0fv.12 # §§2–3. GUI paint obtains trailing-blank ranges for the visible text window. L1 harvest §3.2 names the range query, and the landed §3 gives its current spelling.
find_literal_next wave1-L2-L5-harvest # §4.3, view_skip_next_occurrence. The skip-next-occurrence plan needs the first following literal match at or after an offset. L1 harvest §7.2 names this single-match API.
find_literal_visit wave1-L1-harvest # §§7.2–7.5. Find session and fif stream selected literal matches with native case and word options.
find_regex_bytes wave1-L1-harvest # §§7.2–7.5. The caller funds opaque program storage before compiling a Wave 3 query.
find_regex_captures wave1-L1-harvest # §§7.2–7.5. A replacement transaction obtains capture ranges by re-running the expression anchored at a chosen match.
find_regex_compile_options wave1-L2-L5-harvest # §8, fif row. The fif worker compiles an option-aware regex against the unified find header.
find_regex_next_budget wave1-L1-harvest # §§7.2–7.5. Wave 3 filtered regex enumeration carries the remaining work allowance across successive candidates. §7.3 names the caller-budgeted driver.
find_regex_scratch_bytes v2-0fv.19 # Public contract. The replacement caller sizes the single-call workspace for anchored captures.
find_regex_stream_begin wave1-L1-harvest # §§7.2–7.5. The find worker begins a resumable regex visit with a caller-funded workspace.
find_regex_stream_scratch_bytes v2-0fv.19 # Public contract. The find worker sizes storage for the resumable regex visitor.
find_regex_stream_step wave1-L1-harvest # §§7.2–7.5. The worker resumes that visit at NFA byte boundaries until completion.
find_regex_visit wave1-L1-harvest # §§7.2–7.5. The Wave 3 consumers visit regex matches through the promoted visitor.
find_regex_word_visit wave1-L1-harvest # §§7.2–7.5. Whole-word regex visits perform boundary filtering inside the shared matcher.
find_regex_work_budget wave1-L1-harvest # §§7.2–7.5. The shared request budget bounds successive filtered regex retries. §7.3 names the budget initializer alongside its consuming driver.
config_toml_parse LF-2-toml # The unit in one paragraph and opening scope. Wave 3 settings glue consumes the transactional TOML visitor. See the same-module qualification below.
dnd_parse_uri_list LF-1-dnd # The module in one paragraph. The GUI drop handler decodes a text/uri-list payload into an IPC open request.
```

## Dead list

| Module | Count | Exports |
|---|---:|---|
| base | 4 | `base_arena_reset`, `base_cpu_has_popcnt`, `base_pool_count_live`, `base_version` |
| ipc | 5 | `ipc_client_send`, `ipc_result_string`, `ipc_server_init`, `ipc_wire_decode`, `ipc_wire_encode` |
| trace | 1 | `trace_reset` |
| work | 4 | `work_cancel_time_ns`, `work_mailbox_drain_bounded`, `work_mailbox_receive_bounded`, `work_pool_worker_id` |
| scan | 0 | — |
| utf8 | 5 | `utf8_cell_width_table`, `utf8_cluster`, `utf8_cluster_width`, `utf8_grapheme_next`, `utf8_prev` |
| indent | 0 | — |
| find | 8 | `find_lit_init`, `find_lit_seek`, `find_literal`, `find_regex_compile`, `find_regex_group_count`, `find_regex_next`, `find_regex_prefix`, `find_regex_search` |
| config | 0 | — |
| dnd | 0 | — |
| **Total** | **27** | |

## Advisor decisions

Leave these exports in place pending the named caller decision.

- `base_malloc_guard_exempt_begin` — Does Wave 2 X11 need a post-submit library exemption during another active guard window? The signed edit proof forbids exemptions through submit.
- `base_malloc_guard_exempt_end` — Keep paired with the opening exemption until the X11 owner identifies a concrete library-call interval.
- `work_mailbox_bind` — The app spec allows registered per-handle receivers as an alternative to its ordinary dispatcher. Does any owner require registration?
- `work_mailbox_receive` — The file brief requires dossier §0.1, which directs file to use selective receive. Does file still call it under the app’s ordinary single-dispatcher delivery?
- `work_prioritize` — Wave 2 requires navigation under bulk load. A19 removed the lineidx builder that upgraded an existing lease. Does the GUI still upgrade a live job?
- `work_submit_batch` — Q7 cites batch admission for capacity arithmetic. Single-job submit already uses it internally. Does a Wave 2 raster owner require atomic multi-job admission?
- `utf8_break_certain` — Layout's long-line checkpoints need a valid segmentation restart. Will they test adjacent units for a certain break or retain segmentation state?

## Out of scope and proposed vendor policy

The full scan reports 492 diagnostics. Outside the ten modules it reports `piece` 11, `undo` 17, `journal` 28 and `vendor_ts` 331. The first three modules are under rewrite. The current checker includes the vendored definitions in its diagnostics. [1.22 policy and baseline](v2-0fv.21.md)

Proposed policy line for both `tools/README.md` and `tools/check_exports.py`

> Vendored code (`vendor_ts`) is exempt from the export-usage gate because the project preserves the upstream library’s public surface.

The exemption applies to definitions owned by the vendor pseudo-module. Its references to project-owned exports still count as cross-module uses.
