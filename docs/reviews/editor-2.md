**Review of main `faa7baa`: trial use remains blocked.**

The highest-risk findings are false “clean” state after branching undo history, recovery depending on mutable disk originals, checkpoints treating unfinished opens as empty documents, and eviction of an in-flight save’s session leaf.

Findings below are ordered by trial impact: data loss/recovery, crash or fatal loop termination, hangs, incorrect display/input, then typing lag and remaining specification gaps. Reproduction schedules are source-derived unless stated otherwise.

Validation: strict GCC syntax checking passed for the reviewed modules. Existing release tab, layout, wrap, keymap, mailbox-receive and continuation tests passed. Filesystem-backed checks encountered read-only permission failures; `work_foreground_test` subsequently crashed after continuing past failed fixture setup. No display tests ran. Reviewed source remained identical to `faa7baa` despite concurrent documentation commits.

1. **BLOCKER — A new undo branch can be marked clean and silently reloaded away.**  
   Locations: [src/editor/input.c:174](/home/tobias/Projects/sublimite/src/editor/input.c:174), `src/editor/buffers.c:803`.

   Type `x`, save, undo, then type `y`. `remember()` truncates redo history without invalidating `saved_cursor`; the new branch reaches the saved branch’s numerical position. `editor_modified()` marks it clean, and edit-ret’s automatic reload can discard `y` without asking.

   **Fix:** invalidate the saved marker before truncating a redo branch containing that marker. **Missing regression:** save → undo → different edit → external replacement; assert dirty state and preserved unsaved text.

2. **BLOCKER — Ordinary unsaved recovery still depends on a mutable original file.**  
   Locations: [src/editor/buffers.c:168](/home/tobias/Projects/sublimite/src/editor/buffers.c:168), `src/editor/recovery.c:90`, `src/editor/recovery.c:126`.

   Initial journaling persists BASE identity and edits, without retaining the original bytes. If another process replaces or rewrites that file before a crash, recovery skips the buffer’s edits and opens disk contents clean. The set-aside journal cannot reconstruct the complete document when its original bytes are gone. This is the documented edit-btj compromise, but it still violates PRD §§3.5 and 7.

   **Fix:** establish an immutable recovery original before acknowledging the first edit to a named buffer. **Missing regression:** externally overwrite an unsaved buffer’s original, kill, restart, and compare the entire recovered document.

3. **BLOCKER — Complete checkpoints serialize pending opens as empty documents.**  
   Locations: [src/editor/checkpoint.c:37](/home/tobias/Projects/sublimite/src/editor/checkpoint.c:37), `src/editor/buffers.c:191`, `src/editor/buffers.c:771`.

   Before adoption, `b->tree` is empty and the displayed prefix lives in `b->preview`. Checkpointing includes that buffer, emits DELETE for its complete BASE size, and emits no replacement text. Reloading another tab can therefore durably checkpoint the pending tab as an empty document. Recovery presents empty, modified contents; saving them overwrites the file.

   **Fix:** encode an unadopted buffer as BASE/TARGET/VIEW without synthesizing content replacement. **Missing regression:** checkpoint while another COPY open remains pending, then crash and recover.

4. **MAJOR — Reload preserves byte offsets that can split UTF-8 or CRLF on the next edit.**  
   Locations: [src/editor/buffers.c:754](/home/tobias/Projects/sublimite/src/editor/buffers.c:754), `src/editor/input.c:985`.

   Reload clamps selection endpoints only to EOF. An old ASCII caret at byte one can land inside a replacement multibyte character or CRLF pair. The typing path mutates at that offset before `repair()` runs, corrupting the newly loaded sequence.

   **Fix:** normalize both selection endpoints against the replacement generation before publishing it as editable. **Missing regression:** external ASCII → emoji/combining/CRLF replacement, followed immediately by typing and saving.

