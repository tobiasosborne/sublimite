# Wave 1 L0 decision — mapped-file fault identity and backing lifetime

**Decision.** The existing backing reference is the lease. A snapshot or line index that retains the backing keeps both the VM extent and its registration alive. L0 supplies a copyable registration identity and a lock-free validated epoch poll. Only final backing release retires the registration. A short-lived reference inside the signal handler still protects a repair already in progress.

The reference is v1 at `6fec2e8`. Source paths and symbols below refer to that pin unless identified as v2. This revises the API spelling in v2 `docs/PLAN_V2.md` §4.1 and preserves its lifetime requirement, the §2 include order and the §4.3 adoption rule.

## Reader and owner audit

**Is there an unprotected reader?** There is none among the production readers of registered document bytes or their fault epochs in v1 or the proposed v2 path. Some borrowers hold no *individual* reference. Their owning object keeps a reference through their last access.

| Reader at the pin | What actually keeps the mapping alive |
|---|---|
| File change and save | In `src/file/file.c`, `guard_faults` and `guard_faulted` are the production callers of bare `os_fault_epoch`. `map_change` borrows the file's map. `file_save_begin` retains a snapshot and records `f->map` in `f->save.source_map`. `source_validate` borrows that pointer. `file_close` waits for the physical jobs before releasing the file's map. `file_source_acquire` separately acquires the map held through `file_source_validate` and `file_source_adopt_retired`, then dropped by `file_source_release`. |
| Tree, snapshot, find and other snapshot consumers | `src/piece/piece.h:piece_map_hooks` specifies the acquire/release pair. In `src/piece/piece.c`, the tree owns the cache's mapping lifetime, `piece_snapshot_retain` acquires an external owner's reference and `snapshot_drop` releases it. `src/file/file.c:file_snapshot_bytes` returns a span borrowed from that lifetime. `src/find/find_int.h:poll_stop` takes its snapshot from the caller, whose lifetime obligation runs through job acknowledgement (`src/find/find.h`, work-integration contract). `src/editor/highlight.c:run_inner` likewise polls the job's snapshot. |
| Completed line index and running index job | `src/lineidx/lineidx.c:lineidx_bind_snapshot` takes its own backing reference. `lineidx_job.backing` borrows from the index. `worker_stop`, `receive_results` and the UI queries use it. `lineidx_destroy` reaps retired jobs before releasing that reference. The source for worker *bytes* has its own lifetime through physical completion under `src/lineidx/lineidx.h:lineidx_src`. `src/scroll/resident.c:scroll_resident_bind_snapshot` and `scroll_resident_close` use the same ownership pattern. |
| Reload and exclusive owners | `src/savectl/savectl.c:reload_worker` owns the v1 reload mapping via `reload_bytes`. `savectl_take_reload` transfers readable lifetime through `bytes_acquire` / `bytes_release`. This v1 path needs registration in v2. Direct OS test clients such as `src/file/file_test_helpers.c:file_test_guard_registry` own their maps outright. Outside the document service, `src/wayland/wayland.c:keyboard_keymap` directly owns its temporary unregistered map. None reads a document on the strength of a stale slot. |

**What failure would a persistent OS pin count prevent?** None for those readers. v1 `src/file/file.c:map_release` goes from final reference to retire, then unmap. `docs/reviews/edit-pf3l.md` explicitly audits this ordering. A second count could refuse premature teardown by a caller that broke the ownership contract. The smaller design loses that diagnosis and the ability to demand a registration hold using only a naked ID. An ID alone never conferred ownership of the backing object or safe access to its pointers. The piece token carries the backing reference. A poll of a copied stale ID remains safe even without that reference because it touches only process-lifetime atomics. The handler scanning someone else's slot has a genuinely different lifetime and still needs its transient reference.

## Public contract

Complete proposed fault subsection of v2 `src/os/os.h`, using its existing `os_map`, `os_err`, `size_t` and fixed-width types.

```c
#define OS_FAULT_SLOTS 256

typedef struct os_fault_id {
    uint32_t slot;
    uint64_t gen;
} os_fault_id;

#define OS_FAULT_ID_INIT { .slot = 0, .gen = 0 }

os_err os_fault_register(const os_map *view, os_fault_id *out);
os_err os_fault_poll(os_fault_id id, uint64_t *epoch);
os_err os_fault_retire(os_fault_id id);
os_err os_fault_test_trigger(const os_map *view, size_t off);
```

