# v2-0fv.16 · os fault service under A15: no state, code or test for a condition no input can produce

This bead starts from v2 at 11cf3af, which contains ee0705b (v2-0fv.2) and
v2-0fv.13. It applies A15 strictly to the fault service, as the
coordinator's items (a) to (e) ask:

- what an input (a file, its content, a resource, timing) can produce stays
  a return code;
- what only a caller bug or the platform ABI can produce becomes an
  `ASSERT`, and its state and tests go.

## The fault API after this bead

| Entry | Results | The input that produces each failure |
|---|---|---|
| `os_fault_register` | `OS_OK`, `OS_E_NOSPC` | NOSPC has two causes. First, all `OS_FAULT_SLOTS` registrations are live: the user has opened more mapped documents than the table holds (a resource). Second, `UINT64_MAX` generations have been issued (resource exhaustion; A15). `*out` is then the absent id. |
| `os_fault_poll` | `OS_OK`, `OS_E_NOENT` | NOENT is timing: the owner retired the registration (the document closed or reloaded, the backing's final release) while a copy of its id is still polled. An example is a worker result stamped with the id that outlives the backing. |
| `os_fault_retire` | `void` | Nothing an input produces can fail it: a valid retire always drains. |

Gone: `OS_E_NOTSUP` (item a); every `OS_E_INVAL` and `OS_E_EXIST`
(item b); `os_fault_test_trigger` and its `OS_E_INVAL` and native send
error (item c).

**Note for the piece bead.** Polling the absent id is an ASSERT, so a
private or unregistered source carries no token and is never polled.
`retire` has no result to handle.

## (a) Installation cannot fail on the audited target

| Former cause of `FAULT_UNAVAILABLE` | Why no input, file, resource or timing produces it | Now |
|---|---|---|
| An atomic not lock-free | A property of the x86-64 ABI | `_Static_assert(ATOMIC_LONG_LOCK_FREE == 2 && sizeof(uint64_t) == sizeof(long))`, and the same with `ATOMIC_INT_LOCK_FREE` for the `fault_prior` enum |
| A page size that is not a positive power of two | Kernel ABI (`AT_PAGESZ`) | `ASSERT` on `os_page_size()`. The service now uses the same source as `os_map_granularity`, so `sysconf` drops out. |
| `sem_init(&s, 0, 0)` failing | glibc's process-private `sem_init` fails only for a value above `SEM_VALUE_MAX`, and the value here is 0 | `ASSERT` |
| `sigaction(SIGBUS, …)` with valid pointers failing | The kernel returns EINVAL only for a bad signal number, SIGKILL or SIGSTOP, and EFAULT only for a bad pointer. Only a seccomp filter that returns an error for `rt_sigaction` produces a failure. In such an environment glibc's `posix_spawn` child, which resets handlers with sigaction, cannot work either, so `os_proc_spawn` already fails there. No target environment of this editor installs such a filter. | `ASSERT`, both the query and the install |

Deleted with them:

- the `fault_install_state` enum (`FAULT_COLD`, `FAULT_READY`,
  `FAULT_UNAVAILABLE`), the `install` field and the sticky NOTSUP;
- `sem_destroy` and its cleanup path;
- the wraps for `sigaction`, `sysconf`, `sem_init` and `sem_destroy`, the
  four `err_*` injection cases, and the sanitizer startup-wrapper
  exception (`disable_sanitizer_instrumentation`), which existed only
  because the runtimes import `sigaction` and `sysconf`.

The remaining wraps are `mmap` (the recovery remap fails with ENOMEM at the
map-count limit, a resource) and `sem_wait`/`sem_post` (EINTR is timing;
the post/wait order is the drain under test). No sanitizer runtime imports
these three (M, round 2 `nm -u`), so no exception remains.

## (b) Every former INVAL and EXIST, classified

All of these are caller bugs, now ASSERTs, and their return-code tests are
deleted:

- **register:**
  - a null `out` or `view`, a null address or zero length (not a mapped
    view);
  - an unaligned base, or an end past the address space (`os_map_file`
    returns neither);
  - an intersecting page-rounded extent. Live mappings never share pages,
    so an intersection means a view registered twice, or unmapped before
    its retire and the range reused.
- **poll:** a null `epoch`, the absent id (gen 0), or a slot index outside
  the table (forged).
- **retire:** gen 0 or an out-of-table slot. A stale or second retire is
  also an ASSERT: only the owner retires, once, at final release.

The impossible-state ASSERTs of round 2 stay: RETIRING seen under the
mutex, a claimed slot not FREE, a close of a non-LIVE slot, a non-EINTR
`sem_wait` error, the final observation not RETIRING, and a failed
`sem_post`.

**Also cut, in the bead's spirit: epoch saturation.** (P) The epoch cannot
wrap.

- A recovery turns the faulting page through the view's end into
  anonymous zero pages, which never fault. So each increment is one
  thread's fault on a still file-backed page.
- Pages: at most 2^35 (a 2^47-byte user address space of pages of at
  least 2^12 bytes).
- Threads: at most 2^22 (the kernel's `PID_MAX_LIMIT`).
- Bound: 2^35 × 2^22 = 2^57 < 2^64.
- Another process cannot forge a counted fault: Linux refuses a
  nonnegative `si_code` sent to another process.

The checked-CAS loop became one `atomic_fetch_add`, and the header no longer
says "saturates". The signed spec's "Generation width" item says the epoch
"still saturates by checked CAS". That item is now contradicted by this
derivation, so it is open for the advisor (finding 2).

## (c) No trigger: every test fault is real

`os_fault_test_trigger` is deleted from `os.h` and `os_posix.c`, together
with its `syscall` and `gettid` grants and the `syscall` wrap. Every fault
in `tests/os_fault_test.c`, `tests/law2_os_fault_test.c` and
`fuzz/os_fuzz.c` is now real: the file shrinks under the view (or is empty
from the start) and a read faults.

- The Law-2 window now covers a real fault and its recovery.
- Law 2 and the fuzzer map an unnamed `O_TMPFILE` under `build/` instead
  of `/proc/self/exe`, which cannot be truncated.
- The fuzzer remaps per input and models the recovered pages.

**Named exception.** `chain_simple` sends two signals the test itself
forges: a device I/O error (`BUS_OBJERR`, repaired) and a sigqueue
(`SI_QUEUE`, unrepaired, forwarded). A truncation cannot produce either.
They need no hook, because the test sends them with its own
`rt_tgsigqueueinfo`, so nothing moves to the hooks object.

`install_race` loses its `sigaction` counter. Its single-installation
oracle is now observable:

- a prior `SA_SIGINFO` handler is installed before the race;
- after it, an unrepaired real fault must reach that handler exactly once;
- after a second installation, the prior would be the service itself, and
  the fault would recurse instead.

## (d) No shipped object defines `*_test_*`

`tools/check_symbols.py` gains a `[test]` rule. It checks every exported
definition in every release archive member and in `main.o`, which are the
inputs of the shipped binary. The fixture `tests/symbols/test_export`
exports `os_test_probe` from a shipped L0 archive and must be rejected.

There is no exception. The setter `os_fault_test_next_gen` exists only in
the hooks variant. `nm` (M) shows 0 `*_test_*` definitions in
`build/sublimite-v2`. The setter appears only in `build/rel/tests/os_fault_test`,
`build/san/tests/os_fault_test`, `build/tsan/tests/os_fault_test` and
`build/rel/tests/law2_os_fault_test`.

## (e) One macro prefix

**Choice: a macro takes its module's upper-case prefix, as an exported
symbol takes `module_` (`OS_`, `BASE_`).** A separate tree-wide macro
prefix would be a second naming scheme beside the module one. `EDIT_` is
v1's prefix and is also v2's `edit` module's own prefix (`EDIT_H` is
`src/edit/edit.h`'s guard), so it collides by construction.

- A test hook compiles under `<MODULE>_TEST_HOOKS`. `Makefile`
  `CFG_TEST_HOOKS` derives the module from `src/<module>/…`.
- The variant is `<file>_test_hooks.o`.
- `tests/lanes.mk` links it only into the suites that name it.
- CLAUDE.md's code conventions carry this as one line.
- Renamed in os: `EDIT_TEST_HOOKS` became `OS_TEST_HOOKS`, and `os.h`'s
  guard `EDIT_OS_H` became `OS_OS_H`.
- Not renamed (other modules, finding 1): `EDIT_APP_ID`, `EDIT_IPC_H` and
  ipc's `EDIT_IPC_*` environment variables.

## Red and green

The box was charging, [AC]. Load average 1.35 / 3.27 / 4.54 (M, `uptime`)
at the acceptance runs.

| Change | Red | Green |
|---|---|---|
| (d) `[test]` rule | `make -s check-symbols`: `test_export: expected exit 1 and ('[test]', 'os_test_probe'), got 0` | `test_export: rejected as expected` |
| (c) delete the trigger | With the rule: `build/rel/libcore.a(src/os/os_posix.o): error: [test] os ships test symbol os_fault_test_trigger` | `check_symbols: ok (6 archives plus LDLIBS)`. All 19 cases fault for real, in rel, san and tsan. |
| (e) `OS_TEST_HOOKS` | The hooks variant built under `-DOS_TEST_HOOKS` against the old `#ifdef EDIT_TEST_HOOKS`: `undefined reference to 'os_fault_test_next_gen'` | Links; setter present only in the four suite binaries |
| (a), (b), epoch cut | No red: these delete code no input reaches. The remaining oracles stay green. | |

**Mutants of the new code** (`build/fault-evidence/mutation3/`, built with
`OS_TEST_HOOKS` and the three wraps). These show that the real-fault tests
keep their teeth:

| Mutant | Case | Result |
|---|---|---|
| no repair | `recover`, `stale` | exit 135: SIGBUS kills |
| no taint before the remap | `race_delivery`, `recover` | ASSERT on `epoch_of(id) == 1` |
| bare-index poll | `stale`, `race_reuse` | ASSERT on the NOENT oracle |
| no EINTR retry | `race_retire` | `ASSERT failed: rc == 0` |
| relaxed departure (TSan) | `race_departure` | exit 66, data race |
| relaxed admission, load and CAS (TSan) | `race_admission` | exit 66, data race |

One discarded attempt: a relaxed-CAS mutant that kept the seq_cst load was
not caught, because that load already supplies the acquire.

**Acceptance**, all exit 0:

```sh
make all
make check
make check-tsan
make fuzz-smoke
```

```text
test_export: rejected as expected
check-layers: 6 isolated layer links passed (L0-L4 tests, L5 via main.o)
check-san: 17 test binaries passed
law2_os_fault_test: ok
check-law2: 4 release suites passed
check: ok
check-tsan: 6 threaded suites passed
fuzz-smoke: 8 fuzzers replayed their seeds
```

The Law-2 window, now over a real fault:

```text
guard positive control: active=1 count=1
fault service Law-2 window: active=1 count=0 ok=1 epochs 0->1
```

Exploratory fuzzing: `build/fuzz/os_fuzz -runs=50000`, 50,000 runs with no
model violation. This is local evidence; the lane replays the seeds.

## Lines and grants

**Lines (M, `diff` against the files as synced from 11cf3af):**

- `src/os/os_posix.c`: +64, −84 (1656 → 1636). The fault section shrinks
  from 345 to 326 lines.
- `src/os/os.h`: +21, −30.
- `tests/os_fault_test.c`: +217, −437 (1275 → 1055).
- The rest is small:
  - `Makefile`: the hooks rule derives the macro.
  - `tests/lanes.mk`: three wraps instead of eight.
  - `tools/check_symbols.py`: the `[test]` rule.
  - `tests/symbols/run.py`: one case, plus the 3-line fixture.
  - CLAUDE.md: one line.

**Grants:** 98 `os` grants, down from 101 (M, `SYMBOL os #` lines in
`tools/system-symbols.txt`). Removed: `syscall`, `gettid`, `sem_destroy`.

## Findings (reported, not fixed: Law 3)

1. **The same disease in ipc.** `src/ipc/ipc_posix.c` reads three test
   hooks from environment variables in the shipped binary:
   `EDIT_IPC_TEST_PEER_UID` (it overrides the expected peer uid),
   `EDIT_IPC_NAMESPACE` and `EDIT_IPC_FALLBACK_DIR`. This is a test hook in
   the shipped API and binary, like (c). The `[test]` symbol rule cannot
   see it because it is a string, not a symbol. It also carries the old
   `EDIT_` prefix.
2. **The spec's "Generation width" item still says the epoch saturates.**
   This bead removed saturation with the (P) derivation above. The spec
   text needs the advisor.
3. **The main checkout changed after my sync.** At 19:46,
   `/home/tobias/Projects/sublimite-v2/src/os/os_posix.c` changed outside
   the fault section: the wake, poll and spawn code, apparently another os
   bead. `Makefile` and `tests/lanes.mk` changed at 19:46 and 19:55. Its
   fault section and the fault part of `os.h` match my base, but the
   coordinator merges this bead's `os_posix.c`, `os.h`, `Makefile` and
   `tests/lanes.mk` against that change.
4. **Working-rule slip.** One Write went to
   `/home/tobias/.t3/worktrees/sublimite-v2/fuzz-os-placeholder`, outside
   the checkout: a one-byte file containing `x`. I removed that file
   immediately; nothing else outside the checkout was touched.