5. **MAJOR — The public large-file save helper bypasses journal reconciliation.**  
   Location: [src/editor/large.c:571](/home/tobias/Projects/sublimite/src/editor/large.c:571).

   `editor_large_save_begin()` still calls `file_save_begin()` directly. A journal-enabled caller can replace the BASE inode, make another edit, and leave recovery referring to the removed original. This remains an unsafe editor-facing contract after Ctrl+S gained savectl integration.

   **Fix:** route this entry point through the session save controller. **Missing regression:** invoke the helper with journaling enabled, save, edit again, kill, and recover exact contents.

6. **MAJOR — A mapped buffer’s second save rejects the editor’s own first replacement.**  
   Locations: [src/editor/session.c:268](/home/tobias/Projects/sublimite/src/editor/session.c:268), `src/file/file.c:349`, `src/editor/session.c:380`.

   Savectl retains the original mapping identity, including ctime. Atomic replacement changes the old inode’s ctime through unlink. `file_resolve_keep()` permits that retired mapping, but the retained savectl source validator still requires its old metadata identity. Subsequent saving fails with `FILE_ERR_CHANGED`; rebasing the target identity does not update this source lease.

   **Fix:** refresh the mapped-source authorization after a verified self-save, preserving byte-change and fault detection. **Missing regression:** force mmap, edit/save twice, then repeat after an external write through the old descriptor.

7. **MAJOR — A recoverable save failure disables saving throughout the session.**  
   Location: [src/editor/session.c:354](/home/tobias/Projects/sublimite/src/editor/session.c:354).

   A failure retaining a PREPARED token sets `s->blocked` and leaves `s->saving` populated. Every later save request is refused, including requests for unrelated buffers. The editor exposes neither `savectl_recover_cancel()` nor `savectl_recover_finish()`, so clearing ENOSPC and retrying cannot recover normal saving.

   **Fix:** implement an editor-owned recovery transition that resolves the retained token before releasing the session save slot. **Missing regression:** fail after PREPARED, remove the failure, retry, and save another tab without restarting.

8. **MAJOR — Newly created files never acquire external-change monitoring.**  
   Locations: [src/editor/session.c:373](/home/tobias/Projects/sublimite/src/editor/session.c:373), `src/editor/buffers.c:802`.

   A nonexistent named target starts with `b->file == NULL`. Successful saving updates its BASE and controller baseline, but never installs a file/watch owner. Source checking permanently skips it. External changes receive no reload/banner; a later save detects conflict inside savectl without creating the editor’s usable R/K state.

   **Fix:** install a watched file identity after the first successful creation. **Missing regression:** create/save a new target, externally rewrite it, and exercise clean reload and dirty conflict handling.

9. **MAJOR — Two changed BASEs can make both Reload and Keep impossible.**  
   Locations: [src/editor/checkpoint.c:41](/home/tobias/Projects/sublimite/src/editor/checkpoint.c:41), `src/editor/buffers.c:771`, `src/editor/buffers.c:851`.

   Each source choice checkpoints the entire session and validates every remaining named BASE. If A and B both changed externally, reconciling A fails on B; reconciling B fails on A. A changed retained closed tab can produce the same obstruction. Choices repeatedly roll back without resolving the session.

   **Fix:** reconcile all changed clean generations in one atomic session checkpoint transition. **Missing regression:** simultaneous external changes to two live tabs and to a retained closed tab.

10. **MAJOR — Evicted historical buffers remain recovery inputs indefinitely.**  
    Locations: [src/editor/buffers.c:457](/home/tobias/Projects/sublimite/src/editor/buffers.c:457), `src/editor/recovery.c:234`.

    Eviction changes the live session graph but appends no journal retirement record. Recovery installs every historical BASE absent from TABS as a retained closed buffer. Repeated untitled opens can exceed buffer capacity because their live resources are reclaimed while their journal records accumulate. Installation then fails and edit-btj sets the whole session aside.

    **Fix:** persist explicit buffer retirement and exclude retired IDs during replay. **Missing regression:** cycle substantially more buffers than `tab_capacity + closed_capacity`, then recover the remaining live and retained tabs.

