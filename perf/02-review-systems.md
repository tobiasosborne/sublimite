Both files were read in full. All arithmetic below was recomputed with Python. MB/GB are decimal; KiB/MiB are binary. Unmeasured hardware timings remain estimates: multiplying them by slack does not turn them into validated ship gates.

1. **BLOCKER — Method, §0.2, historical calibration: estimated operating times are being treated as hardware lower bounds.**

   **Claim:** A pessimistic achievable floor, multiplied by \(k\), establishes both the gate and when a perceptual ceiling is physically unreachable.

   **Counter-argument:** \(S/R\) is a lower bound only when \(R\) is an upper bound on the relevant operation’s throughput. A slow measured scalar loop, an estimated compositor delay, and a guessed monitor scaler are different kinds of evidence. They cannot establish physical impossibility. Likewise, sums of stage estimates do not establish p99. The WordPerfect timings are explicitly estimates; their ratio cannot experimentally calibrate \(k=1.1\).

   **Binding constraint:** The actual implementation and its latency distribution, including scheduling, faults, synchronization and presentation deadlines.

   **Fix:** Separate theoretical traffic bounds, measured primitive timings, estimated pipeline timings and chosen engineering budgets. Keep the budgets as requirements, but remove assertions that they are guaranteed achievable or that stricter gates are physically impossible.

