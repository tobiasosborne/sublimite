# v2-0fv.2 · Mapped-file fault service

This bead implements docs/decisions/wave1-os-fault-lease.md in L0 `os`:

- the spec as signed at 40ba37b;
- its corrections A15 (one ASSERT; exhaustion as a return code) and A18
  ("Chain and install", seq_cst throughout);
- PLAN §4.1 as amended by wave0-plan-amendments.md A5.

Round 1 is commit 6d8ffc3. Round 2 applies the advisor's rulings A and B
and Astra's review docs/reviews/v2-0fv.2-astra.md: must-fixes 2, 3 and 5,
should-fixes 11, and notes 7, 8 and 9. Its must-fixes 1, 4 and 6 are
superseded by A18. v1 `6fec2e8` is evidence, not a template.

## Acceptance

**`src/os/os.h`**

- The spec's fault subsection: `OS_FAULT_SLOTS`, `os_fault_id`,
  `OS_FAULT_ID_INIT` and the four entries.
- A18's chain rule in one sentence: a SIGBUS the service does not repair
  terminates the process by SIGBUS, unless a callable handler was
  installed before the service, which is then called. It is called with
  the original signal, siginfo and ucontext, errno restored. Its
  SA_RESETHAND is not emulated, and a prior SIG_IGN counts as SIG_DFL.
- The tree's one `ASSERT` (A15). It is always compiled in. A failure
  writes `file:line: ASSERT failed: expr` with `write(2)` and calls
  `abort()`. Both calls are async-signal-safe, so the handler may assert.
  Migrating base, trace, work and ipc to it is v2-0fv.14.

**`src/os/os_posix.c`** holds the service:

- generation-validated ids from one issuance counter;
- lock-free validated poll, behind a fence;
- owner-only retire with one shared drain semaphore and an EINTR retry;
- the A18 chain: one prior cell and one atomic;
- generation exhaustion returns `OS_E_NOSPC` in every build;
- every atomic operation is seq_cst, with no `_explicit` forms;
- the test-only generation setter `os_fault_test_next_gen`, compiled only
  under `EDIT_TEST_HOOKS`.

**Suites**

- `tests/os_fault_test.c` runs in check-san and check-tsan. It has 25
  cases, each in a fresh self-exec child, linked with
  `-Wl,--wrap=mmap,sigaction,sysconf,syscall,sem_init,sem_destroy,sem_wait,sem_post`.
  Its oracles fail through `ASSERT`.
- `tests/law2_os_fault_test.c` runs in check-law2 with the shipped release
  settings. Inside one live-guard window it covers install, polls, a
  repaired fault, a stale poll, retire, and exhaustion at full width
  (`UINT64_MAX`, then NOSPC). It requires `active && count == 0`.
- `fuzz/os_fuzz.c` runs in fuzz-smoke. It is a model-based fuzzer over the
  registry (Law 1), and its oracles are `ASSERT`.

**Build**

- `Makefile` `CFG_TEST_HOOKS` builds `%_test_hooks.o` from a module source
  with `-DEDIT_TEST_HOOKS`. The three test link rules link those
  prerequisites ahead of the archives.
- `tests/lanes.mk` attaches `src/os/os_posix_test_hooks.o` to the two fault
  suites and keeps the per-suite `LDFLAGS_<stem>` hook for `--wrap`.
- The test-hooks object does not enter any archive. `nm` (M) shows the
  setter in `build/rel/src/os/os_posix_test_hooks.o` only, and absent from
  `build/rel/libcore.a` and the shipped `build/sublimite-v2`.

**Grants.** `tools/system-symbols.txt` gives these to `os` only, one line
each with a reason: `abort` (ASSERT), `gettid`, `raise`, `sem_destroy`,
`sem_init`, `sem_post`, `sem_wait`, `sigaction`, `sigemptyset` and
`syscall`. Round 1's `_Exit` grant is removed. `nm -u` (M) shows `abort`
imported only by `libcore.a:os_posix.o` and, as already granted, by the
vendored tree-sitter in `libui.a`.

## Evidence before the A18 cut (M)