11. **MAJOR — Recovery abandons the large-file memory model.**  
    Locations: [src/editor/recovery.c:102](/home/tobias/Projects/sublimite/src/editor/recovery.c:102), `src/editor/recovery.c:132`, `src/editor/recovery.c:215`.

    Replay materializes complete BASE files into heap buffers, repeatedly reallocates them, forces COPY acquisition, reinserts complete recovered contents, and builds another full checkpoint. Several complete generations can coexist. Restarting a session containing multi-gigabyte logs can exhaust memory and trigger the empty-session retry.

    **Fix:** replay into worker-owned piece trees over immutable recovery originals, preserving mapped acquisition and bounded edit application. **Missing regression:** recover a sparse file above the copy threshold under a realistic memory ceiling, alongside a dirty small tab.

12. **BLOCKER — Evicting the saving tab removes the leaf needed by save completion.**  
    Locations: [src/editor/session.c:157](/home/tobias/Projects/sublimite/src/editor/session.c:157), `src/editor/session.c:302`, `src/editor/session.c:364`.

    After PREPARE handoff, tab controls are allowed while file writing continues. Close the saving tab and enough other tabs to evict it from retained history. Metadata forgets its leaf even though buffer destruction is deferred. Finish capture dereferences `leaf_at(...)->snapshot` without checking for NULL.

    **Fix:** retain the saving buffer’s session leaf through finish/token retirement, independently of tab retention. **Missing regression:** hold file writing after handoff, evict its tab, release writing, and finish successfully.

13. **MAJOR — Wheel/page input during a pending byte jump terminates the editor loop.**  
    Locations: [src/editor/input.c:1053](/home/tobias/Projects/sublimite/src/editor/input.c:1053), `src/editor/input.c:1004`, `src/scroll/scroll.c:94`.

    Find/minimap navigation sets `b->scroll.request = SCROLL_BYTE`. Wheel handling clears host flags but does not cancel that request. `scroll_wheel()` and page/home/end dispatch reject it; the error becomes sticky editor failure.

    **Fix:** cancel the pending byte request into the displayed READY state before applying new navigation. **Missing regression:** hold resident acquisition, start a far jump, then wheel and press Page Down.

14. **MAJOR — A late mapped-source fault can become a global fatal error.**  
    Locations: [src/scroll/resident.c:133](/home/tobias/Projects/sublimite/src/scroll/resident.c:133), `src/editor/scrollctl.c:293`, `src/editor/editor.c:696`.

    A backing fault arriving after `editor_check_sources()` but before a resident slice returns `SCROLL_ERR_SOURCE`. Scrollctl propagates it into `fail()`, stopping all tabs instead of suspending the affected source. The module’s viewport-preserving error behavior is not sufficient at the editor boundary.

    **Fix:** translate an authenticated backing-change error into owner-local source suspension. **Missing regression:** inject the fault between source checking and scroll resolution while another tab remains editable.

15. **MAJOR — Retired file buffers permanently consume buffer slots and resources.**  
    Location: [src/editor/buffers.c:124](/home/tobias/Projects/sublimite/src/editor/buffers.c:124).

    `b->file` alone prevents physical retirement forever. Every reload consumes another slot; closed named files eventually do too. At the default 144 slots, further opens and reloads become impossible even with very few live tabs. File descriptors, trees, undo storage and arenas also remain retained.

    **Fix:** add a nonblocking file retirement/readiness contract and reclaim completed retired owners. **Missing regression:** perform more than 144 reloads/open-close cycles while checking reusable slots and resource counts.

16. **MAJOR — Open, launch-coordinate and byte-jump paths wait on the UI thread.**  
    Locations: [src/editor/buffers.c:148](/home/tobias/Projects/sublimite/src/editor/buffers.c:148), `src/editor/buffers.c:492`, `src/editor/large.c:447`.

    These paths poll or sleep until worker publication/physical completion, without pumping native input. IPC admission executes them inside the running editor’s event loop. Slow storage or an occupied bulk lane can freeze the existing window, including unrelated tabs.

    **Fix:** represent acquisition and coordinate seeking as persistent, mailbox-driven editor continuations. **Missing regression:** hold each worker stage and verify native/control input remains serviceable.