Capacity 256 (M, table size defined by v1 `src/os/os.h:OS_FAULT_SLOTS`) comes from historical `src/file/file.c:FILE_GUARD_SLOTS`, removed by migration commit `1d7d786`. It bounds storage and the handler scan. `uint32_t` follows v1's fixed-width slot word and can represent every table index. Slot zero is valid. Generation zero denotes absent/unpublished (P, one sentinel outside the issued nonzero range). Epoch zero means pristine, with one increment per claimed recovery attempt as in `src/os/os_posix.c:fault_recover`.

| Entry | Ownership, threading, return |
|---|---|
| `os_fault_register` | Owner on an ordinary thread, before publishing readable bytes. Borrow one live read-only private `os_map_file` view through successful retire. Reject null inputs, null/empty view, unaligned base or unrepresentable logical/rounded end with `OS_E_INVAL`. Under the registry mutex reject intersecting page-rounded VM extents, including duplicates, with `OS_E_EXIST`. Distinct mappings of the same file are fine. Installation unavailable gives `OS_E_NOTSUP`. No available slot or no remaining issuable identity gives `OS_E_NOSPC`. Success writes the ID; failure writes the absent ID if output is valid. |
| `os_fault_poll` | Any ordinary thread can poll a copied ID with its own output cell, concurrently with polls, delivery, retire or reuse. On `OS_OK` writes the validated cumulative epoch, saturated at `UINT64_MAX`. Null output, zero generation or out-of-table index gives `OS_E_INVAL`. Well-formed identity absent or superseded at validation gives `OS_E_NOENT`. Every failure leaves the output untouched. The function reads slot atomics only. It grants no permission to dereference a mapping. |
| `os_fault_retire` | Registration owner only, at final release after ending all application byte access. Validate under the mutex. Malformed ID gives `OS_E_INVAL`; a missing/stale ID gives `OS_E_NOENT` with no effect on the current registration. Close and drain the handler gate as specified below. Only `OS_OK` authorizes unmap. No public BUSY result. |
| `os_fault_test_trigger` | Test thread with SIGBUS unblocked and a valid view kept mapped through return. Null view/base, overflow or offset outside its logical extent gives `OS_E_INVAL`. As in v1 `src/os/os_posix.c:os_fault_test_trigger`, send the native address fault to the calling thread. Return the mapped native send error on failure. A delivered signal can chain or terminate the process. A successfully recovered signal returns `OS_OK`. |

All four are ordinary thread entries, with no C heap allocation. Use the existing OS native-error convention. The only mutable process global is the PLAN-named `os_fault_service`. A stale ID can be copied, retained and discarded without cleanup. Transferring a **byte reader** across a mailbox requires publishing an already owned backing or snapshot reference and holding it through physical release.

## State and lifetime

The private slot enum is `SLOT_FREE, SLOT_RESERVED, SLOT_LIVE, SLOT_RETIRING` from zero in that order, the phases of v1 `src/os/os_posix.c:fault_slot`. Installation uses `FAULT_COLD, FAULT_READY, FAULT_UNAVAILABLE` under `pthread_once`; a prior one-shot disposition uses `CHAIN_ARMED, CHAIN_CONSUMED`. Handle each enum exhaustively under `-Wswitch-enum`. Backing references use the existing backing ownership protocol. There is no independent L0 lease FSM.