The probe `build/fault-evidence/query/probe.c` runs
`sigaction(SIGBUS, NULL, &q)`. Each build uses that lane's exact flags.
For the fuzz lane the query runs inside `LLVMFuzzerTestOneInput`.

| Lane | Query result |
|---|---|
| ASan + UBSan (`-fsanitize=address,undefined`) | callable, flags `0x4c000004` = SA_SIGINFO, SA_NODEFER, SA_ONSTACK, SA_RESTORER; mask empty |
| fuzz (`-fsanitize=fuzzer,address,undefined`) | callable, flags `0x4c000004`, as above; mask empty |
| TSan | callable, flags `0x4c000004`, as above; mask empty |
| release (gcc -O2) | SIG_DFL, flags 0, mask empty |

None of them carries SA_RESETHAND or a non-empty mask, so there is no
counter-example to the cuts. The service's fixed action does not carry the
runtimes' SA_ONSTACK or SA_NODEFER. A runtime handler that the service
calls therefore runs on the faulting thread's stack with SIGBUS blocked.
SIGBUS here is never stack exhaustion; that arrives as SIGSEGV, which the
service does not install.

## State, invariant and spec section

All of this state lives in `os_fault_service`, the one PLAN-named global.

| State | Invariant | Spec |
|---|---|---|
| `slots[i].control`: phase `SLOT_FREE`, `SLOT_LIVE` or `SLOT_RETIRING` in two bits, plus 4 × admitted handlers | Only the mutex holder changes the phase. Handlers add 4 only in LIVE and remove it in LIVE or RETIRING. FREE has no handlers. RETIRING exists only while its retiring owner holds the mutex. | State and lifetime (RESERVED deleted, review 5); Transient count width |
| `slots[i].gen` | The issued generation from publication (register's last store) to invalidation (retire's first store), else 0. Each value is issued once. | Publication, validation and identity proof |
| `slots[i].epoch` | Recoveries of the current registration, saturating. Only register resets it, while gen is 0 and no handler is admitted. | Publication; Recover |
| `slots[i].base`, `.len` | Written by the registering owner while the slot is FREE. A handler reads them only after its admission, so it reads the registration it entered. | Recover |
| `mu` | Serializes register and retire: scan, claim, close, wait and FREE. Poll and the handler never take it. | Drain |
| `issued` | The last issued generation over all slots, under `mu`. At `UINT64_MAX` registration returns NOSPC in every build. The test setter only moves it forward. | Generation width; A15 |
| `once`, `install` (`FAULT_COLD`, `FAULT_READY`, `FAULT_UNAVAILABLE`) | Written once inside the once routine and read after it returns. UNAVAILABLE is sticky. | Install |
| `drain` (`sem_t`) | 0 whenever `mu` is free. It is at most 1, and only between the last departure from a closed slot and its owner's wait. | Drain |
| `page` | A power of two, set before the handler is installed. | Install; Recover |
| `prior` (one `struct sigaction`) | Written once, by the query, before `prior_kind` is stored. | A18 |
| `prior_kind` (`PRIOR_NONE`, `PRIOR_HANDLER`, `PRIOR_SIGINFO`) | Stored after `prior` is written and before the installing `sigaction`. It is the happens-before edge from the cell write to the handler's read on any thread: the handler reads `prior` only after loading `prior_kind`. SIG_DFL and SIG_IGN both map to NONE. | A18 |

Round 1's previous-action pair, selector and one-shot state are deleted,
along with `SLOT_RESERVED` and the `fault_result` enum; the handler now
uses a bool.

## Handler and memory orders

- **Handler calls:** `mmap` (glibc async-signal-safe; the audited target,
  Linux x86-64 with glibc, is named in one comment, with no runtime libc
  NOTSUP path), `sem_post`, `sigemptyset`, `sigaction` and `raise`, plus,
  on a broken invariant, ASSERT's `write` and `abort`. It touches only
  lock-free atomics checked at installation. It restores errno on every
  path.