17. **MAJOR — Automatic reload and Keep synchronously checkpoint every buffer on UI.**  
    Locations: [src/editor/buffers.c:739](/home/tobias/Projects/sublimite/src/editor/buffers.c:739), `src/editor/buffers.c:771`, `src/editor/checkpoint.c:56`.

    Automatic reload waits for replacement acquisition, flushes the journal, allocates checkpoint records, reads complete trees, and waits for rotation durability. One agent-written file can stall the editor for the size and I/O cost of the entire session. Keep takes the same checkpoint path.

    **Fix:** prepare and publish source-reconciliation checkpoints on a worker, with a short UI installation fence. **Missing regression:** reload a small file while another large buffer and delayed journal I/O are present.

18. **MAJOR — Saving a small file can freeze input for the complete session’s size.**  
    Locations: [src/editor/session.c:189](/home/tobias/Projects/sublimite/src/editor/session.c:189), `src/savectl/savectl.c:350`, `src/editor/editor.c:588`.

    Save preparation copies every buffer into checkpoint records, including unchanged large logs. The journal is leased throughout preparation and publication; `queue_pick()` parks all input during that lease. Worker execution avoids a direct UI syscall but still leaves typing and tab controls unavailable for potentially lengthy preparation.

    **Fix:** build the new checkpoint generation while a separate protected delta stream continues serving UI edits, then merge at a mailbox fence. **Missing regression:** save a tiny dirty file with a large unchanged log open and preparation held.

19. **MAJOR — Close runs unbounded save/durability waits and busy loops on UI.**  
    Locations: [src/editor/editor.c:853](/home/tobias/Projects/sublimite/src/editor/editor.c:853), `src/editor/session.c:442`, `src/editor/open.c:18`.

    WM close enters blocking flush/teardown. An admitted save stalled in I/O prevents completion; `sched_yield()` loops consume CPU without processing window events. Later teardown also waits for physical worker retirement.

    **Fix:** implement a closing state that pumps events and retains owners until asynchronous durability and retirement acknowledgements arrive. **Missing regression:** close with save, journal and discovery stages independently held.

20. **MAJOR — Every protected edit still performs blocking regular-file `pwrite()` on UI.**  
    Location: [src/journal/journal.c:505](/home/tobias/Projects/sublimite/src/journal/journal.c:505).

    Bounded bytes and one syscall attempt do not bound regular-file write latency. Storage congestion, page allocation or writeback can block input→mutation→submit. This limitation is accurately documented in `journal.h`, but remains incompatible with the requested no-blocking typing path.

    **Fix:** move journal cache writes into an independent persistence service and publish protected-prefix fences through its mailbox. **Missing regression:** hold the actual append operation and verify UI execution never waits inside it.

21. **MAJOR — Selection deletion and undo/redo still scan unbounded text synchronously.**  
    Locations: [src/editor/input.c:20](/home/tobias/Projects/sublimite/src/editor/input.c:20), `src/editor/input.c:320`, `src/editor/input.c:631`, `src/editor/input.c:614`.

    `newlines()` walks every selected/replayed byte before or after mutation. Ctrl+A followed by Delete or typing over a large selection can scan gigabytes in one UI call. Sliced undo execution is followed by another whole-range newline scan.

    **Fix:** carry newline counts through a resumable mutation plan using indexed/snapshot metadata. **Missing regression:** large selection replacement and replay with charged byte-work limits per UI turn.

22. **MAJOR — Typing can wait on worker-held ownership locks.**  
    Locations: [src/editor/session.c:111](/home/tobias/Projects/sublimite/src/editor/session.c:111), `src/editor/session.c:258`, `src/editor/buffers.c:18`, `src/editor/clip.c:60`.

    Mutation publication takes the same session mutex used by worker-side root release. That release recursively releases snapshots while holding the mutex. Piece and clipboard recycling also use blocking mutexes shared with workers. Zero malloc counts do not prove absence of these waits.

    **Fix:** detach retirement work from UI publication and transfer reclaimed storage through nonblocking ownership queues. **Missing regression:** pause worker reclamation while admitting and submitting typing.

