# v2-0fv.20 · shipped code reads no test environment variable; dead os exports deleted

Rule (advisor): shipped code reads no environment variable that exists for tests, and an
export with no caller outside its module and no filed bead naming it is deleted. Test
isolation goes through a link-time seam, like the peer check of v2-0fv.13, or through a
variable the product reads anyway.

## 1. ipc: `EDIT_IPC_NAMESPACE` and `EDIT_IPC_FALLBACK_DIR` removed

`src/ipc/ipc_posix.c` read two test hooks through `os_env_get`. They are gone:

- The abstract name is `<app-id>-<uid><display-suffix>`.
- The squatter fallback is `/run/user/<uid>`.

`XDG_RUNTIME_DIR` is the only environment variable ipc reads.

**Invariant.** A shipped endpoint is a function of `EDIT_APP_ID`, the real uid, the
display argument, and the runtime directory: the explicit argument or `XDG_RUNTIME_DIR`.
No other variable moves where ipc listens or connects. Isolation is a property of the
test link alone. In a link with `IPC_SEAM`, every abstract name gets a `-<namespace>`
tail at `os_listen_local` and `os_connect_local`, and `/run/user/<uid>` opens a
test-owned directory instead.

**Choice: one seam header, `tests/ipc_seam.h`.** It replaces `tests/ipc_peer_seam.h`.
The Makefile's `IPC_SEAM` wraps four functions for `ipc_test` (rel, san, tsan) and for
`ipc_fuzz`:

- `os_stream_peer_uid`: unchanged from v2-0fv.13.
- `os_listen_local` and `os_connect_local`: they append `-<ipc_seam_namespace>` to
  abstract names. The namespace is the basename of the suite's mkdtemp directory, or
  `fuzz-<pid>` in the fuzzer.
- `os_dir_open`: it opens `ipc_seam_fallback` when asked for exactly `/run/user/<uid>`.

Filesystem endpoints still use `XDG_RUNTIME_DIR` or an explicit runtime directory
(`tests/ipc_isolation.h`). The product reads both anyway.

The seam appends to the name rather than inserting into it, so the name ipc derives
stays visible as the bound prefix. `endpoint_validation` and `t_namespace` still compare
the bound address byte for byte with `EDIT_APP_ID "-<uid>-<namespace>"`. The derivation
is therefore still tested.

**Simplest alternative: `XDG_RUNTIME_DIR` only, with no seam.** A counter-example
defeats it. The abstract endpoint exists only when `XDG_RUNTIME_DIR` is unset, so that
variable cannot isolate it. Without the seam, `endpoint_validation` binds
`sublimite-v2-<uid>` in the network namespace it shares with the live v2 editor and
with every other suite. Under `ipc_test --parallel 4`, three of the four processes get
`IPC_EXISTS` from the first, and a running editor would receive the test's open
requests. The fallback is just as bad: it would be the session's real
`/run/user/<uid>`. Dropping the abstract and squatter cases instead would leave the
name derivation and the fallback untested.

**Second alternative: a private user and network namespace per suite
(`unshare(CLONE_NEWUSER|CLONE_NEWNET)`), with no endpoint seam.** A counter-example
defeats it too. A network namespace scopes abstract names but not the fallback, which
is a filesystem path. Isolating that also needs a mount namespace and a bind mount over
`/run/user/<uid>`. That is more code than the wrap. It also returns `EPERM` wherever
`kernel.apparmor_restrict_unprivileged_userns=1`, which is the Ubuntu 24.04 default.
This box reads `0` (M, `/proc/sys/kernel/apparmor_restrict_unprivileged_userns`), so a
lane built on it would pass here and fail on a stock install. The peer seam is needed
in any case.

**What went with the hooks.** These `ipc_test` assertions tested only the hook:

- An empty namespace gives `IPC_INVALID`.
- An overlong namespace gives `IPC_LIMIT`.
- The hook ignores filesystem endpoints.

The bound on display-name length is still checked by `t_display`. Two assertions were
added in their place. With `EDIT_IPC_NAMESPACE=stale-hook` set, the bound abstract name
is the derivation plus only the seam's tail (`endpoint_validation`). With
`EDIT_IPC_FALLBACK_DIR` pointing at a second directory, the squatter fallback still
lands in the seam's `/run/user/<uid>` stand-in, and nothing appears in the stale
directory (`t_foreign_cred`). The v1 contract sandbox (`tests/contract/`) exports both
names to v1 runs. These assertions show that a v2 binary given them ignores them.

## 2. Exports deleted: `os_proc_id`, `os_proc_self_path`, `os_env_set`

The search covered `src/`, `tests/`, `fuzz/`, `bench/` and `docs/decisions/`. No
shipped module calls any of the three. Their callers were:

