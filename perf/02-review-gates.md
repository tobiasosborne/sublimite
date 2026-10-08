**No. The current ship gates are not safe to build against.** They permit queueing delays that users will feel, omit several important operations, and use unmeasured display and storage estimates to justify exceptions to perceptual ceilings.

I read both files in full and recomputed every arithmetic result reported below with Python. No workspace files were modified. Locations refer to `01-perf-target.md` unless explicitly identified otherwise. Proposed gate values below are **acceptance budgets**, not predictions of hardware percentiles.

**Findings**

**1. BLOCKER — §0.2 G1, notation, and §2: starting the clock after dequeue hides editor-caused input delay.**

- **Claim:** App-side typing p99 ≤2 ms establishes responsiveness, including during background work.
- **Counter-example:** The definition excludes time waiting in the app’s event queue. Under the document’s proposed UI-thread CPU rendering architecture, A performs a full redraw in `2 × 20,736,000 / 9.4e9 = 4.412 ms`. For uniformly phased key arrivals during continuous 90 Hz redraw, the modeled p99 residual wait is `4.412 − 0.01 × 11.111 = 4.301 ms`. Adding the stated 0.3 ms edit path gives **4.601 ms**, while the existing G1 still passes. The analogous B result is **2.403 ms**.
- **Fix:** Timestamp input before the app can queue it, and measure through rendering and submission of the corresponding update. Keep the **2 ms p99** gate, add **0.5 ms p50**, and require bounded UI-thread work slices. A’s full CPU raster must move to workers, use interruptible bands, or use a faster rendering path. A low dequeue-to-submit number does not satisfy the revised gate.

**2. BLOCKER — §0.2 G2, §0.3, and §1: the end-to-end gate abandons tails and treats an estimated pipeline as an unavoidable floor.**

- **Claim:** Mean gates of 29/75/70 ms are justified by a 1.1× hardware-chain allowance; 13 ms mean /25 ms p99 is physically unreachable.
- **Recomputation:** Using A’s listed stages and exact `T = 1000/90`:
  
  `3.25 + 0.125 + 0.55 + 0.3 + T/2 + T + T/2 + 0.55 = 26.997 ms`.
  
  The stated **26.4 ms** is incorrect. Its 1.1× result is **29.697 ms**, approximately **30 ms**. The same modeled compositor-free means become **15.886 ms** with vsync and **10.331 ms** with tearing.
- **Counter-argument:** Midpoints of estimated ranges are not measured means. Nor does a presumed extra compositor frame establish a physical impossibility. Windows flip presentation can use independent flip or hardware overlays; presentation mode is configuration-dependent. [Microsoft’s flip-model documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).
- **Slack problem:** The 10% allowance gives B **6.83 ms** of additional mean delay. That could accommodate an additional 16.667 ms frame on approximately **41%** of inputs. On A, an additional **200 ms** delay on **1.1%** of inputs adds only **2.2 ms** to the mean and fits the modeled allowance. G1’s current endpoint does not reliably catch presentation queueing.
- **Fix:** Replace the mean gate with **p50 and p99 optical gates**, plus a measured minimal-renderer comparison. Require incremental optical latency ≤**1 ms p50 /3 ms p99** over that reference. Use approximately **30/50 ms on A** and **75/100 ms on the specified B display** as active-typing acceptance caps. Stop deriving the allowance from estimated WordPerfect performance.

**3. MAJOR — Target archetypes and assumptions 3–8: “worst-case binding” is not implemented consistently.**

- **Claim:** Pessimistic A values and the worst commodity B configuration bind every gate.
- **Counter-examples:**
  - A’s estimated PSR exit is explicitly omitted. Adding one frame changes its modeled mean from **27.0 to 38.1 ms**, beyond G2’s 29 ms.
  - B selects a one-frame scaler despite specifying a range extending to three frames. Three frames add **33.333 ms**, moving the modeled mean from **68.3 to 101.6 ms**, beyond its 75 ms gate.
  - Defender/EDR adds an estimated **10–500 ms**, but is excluded from startup.
  - B’s 1440p CPU redraw is **4.213 ms**, leaving only **1.116×** margin against the existing 4.7 ms gate.