23. **MAJOR — Raw mapped reads remain on layout/paint/submit paths.**  
    Locations: [src/editor/editor.c:94](/home/tobias/Projects/sublimite/src/editor/editor.c:94), `src/editor/paint.c:48`, `src/editor/paint.c:234`.

    The resident scroll bridge does not protect subsequent layout/decorations. Those paths read the raw tree, and the first file-backed minimap is explicitly prepared synchronously in composition. A 64 KiB byte bound cannot prevent a disk page fault from blocking UI.

    **Fix:** supply layout, decorations and initial minimap preparation from worker-published resident bytes. **Missing regression:** cold mapped viewport/minimap reads with faulting acquisition held off UI.

24. **MAJOR — Font selection performs blocking discovery/file reads on the foreground lane.**  
    Locations: [src/editor/font_size.c:217](/home/tobias/Projects/sublimite/src/editor/font_size.c:217), `src/font/selection.c:43`, `src/font/selection.c:50`.

    The foreground font job calls fontconfig and opens/reads the selected font. A FIFO path blocks before the regular-file check. Slow discovery or font I/O monopolizes the lane reserved for bounded CPU work, delaying foreground minimap/index requests.

    **Fix:** perform discovery and file acquisition on a bulk/discovery service, handing resident font bytes to foreground preparation. **Missing regression:** FIFO and delayed font acquisition while foreground CPU jobs continue.

25. **MAJOR — Pointer hits can use a future layout instead of the displayed frame.**  
    Locations: [src/editor/editor.c:597](/home/tobias/Projects/sublimite/src/editor/editor.c:597), `src/editor/input.c:1092`, `src/editor/input.c:1107`.

    Once layout is prepared, pointer dispatch is allowed even when the backend still displays an older frame. It uses current `hits`, `row_byte` and `scroll_plan`, which may describe an unsubmitted edit/reflow. Clicking the visible old text can therefore select a different location.

    **Fix:** retain hit metadata with displayed-frame identity and map its positions through subsequent edits. **Missing regression:** hold presentation, reflow text, then click a location in the still-displayed frame.

26. **MAJOR — Middle-click paste bypasses the new pointer and find-panel contracts.**  
    Location: [src/editor/clip.c:439](/home/tobias/Projects/sublimite/src/editor/clip.c:439).

    `middle_target()` uses integer rows without the Q8 inverse transform and admits `text_grid.dims.rows`, including overscan. With an unwrapped find panel, its first row can be interpreted as the document’s overscan row; PRIMARY paste mutates document text from a panel click. Fractional scroll also selects the wrong row.

    **Fix:** route middle-click through the shared displayed-frame hit path and explicit panel-field dispatch. **Missing regression:** middle-click in find fields, fractional text rows and the visible overscan fragment.

27. **MAJOR — Far byte seeks can publish the wrong logical line start.**  
    Location: [src/scroll/index.c:58](/home/tobias/Projects/sublimite/src/scroll/index.c:58).

    SCROLL_BYTE uses a coarse backward window. If no newline exists in that window, `align_start()` retains an earlier viewport start or zero, which may belong to a different logical line. Find/minimap seeks can jump to that earlier line; edit-agh’s certified placement only protects its explicit byte-jump path.

    **Fix:** obtain a certified target-line start before adopting SCROLL_BYTE, continuing backward acquisition when necessary. **Missing regression:** seek into a long line following an earlier short line, with its start outside the 64 KiB probe.