- **Unrepaired SIGBUS:**
  - with `PRIOR_HANDLER` or `PRIOR_SIGINFO`, the prior handler is called
    with the original arguments and errno restored;
  - with `PRIOR_NONE`, the handler installs SIG_DFL and raises SIGBUS.
    SIGBUS stays blocked until the handler returns, then the pending
    signal kills; a repeated real fault would also kill. The calls sit
    inside an ASSERT, because they cannot fail with these arguments.
- **Memory orders:** seq_cst everywhere, with no `_explicit` forms. The
  file says, in one sentence, that a weaker order needs a proof and a
  measured need. The per-order pairing comments are deleted. The
  invariant comments stay: what is read only after admission, and what
  the close observes.
- **The poll fence** stays, with the sentence "the mapped byte loads are
  not atomic, so the closing validation needs a barrier compiler and
  machine both respect".
- **Count width:** `_Static_assert`s on pointer width and
  `sizeof(siginfo_t) > 8` back the 62-bit count.

## Round 2 red and green

Box: [bat] (`/sys/class/power_supply/BAT0/status` reads Discharging). Load
average 2.21 to 3.32 (M, `uptime`).

| Change | Red | Green |
|---|---|---|
| A18 chain cut | New `chain_ignore` (IGN prior, truncate, read) against the round-1 code: `os_fault_test: chain_ignore child died by signal 14`, the alarm after an endless re-fault. New `err_remap_default` (no prior, failed remap) already died by SIGBUS on round 1. | Both die by SIGBUS. `chain_simple` and `chain_siginfo` see the original arguments and errno restored; repaired faults leave the prior uncalled. |
| One ASSERT (review 3) | `make -s build/rel/tests/os_fault_test build/rel/tests/law2_os_fault_test` fails with `implicit declaration of function ‘ASSERT’` | The `assert` case's child wrote `tests/os_fault_test.c:622: ASSERT failed: prior_calls == 1` and died by SIGABRT, in rel, san and tsan |
| NOSPC at full width (review 2) | Link: `undefined reference to os_fault_test_next_gen`. With the hook added and round 1's exhaustion code kept: `fault_claim: Assertion 'false && "fault generations exhausted"' failed`, exit 134, both in `exhaustion` and in the law2 window | `exhaustion` passes in rel, san and tsan, and the law2 window passes in rel: generations `UINT64_MAX − 1` and `UINT64_MAX`, then NOSPC with absent output even with a freed slot; issued ids still poll, repair and retire |
| seq_cst departure (ruling B) | TSan mutant with a relaxed departure: `race_departure` exits 66, `WARNING: ThreadSanitizer: data race` | The unmutated TSan build of `race_departure` exits 0 |
| EINTR retry (review 3) | A mutant without the retry: `race_retire` exits 134, `os_posix.c:1131: ASSERT failed: rc == 0` | `race_retire` takes one injected EINTR, then consumes the owed post |
| Observer poll after the reuse handoff (note 7) | Bare-index-poll mutant: `race_reuse` exits 134 at the observer oracle | NOENT with the output untouched |
| Disposition oracle checks the mask (should-fix) | A mutant that restores the prior with an empty mask after a failed install: `err_install` exits 134, `ASSERT failed: same_action(&before, &after)` | Handler, flags and every mask signal compare equal |
| Mutants carried over from round 1 | All of these still fail: post on a live departure (`race_retire`), remap before taint (`race_delivery`), bare-index poll (`stale`), ASSERT on exhaustion (`exhaustion`) | |
| `SLOT_RESERVED` deleted (review 5) | None: a refactor with no observable behavior (the review's own argument) | All cases green |
| Law-2 should-fixes | None: fixture restructuring. The positive control saves success before `free`, and a failed step returns before `os_unmap`. | `guard positive control: active=1 count=1` and `fault service Law-2 window: active=1 count=0 ok=1 epochs 0->1` |

Mutants live under `build/fault-evidence/mutation2/`. The script is
`build/fault-evidence/mutate2.sh`: a test-hooks build with the suite's
wraps, release or TSan.

**Acceptance**, all exit 0:

```sh
make all
make check
make check-tsan
make fuzz-smoke
```

```text
check-layers: 6 isolated layer links passed (L0-L4 tests, L5 via main.o)
check-san: 9 test binaries passed
check-law2: 2 release suites passed
check: ok
check-tsan: 3 threaded suites passed
fuzz-smoke: 2 fuzzers replayed their seeds
```

**Stability** [bat]: five sequential runs each of rel, san and tsan
`os_fault_test`, 0 failures.

**Line counts** (M, `git diff --numstat 6d8ffc3`):

- `src/os/os_posix.c`: +147, −171, from 1281 to 1257 lines. The fault
  section shrank from 379 to 334 lines, and the 22-line ASSERT facility
  was added.
- `tests/os_fault_test.c`: +252, −182, from 1205 to 1275 lines. Ignoring
  the mechanical CHECK→ASSERT rename it is +157, −87.
  - Added: `assert`, `exhaustion`, `race_departure` and
    `err_remap_default`, the EINTR step, the observer handoff and the mask
    oracle.
  - Deleted: `chain_oneshot`, the mask and flag inheritance checks, and
    the IGN-sigqueue case.

## Acceptance rows and cases

| Spec row | Case(s) |
|---|---|
| Lifetime and stale ID | `stale` |
| Arguments and overlap | `args` |
| Capacity | `capacity` |
| Generation width (A15) | `exhaustion`, plus the law2 window in release |
| Recovery | `recover`, `truncate` |
| Chain (A18) | `chain_default` (DFL + stray native fault → SIGBUS); `chain_ignore` (IGN + truncate/read → SIGBUS; alarm, exit or abort fail); `chain_simple` and `chain_siginfo` (both signatures, original arguments, errno; repaired faults do not call the prior; `BUS_ADRALN` at a registered address reaches the prior unrepaired); `err_remap` (failed remap → prior); `err_remap_default` (failed remap, no prior → SIGBUS) |
| OS errors | `err_pagesize`, `err_sem`, `err_query`, `err_install` (sticky NOTSUP, absent output, disposition unchanged in handler, flags and mask, each wrapper fired); `err_send` |
| ASSERT (A15) | `assert` |
| Validated poll versus retire/reuse | `race_reuse`, with the observer's own poll after the reuse handoff |
| Poll versus delivery | `race_delivery` |
| Handler versus serialized retirees | `race_retire`: four scenarios, the first with one injected EINTR |
| Departure ordering (ruling B) | `race_departure`: LIVE departure, zero-count close, reuse before the join; a relaxed flag is the only other link |
| Admission ordering | `race_admission` |
| Many readers | `readers` |
| Exhaustion and first install | `install_race` (exactly one installing `sigaction`) |

## Findings and obligations

1. **Correction of round 1's A15 wording.** Round 1 said "release builds
   keep the mapping". That is wrong under A15. On an impossible finalizer
   ownership violation, every build ASSERTs before unmapping, freeing or
   scheduling a retry. The finalizer belongs to the owning layer (L2
   `file`); L0 cannot detect a fabricated last reference. The L2 finalizer
   tests and the piece, lineidx, find and reload integration rows are not
   this round.
2. **Weak-memory litmus for the poll fence** (A18). This is an obligation
   of the bead that adds a weakly ordered target. On x86-64 no mutant
   distinguishes the fence.
3. **Wrapped runtime imports.** The sanitizer runtimes import `sigaction`
   and `sysconf` (M, `nm -u`), so `--wrap` routes their startup calls into
   the suite. Those two wrappers stay uninstrumented
   (`disable_sanitizer_instrumentation`), and each case zeroes its
   counters when it starts (review 10).
4. **The setter's absence is checked by `nm`, not by a lane.** `nm` shows
   the setter absent from the shipped archive and binary, but no lane
   enforces it. The export gate accepts any `os_`-prefixed symbol.
   Making absence a gate would need a forbidden-export list in
   `check_symbols.py`, which this round does not add.
5. **Unchanged from round 1.** The trigger uses `gettid()`, and its single
   raw `syscall` passes four long words, which the test wrapper forwards
   exactly. The fuzzer remains under Law 1.

## Not done here

- HANDOFF, the worklog and the bead state are the coordinator's.
- v2-0fv.14 migrates base, trace, work and ipc to the one ASSERT.