- **Fix:** Define a finite support envelope and instantiate B: exact SSD, monitor, keyboard, OS build, security configuration, and power policy. Test A unplugged with its actual powersave configuration and PSR enabled. Test A and B independently; require both to pass. Neither machine is universally worse: A binds pixel work and the measured pointer-chase model; B binds storage and its specified display/input chain.
- **Scope fix:** C is an additional, undefined product commitment. Remove it from mandatory release gates until its SKU, OS, display, and rendering configuration are specified. It should not silently determine A/B architecture or budgets.

**4. BLOCKER — §3 /G4: “first interactive frame on glass” omits display stages, and C’s cold gate contradicts its own model.**

- **Claim:** Warm/cold bounds are A 23.5/25.8, B 34.0/42.6, C 49.5/78.1 ms.
- **Recomputation:** Before presentation, the listed steps sum to **12.342/17.370/32.798 ms**. Under §1’s own model, mean mid-screen presentation adds `2T + scaler + panel`. The resulting warm means are:
  
  | Target | Warm modeled mean on glass | Cold modeled mean on glass |
  |---|---:|---:|
  | A | 35.114 ms | 37.514 ms |
  | B | 77.370 ms | 86.020 ms |
  | C | 87.798 ms | 116.631 ms |
  
  Cold additions here use rounded-up request counts and actual payload transfer sizes. Thus B’s current **68/85 ms** gates sit below these modeled means. C’s **100 ms cold cap** also sits below its modeled mean.
- **Further contradiction:** The stated C binary allowance is approximately **0.699 MB in 10 ms**, but one megabyte touched at startup takes **14.667 ms** under the same serial-read model.
- **Fix:** Specify launcher timestamp → readable, input-responsive content on glass. Include phase, scanout, scaler, panel, and the actual desktop mapping behavior. Use **A 40/60 ms warm and 50/80 ms cold**; **B 85/120 ms warm and 100/150 ms cold** as p50/p99 budgets. These need measurement before feasibility is asserted.

**5. BLOCKER — §4 /G5: eager copy mode contradicts size-independent first text.**

- **Claim:** Open any file → first text costs one ≤64 KiB read plus redraw, while files below 256 MB are copied.
- **Recomputation:** Eagerly copying a 100 MB file takes at least the modeled transfer time:
  
  | Path | A | B |
  |---|---:|---:|
  | Warm cached read into private buffer | 23.810 ms | 40.000 ms |
  | Cold device transfer | 40.000 ms | 200.000 ms |
  
  These exceed **9/5.3 ms** before rendering.
- **Fix:** Publish the first viewport before completing the copy. Map initially, or read the first window and finish copying asynchronously. A first-text gate must not secretly wait for whole-file decoding, indexing, encoding detection, or copy completion.
- **Scope fix:** Define “independent of S” as bounded initial work for supported local files. It does not guarantee that 64 KiB contains a screenful of lines, that a requested line number has a known byte offset, or that an entire enormous wrapped line can be laid out immediately.

**6. MAJOR — §2 /G3: the redraw gate is tight on A, but does not establish smooth scrolling.**

- **Claim:** CPU raster fits half a frame everywhere; therefore GPU rendering is not forced.
- **Recomputation:** A’s stated CPU path is **4.412 ms** against an exact half-frame budget of **5.556 ms**: **1.259× slack**, not 2×. Under its own 400 MHz compute scenario, `5,184,000 × 0.5 / 400e6 = 6.480 ms` before upload, exceeding that budget.
- **Counter-argument:** The CPU fits the modeled sustained bandwidth case. This does not establish first-update behavior after idle, shared-memory contention, uncached shaping, glyph misses, or completion before the compositor’s deadline. A submission timestamp can also look fast while GPU work remains queued.
- **Fix:** Gate complete frame preparation, actual displayed cadence, and input queueing separately. Use **A 5/5.5 ms** and **B 4/5 ms** p50/p99 at their specified resolutions. Test actual content with highlighting, selections, gutter, and minimap enabled. Require **zero missed demanded refreshes in a defined 10,000-refresh run**; do not present finite testing as a universal zero-drop guarantee.