28. **MAJOR — Config wrap changes do not rebuild overscan geometry.**  
    Locations: [src/editor/preferences.c:65](/home/tobias/Projects/sublimite/src/editor/preferences.c:65), `src/editor/editor.c:480`, `src/editor/paint.c:155`.

    Hot reload changes wrap mode and scroll generation but does not call `editor_text_geometry()`. Switching wrapped→unwrapped leaves no laid-out overscan row; composition nevertheless copies it. Fractional scrolling can display stale bottom-row cells. The opposite transition retains the extra row in wrapped layout.

    **Fix:** rebuild text geometry through the shared helper whenever wrap mode changes. **Missing regression:** config wrap on→off→on with fractional scrolling and assertions on overscan contents.

29. **MAJOR — Wrapped scrolling and scrolling with find open discard fractional movement.**  
    Locations: [src/editor/input.c:1039](/home/tobias/Projects/sublimite/src/editor/input.c:1039), `src/editor/paint.c:152`.

    Wrapped input steps whole visual rows; accumulated subrow movement is never drawn. Opening find disables the renderer’s fractional origin for unwrapped text too. Prose and find-visible scrolling therefore jump at row boundaries, contrary to PRD §6.5. These are documented integration compromises, still present on main.

    **Fix:** integrate visual-row scroll plans with a renderer text rectangle ending above panel chrome. **Missing regression:** fractional wheel sequences in both wrap modes, with find open and closed.

30. **MAJOR — Paste converts CRLF to LF regardless of the buffer’s line-ending style.**  
    Location: [src/editor/clip.c:106](/home/tobias/Projects/sublimite/src/editor/clip.c:106).

    Copying/cutting a CRLF line and pasting it back removes every CR. A consistently CRLF file becomes mixed or LF after ordinary clipboard editing, despite Enter preserving CRLF. This violates PRD §6.2’s preservation requirement.

    **Fix:** normalize pasted line separators to the buffer’s detected dominant style. **Missing regression:** CRLF current-line copy/cut/paste, undo/redo and byte-exact save output.

31. **MAJOR — More than 512 viewport matches removes all find highlighting.**  
    Locations: [src/editor/findpanel.c:28](/home/tobias/Projects/sublimite/src/editor/findpanel.c:28), `src/findui/findui.c:452`, `src/editor/findpanel.c:167`.

    Visible capacity is `LAYOUT_CLUSTER_CACHE`, currently 512. Overflow returns LIMIT, and the host clears `find_range_count` entirely. A dense ordinary viewport can show no highlights even though matching is complete and many matches are visible.

    **Fix:** publish compressed visible match runs/masks rather than discarding the entire overlay on capacity overflow. **Missing regression:** more than 512 matches visible simultaneously.

32. **MAJOR — Find focus swallows Save, Close, tab controls and configured bindings.**  
    Locations: [src/editor/findpanel.c:293](/home/tobias/Projects/sublimite/src/editor/findpanel.c:293), `src/editor/findpanel.c:325`, `src/editor/input.c:882`.

    Find dispatch precedes configured lookup and consumes unsupported Ctrl/Super/Alt chords. Ctrl+S, Ctrl+W and Ctrl+Tab silently do nothing while find is visible. Remapped Find/F3/Paste bindings cannot control the panel. Edit-m15’s isolation fixed document paste but replaced edit-73j’s global-chord pass-through.

    **Fix:** resolve configured actions with explicit panel/document contexts, allowing document/window controls through. **Missing regression:** configured find/navigation/paste bindings plus Save/Close/tab switching while each field is focused.

33. **MAJOR — Untitled buffers have no Save As path.**  
    Locations: [src/editor/session.c:263](/home/tobias/Projects/sublimite/src/editor/session.c:263), `src/editor/session.c:310`, `src/keys/keys.c:156`.

    Untitled and stdin buffers never receive a save controller. Ctrl+S produces a failure state; Ctrl+Shift+S is consumed as unimplemented. The owner cannot persist newly composed scratch/prose text to a chosen file through the editor.

    **Fix:** add an editor-owned filename prompt that establishes TARGET/controller state and enters the normal save transaction. **Missing regression:** untitled/stdin Save As, subsequent save, conflict and crash recovery.