| State | Event and actor | Transition or result |
|---|---|---|
| FREE | Register, owner under the service mutex | After fallible validation choose the lowest free index and reserve it. Concurrent first registrations share one installation. |
| RESERVED | Publish, registering owner | Set range and initial epoch, release-publish LIVE, then publish the issued generation before returning the ID; writer ordering below. Handler skips RESERVED. Other writers are excluded. |
| LIVE | Retain/release backing, any authorized ordinary holder | Slot stays LIVE through each nonfinal release. A retain requires an already live owner or a synchronized handoff. Copying an ID has no effect. No OS pin/unpin events. |
| Any | Poll, ordinary observer | Sample published generation, epoch, generation. Success linearizes at the epoch sample if both generation samples match the argument. Otherwise return the indicated error. No slot mutation. |
| LIVE | Signal to any thread | Admit transient handler using a control-word CAS, inspect the range, recover or skip, then drop admission. No backing reference is acquired by the handler. |
| LIVE | Retire, final owner under mutex | Invalidate the published generation, then close LIVE → RETIRING with a CAS preserving the transient count. Existing handler admissions finish. No new admission after close. |
| RETIRING | Previously admitted handler exits | Drop count. The final departing handler posts the service drain only when its decrement saw RETIRING. New handlers skip the slot. |
| RETIRING | Owner completes drain | Publish FREE under the mutex. Only now return success. Reuse may issue a new ID even if the old numeric index survives in a mailbox. |
| FREE, RESERVED, RETIRING | Signal scan or invalid owner operation | Skip these phases for handler admission. Register scans only for FREE. Stale retire cannot advance another registration. Publication/close/free by anyone except the corresponding writer is illegal. |
| Any | Process exit | The kernel releases the VM, signal disposition and service. Normal shutdown stops new jobs, joins/reaps work and releases owners. No exit-time slot reset or wait for abandoned application references. |

**Final release invariant.** Only the backing's atomic last-release transition enters its destructor. An ordinary nonfinal release simply drops its reference. If a purported final teardown detects “still in use”, mismatched ownership, or a failed retire, ASSERT the violation. The release build keeps the mapping and the backing resources needed by its outstanding users alive, even if this leaks them until exit. Do not unmap, free, force-close, retry or schedule a timer. There is no shadow counter to detect a lost or fabricated application reference. A handler already admitted when retire closes is an expected condition and follows the drain protocol.

## Publication, validation and identity proof

The service mutex serializes register and retire, including slot selection and overlap checks. A service-local unsigned 64-bit issuance counter, written only under this mutex, records the last issued generation across **all** slots. Each slot has an atomic published generation (zero while unissued) and a separate atomic epoch. These contain different information: whether the ID can be polled, and whether its bytes have faulted. Slot admission control separately protects the bytes of metadata the handler uses. All poll-visible generation loads/stores, epoch loads/resets and handler epoch updates are **sequentially consistent**. No poll reads the non-atomic issuance counter, address or length.

* **Publish.** With published generation zero and the old handlers drained, reserve a free slot, assign its metadata and reset its epoch to zero. Release-store LIVE to control, then store the fresh generation. This last store publishes the pollable identity. Only then hand out the ID and expose bytes. No registration for the same pages can overlap this transition.
* **Close.** Store zero to the published generation **before** closing/draining the handler gate and before any future reset of the epoch. Always invalidate on retire, including when the slot will remain FREE indefinitely. Leaving the old generation visible across the reset would let a stale reader return a new tenant's pristine epoch.
* **Poll.** After argument checks, load generation; return NOENT immediately on mismatch. Load epoch into a local, then load generation again. Return NOENT on mismatch, otherwise copy the local to caller output. In the single SC order, matching nonzero samples enclose the epoch load wholly within one publication interval: each generation is issued once, even if the slot goes through FREE and LIVE between two other operations. A failing poll may sample a later epoch *internally* and discards it. A successful poll overlapping retire can return the old value only as of a point before invalidation. A synchronized poll begun after retire cannot return it. No retry loop, lock or allocation on the read path.
* **Read bytes.** (Amended at the piece Lines freeze, advisor 2026-11-23: one closing poll.) A backing owner polls once after the last mapped load an exact answer or result depends on, requires pristine zero and a generation match, and validates before exposing it. The epoch of a registration never decreases, so zero at the closing poll means zero at every earlier load: no recovery had run and every byte read was file-backed; a retire and re-registration in between fails the generation check. An opening poll is an optional early-out, needed only by a reader that compares two nonzero epochs for equality (none does). A full compiler/CPU fence before the closing validation prevents the loads moving after it. A common implementation may put this fence at poll entry. The handler orders the taint before VM replacement. Equal nonzero epochs already mean compromised; saturation cannot restore pristine. A stale ID returning NOENT invalidates the computation. A successful standalone poll never renews a dead byte pointer.
* **Generation width.** `UINT64_MAX = 2^64 − 1` usable generations (P, width minus the absent value). The review's illustrative rate of `10^9` registrations/s (E, assumed rate, no measured or architectural bound) gives about 584.5 years using `2^64 / (10^9 × 3.156 × 10^7)`, with seconds/year rounded from days/year times hours/day times seconds/hour. Neither that rate nor a maximum process age is a physical constraint. Before issuance, if the counter is already at maximum, ASSERT in debug and refuse new registration with existing NOSPC in release. Keep all currently issued IDs usable until retired. This one comparison prevents wrap for arbitrarily long execution, without a per-slot EXHAUSTED phase, a new public error or a reduced-width test archive.
* **Transient count width.** Retain a 64-bit lock-free control word on the supported ABI. Four phases need two low bits (P, four encodings), leaving 62 count bits. Reader increment/decrement is by four (P, the next bit above the phase mask). Even the previous smaller 61-bit count was sufficient: each simultaneous admission requires a distinct active Linux signal frame larger than eight bytes and holds at most one slot at a time. Fewer than `2^61` such frames fit in an address space of at most `2^64` bytes (P). Verify pointer width and the native signal-frame size premise at build time on supported Linux targets. No runtime overflow branch, chaining fallback or handler-side assertion is needed for this count. Epoch counts **successive** events and therefore still saturates by checked CAS.