**7. MAJOR — Archetype read rates, §4, §5, §7, assumption 1: 9.4 GB/s is not a measured standalone scan rate.**

- **Claim:** A’s binding ST SIMD-read rate is 9.4 GB/s, obtained from the read half of memcpy.
- **Recomputation:** The actual measured standalone ST read is **6.3 GB/s**. One gigabyte therefore takes **158.730 ms**, versus **106.383 ms** at the inferred rate: a **49.2% increase**. The indexed 64 KiB scan becomes **10.403 µs**; with 18 pessimistic tree misses, lookup becomes approximately **17.081 µs**, rather than 14 µs.
- **Fix:** Until the relevant scan is measured, use 6.3 GB/s for standalone ST scan derivations. Preserve measured 23.2 GB/s for the corresponding MT read model, while validating the actual search/index implementation. The acknowledged follow-up must precede labeling an inferred rate binding.
- **Important distinction:** Dividing r+w memcpy throughput by two is correct for **copy payload throughput**. The error is transferring that result to a different operation as measured evidence.

**8. MAJOR — §6, §9, and highlighting addendum: the editing gate covers a narrow micro-operation, not the shipped editor.**

- **Claim:** A 50 µs piece-tree insert, one-line layout, and partial redraw characterize editing performance; highlighting is outside the gates.
- **Counter-argument:** Users also paste, delete selections, undo transactions, enter Unicode through IMEs, type into long lines, and edit while syntax state propagates. Piece lookup does not include allocation, splits, history retention, layout invalidation, glyph discovery, or reclaiming deleted structures.
- **Recomputation:** The pessimistic 10k-undo mutation costs are correctly **37.1/15.0/18.0 ms**. Including the promised full redraw makes them **41.512/17.370/20.798 ms**. Twice those complete bounds is **83.024/34.740/41.595 ms**, not the summary’s 74/30/36 ms.
- **Fix:** Put shipped highlighting and normal Unicode rendering inside the interaction gates. Add separate bulk-edit and transaction-undo gates. Exercise 100k scattered edits, a 10 GB single-line file, dense short lines, malformed UTF-8, and large selection deletion. Require bounded foreground invalidation and deferred reclamation.

**9. MAJOR — §7 /G6: “last result” has an unspecified and potentially enormous output cost.**

- **Claim:** Any literal find is DRAM-bound and completes at twice the all-core scan bound.
- **Counter-example:** Searching one gigabyte of `a` for `a` produces approximately one billion matches. Eight-byte offsets alone require **8 GB**. That exceeds B’s stated 4 GB editor budget and is incompatible with treating result construction as negligible.
- **Further gap:** A first-byte filter followed by naive verification does not guarantee ≥8 B/cycle for every literal and corpus. Searching fragmented pieces also differs from scanning one contiguous array.
- **Fix:** Define completion as **correct total count plus a bounded result page**, for example the first 4,096 offsets; retrieve subsequent pages lazily. Include absent, frequent, repetitive, boundary-crossing, and fragmented-buffer cases. Add **1/5 ms p50/p99 cancellation acknowledgment** and reject stale result generations.
- **Slack fix:** A roughly 2× p99 scan allowance is reasonable; it should not also be the median allowance. Use **65/90 ms on A** and **85/115 ms on B** for the defined warm 1 GB search workload. Require typing responsiveness concurrently. Concurrent index, find, and save cannot each be promised uncontended all-core throughput.

**10. BLOCKER — §0.2 G7 and §12: the per-file memory gate omits edit state and uses an ambiguous accounting metric.**