34. **MAJOR — Unicode cache exhaustion replaces valid text with `#`.**  
    Location: [src/font/async.c:336](/home/tobias/Projects/sublimite/src/font/async.c:336).

    The append-only owner has approximately 4,000 non-ASCII slots. Exhaustion or bounded-probe refusal returns the baked `#` glyph. Scrolling a document with many distinct CJK/combining clusters can permanently render supported characters as misleading text for that owner’s lifetime.

    **Fix:** rebuild a bounded visible-glyph cache generation on a worker and adopt it after frame retirement. **Missing regression:** exceed cache capacity with supported distinct clusters, scroll back and forth, and verify actual glyphs recover.

35. **MAJOR — CPU-only resident navigation still waits behind bulk I/O.**  
    Location: [src/scroll/resident.c:119](/home/tobias/Projects/sublimite/src/scroll/resident.c:119).

    Every resident-window request uses WORK_BULK, including nonfaultable memory buffers. A long save/open operation can delay find/minimap/page navigation in another tab. Submission waits for `scroll_pending`, so the containing frame also waits. The dedicated foreground lane is unused here.

    **Fix:** admit nonfaultable resident copies on WORK_FOREGROUND through an explicit source-class contract. **Missing regression:** hold unrelated bulk I/O and complete memory-buffer navigation.

36. **MAJOR — Ordinary typing repaints and hashes the whole minimap.**  
    Locations: [src/editor/paint.c:261](/home/tobias/Projects/sublimite/src/editor/paint.c:261), `src/editor/editor.c:421`.

    Normal edit composition regenerates every minimap pixel, compares the complete pixel region, and submission hashes that region again. This occurs even when the retained summary, viewport and colours are unchanged; the hash is computed without requiring an observation hook. Cost scales with window pixel area on every typing frame.

    **Fix:** cache minimap pixels and identity by summary/viewport/geometry/style generation. **Missing regression:** unchanged minimap during typing with pixel-work counters at high-DPI geometry.

37. **MAJOR — Recoverable capacity refusal stops subsequent editing globally.**  
    Locations: [src/editor/input.c:308](/home/tobias/Projects/sublimite/src/editor/input.c:308), `src/editor/editor.c:751`.

    Index admission refusal increments `rejected_commands` but returns `EDITOR_ERR_CAPACITY`, which becomes sticky editor failure. Other mutation allocation failures take the same global path. A refused command with unchanged text can terminate the entire session rather than leave it usable.

    **Fix:** distinguish unchanged admission refusal from a committed-prefix failure and report the former locally. **Missing regression:** refuse one oversized command, then type, save and switch another tab successfully.

38. **MINOR — Relative TARGETs are unsafe through the public editor APIs.**  
    Locations: [src/editor/buffers.c:143](/home/tobias/Projects/sublimite/src/editor/buffers.c:143), `src/editor/buffers.c:369`, `src/editor/recovery.c:121`.

    CLI arguments are canonicalized, but `editor_config.path` and `editor_add_buffer()` accept relative paths and persist them as TARGET. Recovery from another cwd can associate recovered text with a different file, potentially overwriting it on save.

    **Fix:** canonicalize intended targets at the editor API boundary, including existing parents of missing files. **Missing regression:** public relative-path open followed by recovery from another cwd.

39. **MINOR — Common daily-use commands remain consumed placeholders.**  
    Locations: [src/keys/keys.c:95](/home/tobias/Projects/sublimite/src/keys/keys.c:95), `src/keys/keys.c:11`, `src/editor/input.c:932`.

    Ctrl+N, Ctrl+O, Ctrl+Q, ordinary Escape and Ctrl+Up/Down still lack executors. Escape does not provide normal selection cancellation. Binding-row tests verify recognition and intentional refusal, not Sublime behavior.

    **Fix:** implement editor executors for these existing actions and dispatch them through configured lookup. **Missing regression:** action outcomes for new/open/exit/cancel/viewport scrolling.