| Export | Callers before | Bead naming it |
|---|---|---|
| `os_proc_id` | `tests/os_test.c` (`case_proc`, a self-check against `getpid`) | none. `v2-0fv.13.md` lists it for the wave-end deletion review, and `wave1-L0-harvest.md` inventories v1. |
| `os_proc_self_path` | `tests/os_test.c`, `tests/os_fault_test.c` (re-exec of itself), `tests/find_bench_test.c` (locates the sibling bench) | none (same two files) |
| `os_env_set` | `tests/os_test.c` only (13 calls) | none (`wave1-L0-harvest.md` inventories v1) |

Where the callers went:

- The two re-exec and locate callers read `/proc/self/exe` with `readlink` in the test.
- `os_test.c` sets its fixtures with `setenv`/`unsetenv` through a local `env_set`.
- The self-checks of the two deleted functions in `case_proc` were dropped with them.

`os_env_get` stays. It has one shipped caller, ipc's `XDG_RUNTIME_DIR`, and `os_test`
still checks its UTF-8 and range behaviour.

**Grants.** `getpid`, `readlink`, `setenv` and `unsetenv` (all `os`) are removed from
`tools/system-symbols.txt`. The deleted functions were their only importers in `src/`.

## 3. The guard: environment reads by allowlist

`tools/check_isolation.py` now rejects any `getenv`, `secure_getenv` or `os_env_get` in
`src/` whose first argument is a string literal outside `PRODUCT_ENV`. That set is
`HOME`, `TMPDIR`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_RUNTIME_DIR`. Outside
`src/os/`, the lint also rejects a name that is not a literal. os is exempt from that
second rule because it forwards its callers' literals: `os_env_get(name)` and
`os_app_dir`'s `getenv(var)`.

Fixtures in `tests/isolation/run.py` cover three cases, each with a near neighbour that
must pass:

- A test hook read through os.
- A test hook read through libc.
- A computed name.

A name joins `PRODUCT_ENV` only together with the shipped caller that needs it.

## Red and green

Both reds run the current tests and tools against the sources this bead started from.
Those copies are kept in the session scratchpad. The rebuilt red objects are in
`build/red/`.

| Run | Build | Result |
|---|---|---|
| Red, lint | New `check_isolation.py --root` on a copy of `src/` with the original `ipc_posix.c`, `os.h` and `os_posix.c` | `src/ipc/ipc_posix.c:116: error: environment read of 'EDIT_IPC_NAMESPACE'; …` and `:133: … 'EDIT_IPC_FALLBACK_DIR'; …`, `check_isolation: FAIL: 2 violation(s)`, exit 1 |
| Red, behaviour | Current `build/rel/tests/ipc_test.o` linked against the original `ipc_posix.c` (rel flags) ahead of `libcore.a`, with `IPC_SEAM` | `endpoints`: `tests/ipc_test.c:169: ASSERT failed: bound.sun_path[0]=='\0' && len==…` (the stale namespace reached the name), exit 134. `foreign_cred`: `tests/ipc_test.c:523: ASSERT failed: lstat(sp,&st)==0 && S_ISSOCK(st.st_mode)` (the fallback followed `EDIT_IPC_FALLBACK_DIR`), exit 134 |
| Green | Current tree | `check_isolation: ok (42 files under src/)`. `isolation fixtures: 50/50 passed`. `build/rel/tests/ipc_test endpoints` and `foreign_cred`: exit 0 |

Acceptance, Mon 23 Nov 2026, [AC] (BAT0 `Charging`), load average about 1.5 (M):

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:130-:139`: `check: ok`, exit 0, 22:52–23:01. This includes `check_symbols: ok (6 archives plus LDLIBS)` with the four grants removed.
- `nice make -j2 check-tsan`: `check-tsan: 9 threaded suites passed`, exit 0, with no ThreadSanitizer warnings.
- `nice make -j2 fuzz-smoke`: `fuzz-smoke: 10 fuzzers replayed their seeds`, exit 0. `ipc_fuzz` is linked with `IPC_SEAM`.

## Shipped-code evidence (`build/v2-0fv.20-evidence/shipped.log`)

"Before" means the original sources compiled with the same release flags.

```text
strings -a <f> | grep -c EDIT_IPC_
  before ipc_posix.o: 2   build/rel/src/ipc/ipc_posix.o: 0   libcore.a: 0   libtext.a: 0   build/sublimite-v2: 0
nm -P <f> | grep -cE '^(os_proc_id|os_proc_self_path|os_env_set) '
  before os_posix.o: 3    build/rel/src/os/os_posix.o: 0     libcore.a: 0   build/sublimite-v2: 0
nm -u -P os_posix.o, imports among getpid readlink setenv unsetenv
  before: getpid readlink setenv unsetenv   after: none
nm -u -P build/rel/src/ipc/ipc_posix.o | grep os_env      os_env_get U
nm build/sublimite-v2 | grep -c '__wrap_\|__real_'        0
nm build/rel/tests/ipc_test | grep ' T __wrap_'           os_connect_local os_dir_open os_listen_local os_stream_peer_uid
```

The 6 `EDIT_IPC_` strings left in `build/rel/tests/ipc_test` are the test's own
inertness probes, which set the stale v1 names. No shipped object contains one.