- **Claim:** An mmapped file uses ≤`0.1% × S + 1 MB` private memory.
- **Recomputation:** At 10 GB this permits **11 MB**. Using the document’s 100k-edit scenario:
  
  `index 2.441408 MB + 200k pieces ×48 B + 100k undo ×32 B +100k typed bytes = 15.341408 MB`.
  
  That already violates the gate before allocator or tree overhead.
- **Measurement problem:** Linux `Private_Clean` can include clean source-file pages mapped only by this process. It is not equivalent to editor-owned anonymous memory or Windows private commit. The kernel explicitly describes this distinction. [Linux memory-accounting documentation](https://docs.kernel.org/filesystems/proc.html).
- **Peak problem:** If CPU and GPU double-buffered paths coexist during handoff, the document’s buffers, fixed 11 MB, and three-height minimap cache total **99.128 MB on A /45.733 MB on B**. Both exceed G7.
- **Fix:** Separate baseline, unedited-file metadata, retained edit/history state, clean source residency, and graphics allocations. Count owned shared buffers and attributable graphics memory so moving allocations to Xorg or a driver does not evade the gate. Measure peaks during startup and backend handoff.
- **Suggested caps:** Baseline **80/42 MB**; unedited mmap metadata `≤32 × ceil(S/65536) + 2 MB`; edit/history increment `≤96P +64U +1.25E` bytes, with retained structural records included in P. The specified 100k-edit fixture then permits **25.725 MB**, rounded to **26 MB**, above its unedited footprint.

**11. MAJOR — §8 and the ship-gate table: save is omitted, and its Windows durability endpoint is unresolved.**

- **Claim:** Save equals `max(copy, write) + two flushes`; Windows replacement with write-through provides the corresponding path.
- **Counter-argument:** The table’s arithmetic is correct for the assumed resident-source model. It omits cold original reads and fragmented source access. A serial cold-source 1 GB read/write schedule would take **1.404 s on A /4.520 s on B**, including the assumed flushes. Pipelining may improve this; the source read cannot simply disappear.
- **Tail problem:** The document acknowledges approximately 10× flush variation. Two 20 ms A flushes plus the modeled 1 MB write take **41 ms**, exceeding its 10 ms target.
- **API problem:** `REPLACEFILE_WRITE_THROUGH` is unsupported. `MOVEFILE_WRITE_THROUGH` documents flushing a copy-and-delete move; it does not establish equivalence to the stated Unix data-flush/rename/directory-flush protocol. [ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew), [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).
- **Fix:** Add separate save acknowledgment and completed-save gates. Define and validate the platform-specific atomicity/durability contract, including mapped originals and outstanding snapshots. Display “saved” only at the defined completion endpoint.

**12. MAJOR — §5, §10, §11: tabs, cold navigation, and minimap usability disappear behind redraw arithmetic.**

- **Claim:** Tab switch equals full redraw; indexed navigation adds approximately 14 µs; minimap refresh is a small bandwidth operation.
- **Counter-argument:** These estimates assume resident source data, available glyphs/layout, and usable index state. An inactive tab can have an evicted viewport. Minimap source access and token/layout work are not bitmap-fill bandwidth. A smooth main viewport with a stale or blank sidebar is not a complete pass.
- **Fix:** Test switching among **100 tabs**, including 10 GB files and evicted viewports. Add cold viewport acquisition and unindexed line-jump completion gates. Include populated sidebar toggling/resizing in redraw tests. Gate meaningful viewport minimap content separately; whole-file overview readiness may follow index completion, with partial state explicitly indicated.

**13. MAJOR — §3 and forced toolkit decisions: the GTK measurement does not establish a universal native-toolkit startup floor.**

- **Claim:** Loading GTK3 through Python GI takes 121 ms, therefore no toolkit can meet startup gates.
- **Counter-argument:** That measurement describes the tested Python-GI loading path. It does not isolate a native application’s necessary toolkit initialization, and it says nothing about other toolkit paths.
- **Fix:** Keep that path out of the critical path unless demonstrated otherwise. Gate the actual executable and interactive window. Raw APIs, static linking, and a 2 MB binary may be useful implementation choices; this measurement does not make them mandatory.

**14. MINOR — §0.3 and §3: repair arithmetic and precision independently of the substantive gate changes.**

- `12.3/11.2 = 1.0982`: approximately **1.10×**, not 1.09×.
- Two decimal MB require **16**, not 15, 128 KiB requests when rounded upward.
- `31 × (150 +131) µs = 8.711 ms`.
- `31 × (500 +437) µs = 29.047 ms`, not 28.6 ms.
- **Fix:** Use consistent decimal MB/GB and binary KiB units, ceiling division for requests, and sensible rounding. Estimated historical timings should remain illustrations, not calibration constants for mandatory gates.

**Gate contract I would sign off on**

Both A and an instantiated B must pass independently. A remains on battery/powersave. B retains single-channel RAM and SATA storage. The B optical caps below apply to the specified one-frame-scaler fixture; a three-frame monitor is a separate declared display class, with the same app and incremental-latency requirements.

All latency entries are upper limits in milliseconds. “Warm” means the relevant source pages and required rendering resources are resident. Cold tests deliberately remove that residency. **These numbers do not certify feasibility before the fixtures are measured.**

| Gate / scenario | A p50 | A p99 | B p50 | B p99 | Binding resource and required behavior |
|---|---:|---:|---:|---:|---|
| Input ingress → corresponding rendered update ready and submitted: typing, backspace, ordinary undo/redo, resident navigation | 0.5 | 2 | 0.5 | 2 | UI queue/CPU; bounded foreground slices, including during background work |
| Physical keystroke → photon, mid-screen, active typing | 30 | 50 | 75 | 100 | Input/display chain; no editor-added frame queue |
| Same, first key after ≥15 s idle | 45 | 65 | 75 | 100 | Power-state/PSR wake; test actual supported defaults |
| Incremental optical latency over minimal renderer, matched condition | 1 | 3 | 1 | 3 | Editor overhead; slow peripherals do not create extra app slack |
| Complete warm full-frame preparation: scroll, page-down, tab switch, populated sidebar resize | 5 | 5.5 | 4 | 5 | CPU/GPU completion; include all visible features |
| Launcher request → readable interactive frame on glass, warm | 40 | 60 | 85 | 120 | Process/window/display path; first window must accept input |
| Same, cold executable/resources, previously security-scanned build | 50 | 80 | 100 | 150 | Storage faults plus display; defer optional initialization |
| First-install startup increment over equally cold, signed minimal native reference | 20 | 50 | 20 | 50 | Security/process overhead; report total first-install latency separately |
| Open → first correct text update submitted, warm, 1 KB–10 GB | 7 | 10 | 4 | 6 | Bounded source access/rendering; publish before bulk copy/index |
| Same, cold local file | 10 | 25 | 6 | 35 | Random I/O tails; asynchronous acquisition |
| Evicted viewport acquisition: cold tab switch, indexed jump, jump-to-end | 25 | 50 | 35 | 75 | Storage/residency; avoid blocking input during acquisition |
| Resident 1 MB paste, large selection deletion, transaction undo → updated frame submitted | 5 | 15 | 5 | 15 | Mutation/invalidation; chunk work and defer reclamation |
| 10k undo operations, one final redraw | 50 | 90 | 25 | 40 | Tree/history work; batch and render once |
| Warm 1 GB literal search: final count plus first 4,096 offsets | 65 | 90 | 85 | 115 | Read bandwidth plus real matching; bounded output |
| Search/index cancellation acknowledgment; reject stale generations | 1 | 5 | 1 | 5 | Worker scheduling; stop scheduling obsolete work |
| Warm 1 GB newline/chunk index completion | 65 | 90 | 85 | 115 | Memory scan; full UTF validation must have its own measured budget if included |
| Cold 1 GB index completion | 600 | 800 | 3,000 | 4,000 | Sequential storage; stream and preserve UI responsiveness |
| Unindexed jump to line 10 M, 40 B mean line length, warm | 30 | 45 | 40 | 60 | 400 MB scan; cancellable worker operation |
| Same, cold | 250 | 350 | 1,200 | 1,600 | Storage scan; immediate progress/cancellation |
| Save acknowledgment / visible pending status | 2 | 5 | 2 | 5 | Snapshot/enqueue; no synchronous flush on UI thread |
| Completed 1 MB save under validated durability contract | 10 | 50 | 45 | 250 | Flush tails; preserve input responsiveness |
| Completed 1 GB save, resident source | 1,500 | 2,500 | 3,800 | 6,000 | Write bandwidth/flushes; background streaming |
| Completed 1 GB save, cold source | 2,200 | 3,000 | 6,800 | 9,500 | Source reads and destination writes; include shared-device contention |
| Meaningful viewport minimap content ready, resident source | 5 | 20 | 5 | 20 | Source/layout/raster work; per-edit update remains inside typing gate |
| App CPU time per cursor blink | 0.1 | 0.2 | 0.1 | 0.2 | Wake/render overhead; event-driven idle |

The background completion gates run with ordinary typing present, one bulk job at a time. **Input gates must additionally pass with find, index, and save running together.** Their individual completion times in that combined case are reported; they are not each promised uncontended memory/storage throughput.

For 10 GB files, linear background gates scale with bytes actually scanned or written. For example, the cold-index p99 budgets become **8 s on A /40 s on B**. Initial usability remains governed by the bounded first-text gate.

Capacity and deterministic conditions supplement the percentile table:

| Gate | A p50 | A p99 | B p50 | B p99 | Additional requirement |
|---|---:|---:|---:|---:|---|
| Peak owned baseline memory | 80 MB | 80 MB | 42 MB | 42 MB | Every run fits; include backend handoff and owned graphics/shared buffers |
| Unedited copy-mode increment | `1.25S +64 KiB` | Same | Same | Same | Whole copy may complete after first text |
| Unedited mmap metadata increment | `32 ceil(S/65536) +2 MB` | Same | Same | Same | Clean source residency reported separately |
| 100k scattered one-byte edits, 200k retained pieces, 100k undo records: increment over unedited file | 26 MB | 26 MB | 26 MB | 26 MB | Retained history and snapshot structures count |
| Demanded refreshes missed in each 10,000-refresh continuous-scroll test | 0 | 0 | 0 | 0 | Count actual display refreshes; cover cold glyphs and background work |
| Idle periodic wakeups after blink timeout | 0/s | 0/s | 0/s | 0/s | No background jobs or external file-change events; blink ≤2/s and stops by 10 s |

Memory limits are maximum per-run peaks, not permission for an over-budget tail. The table repeats their caps in both percentile columns deliberately.

**Concrete CI measurement**

- **Dedicated native runners:** Use the actual A laptop and a fixed B machine, with real displays attached. Record power policy, battery state, temperature, desktop/display mode, storage fullness, security configuration, renderer, and corpus hash. Ordinary shared CI or Xvfb can test algorithms and correctness, but cannot certify these display gates.
- **Software latency:** A uses a monotonic high-resolution clock; B uses [QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter). A native producer injects timed input through the OS path and timestamps injection, dequeue, mutation completion, render completion, and present submission. Use preallocated trace buffers. Do not use a timestamp taken only when the editor finally dispatches the event.
- **Rendering and cadence:** Associate input IDs with frame IDs. Record CPU work, GPU completion, and presentation feedback asynchronously. On X11, Present supplies serial/UST/MSC completion information, but app-window completion is not sufficient evidence of final physical output under compositing. [X Present protocol](https://sources.debian.org/src/xorgproto/2025.1-1/presentproto.txt/). On Windows, use ETW/PresentMon to identify presentation mode, displayed frames, and queueing. Its software events do not measure panel response. [PresentMon documentation](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).
- **Optical latency:** Use a photodiode at the cursor’s mid-screen row, sampled at ≥20 kHz, with a synchronized electrical key-contact or measured physical-actuation trigger. Actuate A’s actual internal keyboard; substituting USB changes the tested path. Toggle a marker in the same frame as the verified edit. Define the optical threshold, for example 50% of the black-to-white transition. Test active typing and first input after idle separately. Software timestamps alone cannot certify keystroke-to-photon.
- **Statistics:** Use at least **10,000 samples per interaction scenario** and **1,000 launches per startup condition**. Randomize phase relative to refresh; do not pool targets, warm/cold states, or workload classes. Report nearest-rank p50/p99, sample count, and uncertainty. Never exclude dropped frames, timed-out operations, or failed launches from the result.
- **Cold state:** Evict the relevant file/resource cache on controlled runners and verify faults/I/O. Reopening the same path is not a cold test. First-install security scanning is a separate condition; report its total latency even when the incremental gate is used.
- **Memory and idle:** On Linux, classify mappings rather than blindly summing `Private_Clean`; include owned shared surfaces and report source RSS/PSS separately. On Windows, record private commit and working set separately; [PrivateUsage measures committed private memory](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex). Sample graphics allocations and peak handoff usage. Trace timers and thread wakeups during a quiet interval after the blink timeout.
- **Corpus and correctness:** Cover ASCII code with highlighting, Unicode/bidi, malformed UTF-8, dense short lines, a 10 GB single line, fragmented edit history, dense search matches, and 100 tabs. Verify displayed text and operation results. A placeholder, empty frame, stale minimap, or dropped input is not a latency pass.

A zero-miss result over 10,000 refreshes is useful evidence, not proof of a zero population miss rate: its approximate one-sided 95% upper bound is **0.03%**.

**What the document got right**

- The measured battery numbers should remain binding. Replacing them with turbo clocks or theoretical LPDDR bandwidth would invalidate the intended conservatism.
- Physical pixels are correctly used for HiDPI traffic: A’s framebuffer is **20,736,000 B**, and 90 Hz scanout is **1.86624 GB/s**. Logical resolution would undercount substantially.
- Halving memcpy’s r+w throughput correctly yields its payload-copy rate. The CPU copy formulas do not need another automatic factor of two.
- The MT warm-scan arithmetic is correct: **43.103/55.556/166.667 ms per GB** on A/B/C at the stated rates. Cold indexing being storage-bound is a sensible initial model.
- The sparse-index arithmetic is correct: **0.024414% of S**, or **2.441408 MB at 10 GB**, versus **2 GB** for eight-byte offsets at an average 40 B per line.
- Background indexing, immutable worker snapshots, bounded foreground work, streamed search results, and explicitly provisional line numbers are appropriate requirements.
- A’s measured pointer-chase latency legitimately makes its pessimistic tree/undo model slower than B’s estimated one. Higher advertised bandwidth does not reverse that arithmetic.
- Clean mapped source pages are reclaimable. They should be reported as residency, rather than treated as an additional private full-file copy.
- Separate CPU fallback and GPU paths are defensible. The unsupported part is claiming their deadlines have already been established analytically.
- Blink timeout matters beyond app CPU time, because it can keep the compositor and display power path active.

**Verdict:** **No**, the existing numbers are not safe as a release contract. Keep typing at **2 ms p99**, but include input queueing and add **0.5 ms p50**. Replace G2’s mean-only allowances with **A 30/50 ms and B 75/100 ms p50/p99**, plus **1/3 ms incremental optical overhead** and a separate idle-wake test. Make redraw **A 5/5.5 ms, B 4/5 ms** with an actual finite zero-miss test; change warm/cold startup to **A 40/60 and 50/80 ms, B 85/120 and 100/150 ms**. Make first text asynchronous, separate cold acquisition, define search completion, repair memory accounting, and promote save, bulk editing, undo, indexing, cancellation, cold tabs/navigation, and minimap readiness into the gate table above. B must be instantiated and measured before these budgets are described as achievable; unmeasured estimates cannot authorize exceptions or establish “fastest editor ever.”