40. **MINOR — Undo is bounded despite the unlimited-session requirement.**  
    Location: [src/editor/buffers.c:205](/home/tobias/Projects/sublimite/src/editor/buffers.c:205).

    Default history capacity is 32,768, with fixed editor history arrays and undo-record reservation. Older history is evicted. This remains a PRD §6.3 deviation, independent of the new soft-history functionality.

    **Fix:** grow history in worker-prepared chunks before current reservations are exhausted. **Missing regression:** exceed the default retained history while preserving undo and saved-state identity.

41. **MINOR — `font.family` is parsed but has no editor consumer.**  
    Locations: [src/editor/preferences.c:39](/home/tobias/Projects/sublimite/src/editor/preferences.c:39), `src/editor/open.c:260`.

    Startup and hot reload consume font size; family selection only uses environment variables or the explicit API. Editing the advertised TOML family setting changes reported preferences but leaves the font unchanged. The existing config test asserts the stored string without verifying adoption.

    **Fix:** issue `editor_font_request()` when the effective configured family changes. **Missing regression:** startup, hot reload and deletion of `font.family`, checked against adopted atlas generation.

42. **MINOR — Valid Unicode find/replacement text is displayed as byte escapes.**  
    Location: [src/findui/paint.c:60](/home/tobias/Projects/sublimite/src/findui/paint.c:60).

    Every non-ASCII byte is rendered as `\xNN`, including valid UTF-8. Grapheme-aware editing therefore produces a byte-oriented display and cursor placement. Searching prose such as `café` displays escape sequences instead of the entered text.

    **Fix:** render valid field graphemes through the shared async glyph contract, retaining escapes for invalid/control bytes. **Missing regression:** combining, CJK and emoji field text with matching cursor geometry.

43. **MINOR — Colour-bitmap-only emoji remain missing after edit-r8e.**  
    Location: [src/font/fallback.c:98](/home/tobias/Projects/sublimite/src/font/fallback.c:98).

    Fallback discovery excludes colour faces, while the landed artwork projection only fixes document cluster assignment. Characters whose installed coverage exists solely in CBDT fonts still become boxes. This is the explicitly recorded r8e remainder and a full-Unicode coverage gap.

    **Fix:** support monochrome extraction from supported bitmap emoji faces and admit those faces during fallback discovery. **Missing regression:** a fixture whose only covering face is bitmap-based.

44. **MINOR — Initial find navigation starts at document match zero.**  
    Locations: [src/findui/findui.c:341](/home/tobias/Projects/sublimite/src/findui/findui.c:341), `src/editor/findpanel.c:147`.

    Query/source restart always requests ordinal zero. The first forward navigation selects that result regardless of the current caret; it can jump to the beginning instead of the next occurrence near the user’s working position.

    **Fix:** seed initial selection from the current caret’s byte position in the completed match plan. **Missing regression:** begin find midway through repeated matches and verify forward/reverse starting behavior.

45. **MINOR — Syntax highlighting workers are not connected to editor rendering.**  
    Locations: [src/editor/editor.c:236](/home/tobias/Projects/sublimite/src/editor/editor.c:236), `src/editor/paint.c:6`.

    Editor work routing and painting contain no highlighting integration. Text remains plain after idle, despite the highlighting modules and PRD §6.6’s progressive worker overlay.

    **Fix:** attach revision-tagged per-buffer highlighting requests and adopt their viewport spans into paint. **Missing regression:** idle highlighting arrival, stale-generation rejection and typing during parsing.

46. **MINOR — Trailing-whitespace display has no toggle.**  
    Location: [src/editor/paint.c:85](/home/tobias/Projects/sublimite/src/editor/paint.c:85).

    Every decoration preparation scans trailing whitespace and paints its error colours. No editor preference or action controls it, although PRD §6.3 requires a display toggle.

    **Fix:** add an applied preference controlling both whitespace scanning and painting. **Missing regression:** hot-toggle it with existing whitespace visible and verify overlay removal without text mutation.