## Signal delivery and the shared drain

**Install.** Adapt `src/os/os_posix.c:fault_install`. Initialize the one process-local drain semaphore to zero (P, no outstanding completion). Cache a positive power-of-two page size. Check actual lock freedom of published generation and epoch for lock-free polling and of every handler atomic (control, epoch, previous-action selector, one-shot disposition). Unsupported platform, failed initialization or failed signal installation makes the once result UNAVAILABLE. Clean up initialized resources on pre-install failure. Only this service installs SIGBUS, per v1 `docs/decisions/edit-177r.2.md` §8 item 1, §3.6 and §9 item 7.

**Recover.** `src/os/os_posix.c:fault_handler` saves thread-local errno and accepts SIGBUS with valid siginfo and code `BUS_ADRERR` or `BUS_OBJERR`. In `fault_recover`, acquire-load each static slot's control and acquire-CAS a transient increment only in LIVE (relaxed CAS failure, re-evaluate phase). Read the plain immutable base/length only after successful admission. Even if the control word was recycled before this CAS, the handler reads the metadata of the registration it actually entered. Check `a >= base && a - base < len`. Drop admission on mismatch and continue. Overlap rejection makes the matching registration unique.

On match, SC-CAS increment the epoch until successful or saturated. Order it before remapping via a sequentially consistent fence. Round down the fault address using the cached page size. Recover with read-only `mmap(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE)` from that page through the logical end. Page rounding stays inside the borrowed extent; bytes before that page survive. Concurrent suffix repairs install identical zero contents. Release admission on **every** path, including failed remap. Failure taints and chains. Recovery relies on the Linux/glibc AS-safe `mmap` contract; POSIX alone does not provide it. An unaudited backend returns NOTSUP at installation. [GNU C Library, Memory-mapped I/O](https://sourceware.org/glibc/manual/latest/html_node/Memory_002dmapped-I_002fO.html)

**Drain.** v1 `src/os/os_posix.c:os_fault_retire` CASes the phase to RETIRING, then acquire-loads the control word in a `sched_yield()` loop until only the phase remains. In v2 **one semaphore for the service suffices because exactly one retire can be waiting**. Keep the registry mutex through close, wait and FREE publication. No poll or handler acquires it. Other owner-side register/retire calls can wait behind that drain; byte readers can continue on their own live mappings.

* Close via acq_rel CAS preserving the handler count (acquire on failure, recheck). A zero count at the successful CAS requires no post and no wait. A positive count requires exactly one `sem_wait`, restarting on EINTR, with cooperative application cancellation only. The wait holds the registry mutex.
* Each departing handler performs an acq_rel count decrement. Only the handler observing RETIRING **and** last reference in its returned old word calls async-signal-safe `sem_post`. It does this after finishing all accesses to the mapping and slot, then returns or chains. An early post is retained by the semaphore. Late arrivals cannot enter RETIRING. There is no notification on an ordinary last departure from LIVE.
* Consume the owed post even when the count has already fallen to zero. The retiring thread acquire-observes closed/zero and release-publishes FREE before unlocking. Thus a second retire cannot consume the first's post. At most one post can be outstanding (P, one serialized retire and one last departure). The semaphore has process lifetime. No waiting, mutex operation, allocation or logging runs in the handler.

**Chain.** *(Superseded by amendment A18, see "Chain and install (A18)" at the end of this file.)* In `fault_install` query the previous action into immutable service storage and release-publish its selector before installing the new action. Use one replacing `sigaction(SIGBUS, &new, &old)` to capture the action actually replaced. As in v1, if publishing the returned action, write a second immutable cell and release-store the selector. Two cells (P, one readable during installation and one for the returned action) suffice. Inherit the previous mask and `SA_ONSTACK`, `SA_NODEFER`, `SA_RESTART` flags; install `SA_SIGINFO`. A competing, independent installer violates this process contract. The handler does not read back installation state or consult a file object.

On wrong code, unmatched address or failed recovery, drop all transient admissions before `fault_chain`. Acquire-load the selector, restore interrupted errno before forwarding and restore it on return. With a previous simple handler call it with the signal; with `SA_SIGINFO` forward the original siginfo and ucontext. For a one-shot action an acq_rel exchange ARMED → CONSUMED reserves its single invocation. Honor `SIG_IGN` for a forwarded signal; returning cannot repair a real synchronous fault. For default or consumed one-shot install default with `sigaction` and re-raise on the faulting thread via `raise`. If that fatal dispatch itself fails, use async-safe `_Exit(EXIT_FAILURE)`. Successful recovery also restores errno. Handler-visible state is limited to signal-frame locals, errno, the published immutable page size/prior actions, admitted range, verified lock-free atomics and the drain post.

## L2 and L1 integration

| Owner | Change from v1 |
|---|---|
| `file` and reload | `src/file/file.c:open_job` registers before publication. Store the resulting ID in the existing backing. `guard_faults` / `guard_faulted`, change detection and save/source checks use validated poll while the backing is held. Route `src/savectl/savectl.c:reload_worker`'s private file-backed map through the same construction and finalization. On failed registration, dispose of the unpublished map and fd. On a later failed handoff, drop the acquired backing reference. Final release retires successfully before unmapping. A reload builds a new registration and index; observers of the old backing can still read, recover and invalidate it. Private heap copies use the absent identity. |
| `piece_snapshot` | Extend `src/piece/piece.h:piece_map_hooks` with an `os_fault_id` supplied by the backing owner. Existing acquire/release callbacks are the retain/drop operations. Preserve v1 `piece_snapshot_take`, cached takes, external retains, worker release and deferred reclamation lifetimes. A live snapshot reads its original's hooks/ID. It needs no additional header hold. Expose `bool piece_snapshot_faulted(const piece_snapshot *)` and a caller-stored piece token for acquiring, polling and dropping a backing independently of the revision. Acquire by copying the hooks/ID from a retained snapshot and invoking the mapped backing's acquire hook before returning. Drop releases exactly that acquired hook reference. No downcast or callback back into file for polling. Private/unregistered sources need no fault token; find's null snapshot selects flat bytes. For a registered source any poll error fails validation. |
| `lineidx` | Replace `src/lineidx/lineidx.c`'s `file/file.h` with the public piece interface. In `lineidx_bind_snapshot` acquire the standalone piece token in place of `file_snapshot_backing` / `file_backing_acquire`; release it in `lineidx_destroy` after job retirement. This keeps completed metadata invalidatable after file close without retaining a revision, as `src/lineidx/lineidx.h:lineidx_bind_snapshot` requires. Replace `file_backing_faulted` at `worker_stop`, `receive_results`, `invalidate_backing`, `lineidx_any_nonascii` and `lineidx_chunk_nonascii` with token poll. Bracket exact computations and check queued results before adoption. Taint cancels pending adoption and makes answers inexact. |
| `find` | Drop `file/file.h` from `src/find/find_int.h`; `find.h` already includes `piece/piece.h`. In `poll_stop` call `piece_snapshot_faulted` on the held source. Preserve work/generation cancellation. Validate after mapped reads before reporting matches, visitor batches or final success, including a short last batch. Discard results derived from the recovered zero suffix. |
| Physical work retirement | Acquire an owning source reference before submit. Roll it back on failed submission. Logical cancel, supersession and mailbox detachment suppress stale adoption but retain a running job's source. `src/work/work.h:work_handle_finished` acknowledges physical return or queued removal. Release job resources exactly once after that acknowledgement or joined shutdown, including continuation rescheduling. `src/lineidx/lineidx.c:job_done`, `reap` and `job_free` are the v1 model. The work module handles generic job lifetimes without upward includes. |

For PLAN §4.3, stamp a bulk rebuild with the document revision, backing identity and pristine epoch. Read the retained post-edit snapshot on the worker. The UI swaps a replacement table only if the identity and revision still match and a final poll returns the submitted pristine epoch. A later fault invalidates subsequent exact queries. Capacity degradation permits off-path regrowth; faulted bytes require a new backing. These paths and token retain/poll/drop need no typing-path malloc.

## Implementation acceptance tests

Write the public-header tests failing first. Demonstrate behavioral red on a bare-index poll/reset implementation: after reuse it returns a current tenant's epoch for the old ID. Demonstrate a separate red when final source release precedes physical worker completion. Link the layer archive and use disposable maps; no private C-file includes. These are acceptance criteria for the implementation.

| Single-thread public case | Acceptance oracle |
|---|---|
| Lifetime and stale ID | Register A, poll pristine, trigger and observe its increment. Retire A, poll the saved ID before reuse and after registering B in the same index. Both return NOENT and preserve output; B has a fresh generation and pristine epoch. A stale retire cannot disturb B. Trigger B and verify only B can return its new epoch. |
| Arguments and overlap | Reject null pointers, invalid ID, empty/unaligned/overflowing extents and trigger at logical end. Check the exact public errors and output preservation. Reject duplicate and partially overlapping rounded ranges with EXIST. Adjacent extents and distinct maps of one inode each register and taint independently. |
| Capacity | Hold exactly `OS_FAULT_SLOTS` distinct registrations, then one extra disjoint registration gets NOSPC with absent output. Retire one and retry the extra; only the freed index can be reused. v1 `tests/os_test.c:case_fault` used repeated copies of one view for this fixture. |
| Recovery | Adapt v1 `tests/os_test.c:case_map` / `case_fault`. Pattern-fill a mapped prefix and two possible suffix starts (P, one page per role). Trigger later then earlier. Each increments once, zeros the expected suffix and leaves the prefix and neighbor intact. Include a partial final page and remap to verify the actual file bytes. In a fresh native child truncate on a page boundary and perform a volatile read of the missing page. Require a zero result, positive epoch, intact prefix and normal exit. |
| Chaining (superseded by A18) | In fresh exec children test a stray native fault with default disposition (death by SIGBUS), ignored asynchronous delivery, both previous handler signatures, original masks/context and one-shot then default. Accept both recovery codes, forward unrelated codes, preserve errno, and verify a successfully handled mapped fault does not consume the previous one-shot. |
| OS errors | Link isolated fixtures with `-Wl,--wrap=sysconf,--wrap=sigaction,--wrap=sem_init,--wrap=mmap,--wrap=syscall` as appropriate to actual referenced symbols. Inject bad page size, failed service semaphore setup, failed query and failed install in fresh processes. Require sticky NOTSUP, absent output and unchanged prior disposition. Fail only the handler's fixed remap during a synthetic trigger: prior handler runs, epoch is tainted, retire succeeds. Fail the native send: translated native error and unchanged epoch. Require each selected wrapper to fire. Handler-side wrappers use only AS-safe operations. |

| TSan interleaving | Acceptance oracle |
|---|---|
| Validated poll versus retire/reuse | Give the observer a **value copy of A's ID only** and assign A a nonzero epoch distinct from B's pristine one. Barrier-coordinate polls while live, racing close/reuse and after synchronized reuse. Concurrent results are A's epoch or NOENT with untouched output. A poll following completed invalidation is always NOENT, including while the slot is free. B reads zero. No stale observer dereferences either map. |
| Poll versus delivery | Hold a real backing reference. Pause the delivering thread inside wrapped fixed remap using preallocated pipe handshakes. The pre-fault poll is pristine, the paused-handler poll sees the increment before any mapping replacement and the final poll remains tainted. |
| Handler versus serialized retirees | In native-trigger fixtures keep test mappings owned through the experiment. Pause an admitted handler at fixed remap, start owner retire and observe published generation become absent while retire is draining. Interpose `sem_wait` / `sem_post` with AS-safe instrumentation to force the final post on either side of wait entry. Start a second owner retiring another slot. It must neither steal the first completion nor free an admitted slot early. Where both drain, require one consumed post per positive-count close; where the second has zero handlers, it consumes none. After both retires re-register and fault both mappings independently. |
| Many readers on one slot | Independent retained backing tokens and multiple borrowers of a retained snapshot poll one registration while another thread triggers it. All see its taint after synchronization. Drop the file handle and original snapshot while an index token remains; the mapping still recovers. The last backing release retires exactly once and the saved ID then gives NOENT. |
| Exhaustion and first install | Race first registrations at a barrier. Count exactly one installing swap, distinct simultaneously live IDs, exactly capacity successes for capacity plus one held disjoint requests, and one NOSPC. Join before retiring as in v1 `case_fault`. After one final release, retry the loser and require a fresh generation. |

Test the integration through its actual entry points. A bound index must become inexact after its source revision is released and the backing faults. Reload and stale build completion must never install old results. Find must cancel without emitting zero-derived hits. Block a running job, cancel it, then release it to finish; assert its backing survives to physical acknowledgement and is retired exactly once. For the finalizer invariant use a dedicated wrapper at the retirement boundary to report unexpected failure: the assertion-enabled child trips the invariant; the release child observes zero unmaps/frees of that backing and zero scheduled retries. Exercise private and flat sources too. Require clean TSan evidence and a native SIGBUS/death oracle. The Law 2 release guard must be active with zero allocations over snapshot take/retain, token retain/poll/drop and foreground queries. Verify the L1 include DAG and whole-archive link against L0.

## Review delta and PLAN corrections

* **Interface.** The earlier header block contained 21 nonblank interface lines, the block above contains 10 (M, count preprocessor and declaration lines inside the respective fences). Removed 11: four for the lease enum, four for the lease struct, one initializer and two pin/unpin declarations. Public functions fall from six to four; poll now accepts an ID by value.
* **States and waits.** Removed three enum values (M, EMPTY and HELD from the public lease FSM and EXHAUSTED from the slot FSM). Four slot phases remain. Removed the persistent OS reference count and its BUSY/RANGE cases. Replaced 256 per-slot semaphores with one service semaphore, removing 255 wait objects (P, table capacity minus the one serialized drain).
* **Tests.** The earlier acceptance tables had seven single-thread and five TSan rows. These have six and five (M, count named table cases): one fewer named case and one fewer test archive. The removed case used reduced ceilings. Pin-versus-retire is replaced by validated poll versus reuse. Double unpin, pin into HELD, pin-count overflow, BUSY retry and partial per-slot semaphore initialization cease to be assertions. The stale-ID and physical-lifetime oracles stay; the finalizer invariant now checks the release-build leak explicitly.
* **PLAN §4.1.** The pin/unpin API named there was proposed v2 work. `src/os/os_posix.c:os_fault_register` returns only a bare slot in v1. The smaller API delivers §4.1's lifetime property through generic mapping ownership and a nonrepeating validated identity. Rejecting intersecting extents with EXIST is accepted: v1 credited only the first matching slot, which could leave a duplicate falsely pristine.
* **Snapshot budget.** The 1,920 B per live-snapshot edit in `src/piece/piece.h` is the frozen G10f allowance (G, MEMORY BOUND). V1 `docs/decisions/P1.4e.md`, “Unresolved frozen contracts”, shows that deeper paths exceed it before private ADD boundary storage. A sufficient reserve must account for actual path/allocator classes, distinct retained revisions and `src/piece/piece.c:checkpoint_snapshot_storage`. `src/editor/session.c:publish` allocates a leaf at PLAN's cited line and retains `saved_snapshot`; `capture` retains the in-flight `save_snapshot`. Constant-time directory sharing is also a v2 target: `src/piece/piece.c:add_store_prefix` walks chunk pointers during `piece_checkpoint_begin` at the pin.

VERDICT: Adopt one backing lifetime, generation-validated lock-free polling and one serialized handler drain. Remove persistent OS pins and BUSY retirement. Finalizer ownership violations assert and preserve the mapping in release builds.

## Sign-off

Advisor, 2026-11-22: signed off on revision 40ba37b, including (e) — the overflow comparison with ASSERT and the
existing NOSPC is the correct form, since the 10^9/s rate was (E). Plan wording corrected in
docs/decisions/wave0-plan-amendments.md A5. This file is the acceptance of the Wave 1 L0 lease bead.

## Correction (advisor, 2026-11-23, amendment A15)

Generation overflow: one comparison returning OS_E_NOSPC in every build, no ASSERT; tested at full width through a
test-build-only setter for the next generation. Unregistered fault handling (pending Astra's confirmation): if
siginfo si_code > 0 (kernel-raised, e.g. BUS_ADRERR), restore the default disposition and return so the re-executed
instruction terminates the process as it would without the service; if si_code ≤ 0 (kill/sigqueue), honour the
previous disposition (ignore for SIG_IGN, chain for a handler).


## Chain and install (A18, advisor 2026-11-23; supersedes the Chain paragraph, the Chaining test row and the
## si_code rule in the A15 correction above)

The chain has exactly one client: a callable SIGBUS handler installed before os installs — in this project the
sanitizer runtime and libFuzzer in the test lanes (os_fuzz truncates files under mappings; an unrepaired fault must reach
libFuzzer's handler or the crashing input is not saved). Header rule, one sentence: **a SIGBUS the service does not
repair terminates the process by SIGBUS, unless a callable handler was installed before the service, which is then
called** with the original sig, siginfo and ucontext, errno restored. Invariant: an unrepaired SIGBUS never returns to
the faulting instruction with nothing changed. Cut, each for want of a client: SIG_IGN emulation (a prior SIG_IGN is
treated as SIG_DFL; no si_code classification, no platform sign conventions, no MCEERR/SI_KERNEL cases — the stated
difference from "never installed" is that a sent SIGBUS terminates a process whose launcher ignored SIGBUS; a real fault
under SIG_IGN is already fatal on Linux); SA_RESETHAND emulation (one-shot state and its exchange; the header says it is
not emulated); mask and flag inheritance (the service's action is fixed: SA_SIGINFO, empty mask; any further flag needs a
named need beside it); the second cell, selector and install race (query into one cell, then one
sigaction(SIGBUS, &new, NULL); the cell is written once before the handler can run; os is the only module allowed to call
sigaction, and the runtimes install before the first registration; a later dlopen'd installer can displace the service
whatever we write). The implementation states the happens-before edge from the cell write to the handler's read on
another thread (smallest sufficient: one atomic). Evidence required (M): the action the query returns in the ASan lane
and in the fuzz lane (callable or not, flags, mask); SA_RESETHAND or a non-empty mask there is a counter-example to stop on.

Tests: default prior + stray native fault → death by SIGBUS; IGN prior, truncate then read → death by SIGBUS (alarm, exit
or abort fail); prior callable handler, both signatures → called with the original arguments; failed fixed remap →
prior handler when one exists, death by SIGBUS otherwise; a repaired fault does not call the prior handler.

**Memory orders (A18):** seq_cst everywhere in the fault service, no `_explicit` forms. On x86-64 a seq_cst load is the
same instruction as a weaker load and a seq_cst RMW the same locked instruction; only plain stores differ (xchg for mov),
and none is in poll (P). A weaker order needs a proof and a measured need; the default needs neither. The poll fence
stays: the mapped byte loads are not atomic, so the closing validation needs a barrier compiler and machine both respect.
A weak-memory litmus for the poll fence is an obligation of the bead that adds a weakly ordered target.


## Epoch width (advisor ruling, 2026-11-23; supersedes "saturated at UINT64_MAX" in the poll row, "saturation
## cannot restore pristine" in Read bytes, "still saturates by checked CAS" in Transient count width, and "until
## successful or saturated" in the handler paragraph)

The epoch does not saturate: saturation never kept the invariant (stuck at the maximum, a later recovery is invisible
to a reader comparing epochs). A bare increment that could wrap to 0 would be worse. So: plain increment, and the
handler ASSERTs that the value before the increment is not UINT64_MAX (ASSERT is async-signal-safe). Bound (P):
recoveries ≤ pages × threads, since a recovered page never faults again; 2^35 × 2^22 = 2^57 with a 2^47-byte user
address space (4-level paging) and PID_MAX_LIMIT 2^22; with 5-level paging 2^44 × 2^22 = 2^66, which does not fit —
hence the ASSERT rather than an argument. One comparison, no hook, no silent wrong answer on any machine.