2. **BLOCKER — §1, G2, forced presentation architecture: Muffin and DWM do not have one interchangeable, fixed “+1 frame” pipeline.**

   **Claim:** Waiting for a compositor latch always precedes another complete compositor frame; absolute parity is consequently unreachable in every composited configuration.

   **Counter-argument:** Muffin’s redraw scheduling depends on its frame timing and synchronization policy. Its Clutter code explicitly supports changing when redraw occurs relative to presentation; that is not equivalent to a universal extra \(T\). On Windows, composed presentation, DirectFlip, Independent Flip and MPO are distinct paths. Microsoft documents eligible windowed flip configurations that bypass desktop composition. Conversely, the proposed **GDI/BitBlt CPU fallback is not a DXGI waitable flip swapchain**, so its queue cannot be controlled by the stated DXGI setting. [Muffin scheduling source](https://raw.githubusercontent.com/linuxmint/muffin/6.6.3/clutter/clutter/clutter-stage.c), [Microsoft presentation documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).

   **Number:** One wrongly included or omitted frame changes A by **11.11 ms**, B/C by **16.67 ms**. Removing the assumed extra compositor frame changes A’s recomputed mean from **27.00 to 15.89 ms**.

   **Fix:** Derive separate XShm, GL/DRI3, GDI and DXGI pipelines. Record presentation mode and queue depth during tests. Make compositor bypass conditional on verified unredirection; `_NET_WM_BYPASS_COMPOSITOR` is a request, not an application-controlled guarantee.

3. **MINOR — §1 and summary: the latency arithmetic and “phase” labeling need correction.**

   **Claim:** A mean is 26.4 ms; its compositor-free means are 15.3 and 9.8 ms.

   **Recomputation:** Using the listed stage endpoints and exact refresh intervals:

   | Model | A | B | C |
   |---|---:|---:|---:|
   | Mean, middle row | **27.00** | **68.30** | **63.90** |
   | Mean without extra compositor frame | **15.89** | **51.63** | **47.23** |
   | Mean without compositor or vsync wait | **10.33** | **43.30** | **38.90** |
   | \(1.1\times\) original model mean | **29.70** | **75.13** | **70.29** |

   The reported best/worst values also vary cursor position, keyboard delay, OS delay and panel response. They are not merely best/worst *phase*. Holding the cursor at mid-screen gives modeled extrema **17.72–36.28 ms on A**, **48.07–88.53 ms on B**.

   **Fix:** Correct the sums, distinguish phase from other uncertainty, and state that midpoint averages assume distributions that have not been measured.

4. **BLOCKER — Target B display archetype, §1 and G2: the scaler assumption makes the gate arbitrary in both directions.**

   **Claim:** An unspecified office monitor necessarily adds one full frame of scaler delay; the pessimistic configuration binds.

   **Counter-argument:** Scaling circuitry does not inherently require buffering a complete frame. The document supplies neither a monitor model nor a measurement. It also explicitly allows **three-frame** scalers while binding the gate to **one frame**.

   **Number:** Under its own otherwise unchanged model, zero scaler buffering gives B **51.63 ms mean**; three frames give **101.63 ms mean**, with \(1.1\times\) equal to **111.80 ms**. The proposed **75 ms** gate covers neither interpretation consistently.

   **Binding constraint:** Actual monitor processing delay and transition-specific panel response. A quoted LCD grey-to-grey settling time is not automatically the time to first detectable photon.

   **Fix:** Specify the monitor, native resolution, processing mode and optical threshold. Keep scanout position separate from pixel response. Measure both dark-to-light and light-to-dark text changes; report OLED and LCD results separately.

5. **MAJOR — Keyboard archetypes and §1: correct transport classification, unsupported timing distributions.**

   **Claim:** A’s EC-to-IRQ transport is 0.05–0.2 ms; a commodity USB keyboard has exactly an 8 ms polling interval; A is approximately 8 ms better.

   **Counter-argument:** The i8042 device identity establishes the absence of USB polling quantization. It does not measure matrix scan, debounce or EC delivery time. USB HID polling is specified by the endpoint descriptor, rather than universally by “office keyboard.” [USB HID specification](https://www.usb.org/sites/default/files/hid1_12.pdf).

   **Number:** For the stipulated polling intervals, changing 125 Hz to 1000 Hz saves **3.5 ms mean**, while the phase-range difference is **7 ms**. Comparing “no USB quantum” with 125 Hz gives approximately **4 ms mean** polling advantage, not an 8 ms mean advantage.

   **Fix:** Specify B’s keyboard and descriptor. Keep A’s scan/debounce range explicitly unvalidated. Define whether timing starts at physical actuation, electrical contact or delivered key event; those are different gates.

6. **MAJOR — Hardware profile, §1 and G1: kernel delivery and scheduling are not bounded by the listed 1 ms allowance.**

   **Claim:** Kernel-to-app delivery, including C-state exit, fits A’s 0.1–1.0 ms range.

   **Counter-argument:** The installed kernel configuration confirms **HZ=1000**, but also **CONFIG_PREEMPT_DYNAMIC=y** alongside the voluntary default. The configuration line alone does not establish the current runtime preemption mode. Neither HZ nor voluntary preemption provides a 1 ms scheduling guarantee under reclaim, writeback or driver activity. Interrupt delivery also does not wait for a periodic scheduler tick. [Linux preemption configuration](https://raw.githubusercontent.com/torvalds/linux/v6.8/kernel/Kconfig.preempt).

   **Binding constraint:** Event queue age and runnable-to-running delay. G1 starts after dequeue, so an application can pass while events have already waited substantially longer.

   **Fix:** Retain **2 ms p99** for dequeue-to-submit **wall time**, and add input-delivery/queue-age instrumentation under the specified background workloads. Report scheduler delay separately; do not invent a universal numerical ceiling from HZ.

7. **BLOCKER — §1 app allowance, §2 CPU raster: 400 MHz and hybrid-core execution invalidate the guaranteed CPU-fit conclusion.**

   **Claim:** A’s ramp costs approximately 0.15 ms, and CPU raster therefore fits the 5.6 ms gate.

   **Recomputation:** Even accepting the optimistic **0.5 cycle/pixel** kernel:

   \[
   2880\cdot1800\cdot0.5/(400\cdot10^6)=6.48\text{ ms}.
   \]

   Adding the document’s upload allowance gives **8.69 ms**, exceeding **5.56 ms** before layout or request overhead.

   Local read-only inspection reports `intel_pstate`, `powersave`, **`balance_power`**, a **400 MHz** minimum and **`hwp_dynamic_boost=0`**. HWP preferences do not establish a 0.15 ms boost guarantee. The P-core benchmark also does not establish E-core execution cost. [Intel P-state documentation](https://docs.kernel.org/admin-guide/pm/intel_pstate.html).

   **Binding constraint:** First-burst frequency, chosen core and migration, rather than sustained-loop bandwidth alone.

   **Fix:** Benchmark first edit and first full redraw after idle, on battery, including E-core placement. Treat GPU or parallel raster as implementation options necessary to meet the measured gate; remove the proof that single-thread CPU raster always fits.

8. **MAJOR — §1, §2, §13 and known gap 4: GPU/display wakeup is acknowledged but excluded from the gates.**

   **Claim:** A’s GPU frame costs 0.83 ms and its typing gate is 29 ms, including first input after idle.

   **Counter-argument:** Framebuffer bytes divided by GPU bandwidth excludes submission, fences, GPU power-state exit and display power-state exit. RC6 wake behavior is hardware dependent. PSR support and current activity must also be established before assigning a penalty. [i915 RC6 implementation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/rui/linux/+/44f28934af141149959c4e6495bb60c1903bda32/drivers/gpu/drm/i915/gt/intel_rc6.c).

   **Number:** Applying the document’s own possible **one-frame PSR exit** changes A’s modeled mean from **27.00 to 38.11 ms**, and its relative gate to **41.92 ms**. Its all-position worst case becomes **52.94 ms**.

   **Fix:** Split continuous typing from first input after idle. Measure GPU and PSR state alongside optical latency. Do not fold an unmeasured wake cost into a tiny raster bandwidth number.

9. **MAJOR — §2: memcpy payload bandwidth is not a measured raster or upload rate.**

   **Claim:** CPU background fill plus glyph alpha blending costs \(fb/9.4\), and Xorg/glamor upload costs another \(fb/9.4\).

   **Counter-argument:** Dividing the measured **18.8 GB/s read-plus-write memcpy** by two correctly obtains its payload rate. It does not establish either raster-write throughput or the server’s upload throughput. Background fill followed by glyph blending may reread and rewrite destination pixels; cache allocation, atlas reads and pixel format matter. The full A framebuffer is **15.82 times** its P-core L2 capacity.

   The alleged SIMD scan rate has the same problem: “the read side of memcpy” is not a measured read-only scan.

   **Number:** Until a relevant scan is measured, A’s binding measured ST read rate is **6.3 GB/s**: **158.73 ms/GB**, rather than **106.38 ms/GB**; **10.40 µs/64 KiB**, rather than **6.97 µs**.

   **Fix:** Benchmark the actual blend kernel and complete upload path separately. Preserve the framebuffer traffic calculations, but label the resulting times conditional estimates.

10. **MAJOR — §2 partial redraw, G1 and G3: submission, buffer reuse and visible completion are being conflated.**

    **Claim:** One-line redraw is 39 µs on A, with a 0.1 ms target; handing off the request establishes completion.

    **Recomputation:** Applying the full-redraw model consistently gives A **39.22 µs raster + 39.22 µs upload**. Adding the stated **20–50 µs** request overhead gives **98.43–128.43 µs**, before edit and layout.

    XShm completion governs when shared image storage can safely be reused; request return does not establish scanout readiness. Partial updates across alternating buffers additionally need valid retained contents or buffer-age repair. [XShm documentation](https://xorg.freedesktop.org/archive/X11R7.5/doc/man/man3/XShmPutImage.3.html).

    **Binding constraint:** Buffer ownership and the compositor’s submission deadline. An additional 2 ms of work can cross a latch boundary for **18%** of uniformly distributed phases at 90 Hz, producing an **11.11 ms** optical step.

    **Fix:** Define CPU completion, safe buffer reuse, compositor-ready completion and optical completion separately. Include upload damage and synchronization; keep presentation waits off the input thread.

11. **MAJOR — Threading, §§4/7, G1 and G6: worker priority does not reserve memory bandwidth or compositor capacity.**

    **Claim:** All-core scan throughput and isolated redraw throughput remain simultaneously available while G1 holds.

    **Recomputation:** Using the document’s memcpy-equivalent accounting, A’s two CPU passes at 90 Hz represent **7.46 GB/s** of read-plus-write traffic. Its compositor contributes **3.73 GB/s**, scanout **1.87 GB/s**: **13.06 GB/s** before search. Adding the isolated search rate gives **36.26 GB/s** of combined demand. These are unlike workloads; this sum does not establish available concurrent throughput.

    **Binding constraint:** Shared DRAM, caches, package power and scheduling of Xorg/Muffin. Lower worker priority addresses only part of this.

    **Fix:** Limit workers and measure typing/redraw concurrently with scanning and saving. For a provisional one-worker scan budget, \(2S/R\) gives **317.46/200/500 ms per GB on A/B/C**. Faster gates require a concurrency measurement, rather than the isolated all-core result.

12. **BLOCKER — G1, G5, §§1/4/5: the implicit maximum line length is missing from the supported-file contract.**

    **Claim:** Editing and first display are independent of file size; shaping concerns a line of at most 200 characters; one 64 KiB read suffices.

    **Counter-argument:** “Any file ≤10 GB” includes one enormous line, long combining sequences, tabs, bidi paragraphs and wrapping. Clipping pixels after whole-line layout does not bound the work.

    **Number:** At the document’s **5 ns/character**, a 1 GB ASCII line requires **5 seconds** of whole-line processing. At its **1 µs/character** shaping example, it requires **1000 seconds**. A sparse newline index does not bound paragraph layout or backward discovery of a line start.

    **Binding constraint:** Maximum foreground decoding/layout work and resident source bytes.

    **Fix:** Specify a bounded long-line mode, asynchronous paragraph shaping and bounded foreground chunks. G1 can promise immediate edit acknowledgement using resident state; newly faulted or newly shaped content needs its own completion metric. Test newline-free files explicitly.

13. **BLOCKER — §3 and G4: best-phase presentation is being used to gate an on-glass startup metric.**

    **Claim:** Startup bounds of 23.5/34.0/49.5 ms support on-glass gates of 47/68/99 ms.

    **Recomputation:** The sums before correction are **23.45/34.04/49.46 ms**. Restoring the document’s own average latch phase, middle-row scanout, scaler and panel stages gives:

    | Startup model | A | B | C |
    |---|---:|---:|---:|
    | Warm, mean on glass | **35.11** | **77.37** | **87.80** |
    | Cold, mean on glass | **37.51** | **86.02** | **116.63** |
    | Cold, worst phase/bottom row | **49.08** | **107.69** | **136.30** |

    These remain conditional on the disputed compositor/scaler model.

    Cinnamon adds another excluded condition: installed source uses a **120 ms × effect multiplier** map animation, beginning transparent and scaled for the traditional effect. That affects readable and settled presentation; it is not necessarily 120 ms to the first detectable pixel. [Cinnamon window animation source](https://raw.githubusercontent.com/linuxmint/cinnamon/6.6.9/js/ui/windowManager.js).

    **Fix:** Define the startup percentile, optical threshold and animation policy. Measure “first readable interactive frame” separately from “fully settled window.” Stop deriving an on-glass gate from best-phase presentation.

14. **MAJOR — §3, fonts and linking: hot desktop libraries do not remove per-process initialization; deferred discovery can still break typing.**

    **Claim:** GL driver, libc and font costs are hot because the desktop uses them; embedded startup fonts solve the font path.

    **Counter-argument:** Page-cache residency does not remove relocations, constructors, driver/context initialization or process-local font configuration. The proposed **20/50/80 ms** GPU-init estimates already dwarf the raster estimates. Worker initialization can also contend with the first frame unless it is sequenced deliberately.

    A review probe of five fresh `fc-match monospace` processes on this desktop took **16.26, 20.85, 19.31, 17.72 and 25.94 ms**. These include process creation and matching, and are not isolated `FcInit` measurements or cold-cache bounds. They nevertheless expose a concrete path far beyond G1’s **2 ms** if deferred font discovery first runs synchronously during editing. Fontconfig initialization builds process-local font information. [Fontconfig initialization documentation](https://fontconfig.pages.freedesktop.org/fontconfig/fontconfig-devel/).

    **Fix:** Keep embedded startup glyphs, then perform discovery and fallback-font preparation asynchronously. Measure the actual dependency closure and first-use paths. The **121 ms Python-GI GTK load** is a valid measured configuration cost, but does not establish a universal native-toolkit minimum or force “no toolkit.”

15. **MAJOR — §§3/4/5: page-cache warmth, populated mappings and cold storage are different states.**

    **Claim:** A warm mmap scan costs only \(S/R_{\text{read}}\); cold access is covered by one random read plus transfer time.

    **Recomputation:** A 1 GB mapping has **244,141 4 KiB pages**. Linux 6.8 normally attempts **64 KiB fault-around**, so a favorable first mapped traversal still entails roughly **15,259 fault events**. At an illustrative **1 µs/event**, that is **15.26 ms** beyond the scan; page-by-page faults would contribute **244.14 ms**. Neither timing is a measurement of A. [Linux fault-around implementation](https://raw.githubusercontent.com/torvalds/linux/v6.8/mm/memory.c).

    On C, sixteen separately serviced cold 4 KiB reads under the document’s latency model cost **8.44 ms**, versus the proposed single-request **0.94 ms**. Readahead can change that; it must be established.

    **Fix:** Benchmark three states: cached file/new mapping, populated mapping, and cold file. Use an explicit worker read/prefetch path for predictable sequential I/O; prohibit foreground major faults. Do not model every page as a separate fault when fault-around applies.

16. **MAJOR — Storage archetype, §4 and G5: NVMe power-state exit is missing from “cold first text.”**

    **Claim:** A’s first cold 64 KiB access costs approximately **0.13 ms**.

    **Counter-argument:** That models an awake controller. Local configuration reports `nvme_core.default_ps_max_latency_us=100000`: a **100 ms power-saving latency allowance**. This is neither proof that the controller enters such a state nor a measurement of its exit latency. It demonstrates that the operating-system policy does not establish the assumed sub-millisecond access bound. [Linux NVMe APST configuration](https://raw.githubusercontent.com/torvalds/linux/v6.8/drivers/nvme/host/core.c).

    **Binding constraint:** Actual enabled NVMe power state, wake latency, pathname metadata residency and queued I/O.

    **Fix:** Measure first read after idle separately from steady-state QD1 latency. Make G5’s cold threshold include measured controller exit and metadata time. Retain **9 ms** only for a clearly specified warm, bounded-input case until this is measured.

17. **BLOCKER — §4 and forced SIMD architecture: newline comparison is not UTF-8 validation.**

    **Claim:** SSE2 `pcmpeqb`/`pmovmskb` throughput proves the fused UTF-8 validation/index pass is memory bound on every target.

    **Counter-argument:** Those operations find newline bytes. They do not validate continuation structure, overlong encodings, surrogates or code-point limits. The final architecture section itself assigns non-ASCII SSE2 validation on C **0.5–1 GB/s**, contradicting the earlier universal memory-bound claim.

    **Number:** That rate gives **1–2 seconds/GB**, rather than **167 ms**. Even granting the stated 1.5× parallel speedup yields **667–1333 ms**. Published AVX2 validation results include approximately **12 GiB/s** for non-ASCII synthetic input, but were measured single-threaded, at sustained clock, with cache-resident data; they cannot replace this laptop’s battery measurements. [UTF-8 validation research](https://arxiv.org/html/2010.03090).

    **Fix:** Separate newline indexing from full validation, or measure the actual fused kernel. Add appropriate runtime dispatch. Preserve up to **three bytes** of boundary context between chunks; a valid sequence crossing a chunk boundary must not be flagged invalid.

18. **BLOCKER — §7 and G6: finding all results can be output-bound, and first-byte filtering does not bound verification work.**

    **Claim:** Every literal search completes at \(S/R_{\text{read}}\), including the last result.

    **Counter-example:** Searching 1 GB of `a` for `a` produces **one billion matches**. Eight-byte offsets alone occupy **8 GB**. Materializing input plus results requires approximately **9 GB** of traffic.

    **Number:** Even at the stated theoretical DRAM peaks, that traffic takes **87.89/351.56/703.13 ms on A/B/C**, before allocation, cache write allocation, verification or result consumption. B and C already exceed their gates by wide margins. Streaming avoids retaining 8 GB, but does not make emitting those results free.

    Repetitive near-matches also invalidate a generic memchr-plus-naive-verification throughput claim. A linear-time algorithm such as Two-Way has a different guarantee. [glibc search implementation](https://raw.githubusercontent.com/bminor/glibc/master/string/memmem.c).

    **Fix:** Define G6 as bounded-output scanning, counting or locating the last match. Cap materialized results and stream/count the remainder. Specify pattern sizes and adversarial repetitive inputs; budget result processing separately.

19. **BLOCKER — §§4/8/12 and threading: a read-only mmap is not an immutable original.**

    **Claim:** A tree over mapped original bytes provides immutable snapshots for search, save and undo.

    **Counter-argument:** Another process can modify the same inode in place. Read-only protection prevents this process from writing; it does not freeze backing-file content. `MAP_PRIVATE` does not supply a portable frozen snapshot either. Truncation can invalidate access, and notification/change detection cannot eliminate the race. [Linux mmap semantics](https://man7.org/linux/man-pages/man2/mmap.2.html).

    **Number:** Copying 1 GB through A’s measured page-cache path costs **238.10 ms**; a cold sequential read is estimated at **400 ms**. Snapshot creation therefore cannot silently occur within G1’s **2 ms** or §8’s claimed O(1) operation.

    **Fix:** Define an external-modification policy and stable backing-store strategy. A persistent tree root freezes structure only. Resolve backing-byte lifetime and consistency before claiming safe concurrent snapshots or byte-exact undo.

20. **MAJOR — §4 and §12: mmap residency is double-counted in B’s “10 GB does not fit” conclusion, while page tables are omitted.**

    **Claim:** A mapped 10 GB file exceeds B’s 16 GB once OS memory and page cache are counted.

    **Counter-argument:** Clean mapped pages and the page cache refer to the same physical pages; they are not two copies. Whether 10 GB remains resident depends on available RAM and competing applications. A **copied** 10 GB buffer plus a cached copy is different.

    **Number:** With 4 KiB pages and eight-byte PTEs, a fully populated mapping needs approximately **1.95 MB of leaf page tables per GB**, or **19.53 MB for 10 GB**, before upper levels. These are system memory costs outside a private-RSS gate.

    **Fix:** Separate private commit, mapped resident bytes, shared buffers, page tables and total working-set pressure. Keep streaming support because available memory can be insufficient; remove the unconditional B no-fit claim.

21. **MAJOR — §6: tree traversal is only part of insertion, and append-only does not imply bounded growth cost.**

    **Claim:** Five levels and ten misses establish **3.7/1.5/1.8 µs** insertion, independent of file size.

    **Counter-argument:** The multiplication is correct, but excludes allocation, splits, count updates, persistent-path copies and add-buffer growth. A contiguous append buffer can incur the same realloc/memmove event being used to reject gap buffers. Splitting an unindexed original piece also cannot instantly produce exact newline counts.

    **Number:** Scanning two 64 KiB split boundaries at A’s measured **6.3 GB/s** costs **20.81 µs**, before tree work. Cold boundaries add I/O rather than nanosecond cache misses.

    **Binding constraint:** Allocation policy, metadata availability and resident backing data.

    **Fix:** Require chunked add storage, prepared allocation pools and lazy/unknown newline counts until indexed. Keep **50 µs** as a measured resident-edit requirement, including splits and growth events; do not advertise traversal arithmetic as the complete insertion bound.

22. **MAJOR — §6 crossover and forced buffer decision: the crossover uses an unstated budget and is being described as a file-size failure.**

    **Claim:** A gap buffer fails C above approximately **0.75 MB**.

    **Counter-argument:** The equation bounds **gap movement distance**, not all editing in a file of that size. Adjacent typing does not move the entire file. The stated 1 ms app target also conflicts with G1’s **2 ms** cap.

    **Recomputation:** A quarter of the actual G1 cap is **0.5 ms**, giving movement limits **4.7/3.5/1.5 MB on A/B/C**. The document’s **2.35/1.75/0.75 MB** values correspond to a separate **0.25 ms** edit budget.

    **Fix:** State the explicit edit budget and adversarial caret movement. A piece structure is justified by unrestricted remote edits and files larger than RAM; the given crossover does not prove it is uniquely best for every small file.

23. **BLOCKER — G7, §§11/12: the memory gate conflicts with the document’s own edited-file example.**

    **Claim:** A mapped file always fits \(0.1\%S+1\) MB private memory.

    **Recomputation:** For the stated **100,000 edits / 200,000 pieces** on a 10 GB original:

    \[
    200000\cdot48=9.6\text{ MB},\quad
    100000\cdot32=3.2\text{ MB},\quad
    I=2.441408\text{ MB}.
    \]

    Total: **15.24 MB before typed text, tree-node overhead or persistent history**. The gate permits **11 MB**.

    The three-height minimap cache also adds **5.184/1.555/1.106 MB on A/B/C** to the stated baseline. Preserving the document’s 1.5× multiplier changes baseline gates to **86.48/43.72/30.75 MB**. This does not prove the old caps are unattainable; it disproves their stated derivation.

    **Fix:** Split unedited-file overhead from retained edits/history. Use a budget of sparse index + explicit pieces/nodes + undo + retained added text. Include the minimap and rendering-resource ownership. Bound history by a declared policy rather than pretending arbitrary edits fit a fixed percentage of the original file.

24. **MAJOR — §8: save time assumes overlap, warm source bytes and fixed flush latency without establishing them.**

    **Claim:** \(\max(S/R_{\text{copy}},S/R_{\text{write}})+2F\) covers durable save.

    **Counter-argument:** The maximum is a valid ideal pipeline expression when copy and device write overlap. Buffered writes followed by fsync need not achieve that overlap. Saving mapped original ranges can additionally read cold data from the same device that receives the new file.

    **Number:** Without copy/write overlap, a warm 1 GB save becomes **1242/2920/20707 ms on A/B/C**, rather than **1004/2520/20040 ms**. A fully serial cold-source model gives **1642/4920/27373 ms**. These are scenarios, not inevitable timings. Applying the document’s stated possible 10× A flush variation changes two flushes from **4 to 40 ms**, already exceeding the **10 ms** small-save target.

    **Fix:** Define warm/cold source, piece fragmentation, durability completion and percentile. Measure flush distributions and the actual streaming pipeline. Keep UI responsiveness independent of durable-save completion.

25. **BLOCKER — §8 Windows save recipe: “ReplaceFile with write-through” is not a supported durability primitive.**

    **Claim:** ReplaceFile/MoveFileEx with write-through supplies the equivalent of the two Linux flushes.

    **Counter-argument:** Microsoft explicitly marks `REPLACEFILE_WRITE_THROUGH` **unsupported**. `MOVEFILE_WRITE_THROUGH` documents a flushed copy/delete move, which is not sufficient evidence for the proposed same-volume atomic-replacement durability contract. Mapping compatibility also depends on the precise operation, sharing flags and filesystem semantics. [ReplaceFile documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew), [MoveFileEx documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).

    **Binding constraint:** A working, crash-consistent Windows replacement protocol compatible with retained original data.

    **Fix:** Specify and test that protocol before attaching **two × 10 ms** to it. Separate atomic replacement, mapped-file lifetime and durable completion; the known-gap note does not make the forced architecture implementable.

26. **MINOR — Summary bookkeeping and archetype scope.**

    **Claim:** “Undo 10k steps (+ one redraw)” is bounded by 37/15/18 ms.

    **Recomputation:** Including the stated redraw gives **41.51/17.37/20.80 ms**; twice those values is **83.02/34.74/41.60 ms**. The full-redraw divisions are approximately **4.412/2.370/2.798 ms**. The nominal 1 MB file-count calculation, using the actual 64 KiB allowance, gives **6081/3040/760** before baseline memory, rather than 6088/3044/761.

    **Fix:** Correct these entries and use sensible rounding. Define B as a specific reproducible configuration, rather than the worst of all commodity desktops. Define C separately: an unspecified VDI/RDP configuration has network and encoding constraints that its local framebuffer equations cannot cover.

**What the document gets right and should retain:**

- **Physical HiDPI accounting:** A’s framebuffer is **20,736,000 B**, not the logical-resolution framebuffer. Its nominal scanout traffic is **1.866 GB/s at 90 Hz**.
- **Scanout position matters:** middle-row traversal is approximately **5.56 ms on A**, **8.33 ms on B/C**. Fast OLED transitions do not eliminate that traversal.
- **A’s internal keyboard has no USB polling quantum.** Adding an obligatory 8 ms USB stage to A would be wrong.
- **Measured battery results should remain binding.** Replacing them with LPDDR5 theoretical peak, AC benchmarks or P-core turbo assumptions would weaken the review.
- **371 ns is explicitly TLB-inclusive pointer-chase latency.** Using it conservatively is legitimate; calling it pure DRAM latency or substituting a vendor CAS number would be wrong.
- **The memcpy read-plus-write conversion is correct:** **18.8 GB/s aggregate → 9.4 GB/s payload** for that memcpy test. The error is transferring this rate to unrelated kernels.
- **Sparse indexing is justified:** **16 B/64 KiB = 0.024414%**; 10 GB needs **2.441 MB**. An eight-byte per-line array at 40 B/line needs **2 GB**. That 20% figure is a workload example, not a universal ratio.
- **Lazy indexing, bounded damage, cached shaping and background search/save are appropriate.** They need bounded allocations, faults and concurrency policies to deliver the gates.
- **Clean mapped pages are reclaimable**, and avoiding a compulsory private copy supports files larger than available RAM. mmap itself does not guarantee residency or snapshots.
- **Keeping GPU initialization and font discovery off startup’s critical path is sound.** Those costs were acknowledged, not completely forgotten; their first-use and contention consequences were omitted.
- **A CPU fallback, embedded initial glyphs and a blink timeout are reasonable design choices.** Their utility does not require the stronger claim that the existing equations prove every fallback configuration meets every gate.

**Verdict:** **No, the ship-gate set is not safe to build against as derived.** Keep G1 at **2 ms p99 wall time**, with bounded resident foreground work and queue-age reporting, and keep G8’s **0.2/0.2/0.4 ms CPU per blink** and wakeup limits. Replace G2’s unsupported **29/75/70 ms floors** with a measured same-path baseline: permit **≤2 ms added mean latency**, **≤one refresh interval added p99**, and no extra queued frame; report first-after-idle separately. Use G3’s **half-frame budgets: 5.56/8.33/8.33 ms**, verified through compositor-ready completion. Replace G4’s best-phase-derived gates with a provisional **100 ms controlled-desktop on-glass goal for A/B**, with normal map effects reported separately; if C remains, use **150 ms cold** provisionally and validate it. Restrict G5’s **9/5.3/7.5 ms** gates to bounded warm input; cold thresholds must add measured metadata and power-exit costs. Make G6 bounded-output and provisionally **320/200/500 ms per warm GB** with one worker until concurrent optimized measurements justify tighter gates. Recompute G7’s baseline allowance to approximately **87/44/31 MB** under the stated 1.5× method, and replace its edited-file percentage cap with explicit index, tree, text and history accounting. These are testable engineering budgets; the unsupported hardware-floor conclusions should be removed before they drive implementation choices.