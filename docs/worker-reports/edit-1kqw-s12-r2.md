# edit-1kqw round 2 continuation

Verdict: **partial**. Findings 1–9 have implementation and regression coverage.
Finding 2's native round-1 retained-client case already passed on this host, so
its requested native red is unavailable. Finding 10 has conditional production
save tests and a non-skippable integration target, but this worktree has neither
the Windows file/journal implementations nor those modules in WIN_MODULES.
Finding 11's native reload comparisons are fixed, but complete public baseline
storage still requires W2.1's identity representation. Do not close the round's
production save/full-baseline acceptance on the ordinary Windows check alone.

The interrupted session's uncommitted changes were preserved and completed in
this worktree. No main-checkout changes, state-changing git, bd, src/file edits,
os.h changes, HANDOFF.md edits or docs/worklog edits were made. The only project
files read outside this worktree were the three expressly authorized briefs/log.

## Changes and files

- `src/ipc/ipc_win.c`, `ipc.h`: lifetime-owned startup arbitration, authenticated
  generation publication, bounded readiness waiting, cross-process launcher
  handoff by argv endpoint, user-only launcher ACLs, identification SQOS and
  buffered-trailer rejection before callbacks.
- `src/ipc/args.c`, new `src/ipc/crt_win.h`: thread-local scoped UCRT validation
  guards for recoverable descriptor operations; caller handler restored.
- `src/os/os_posix.c`: retained specifically for finding 8's search-only reload
  directory and finding 9's post-create ownership. Readable references remain
  preferred; O_PATH references must reopen readably before sync/flock.
- `src/os/os_win.c`: retained specifically for finding 9. Successful exclusive
  FILE_CREATE skips fallible classification of a necessarily new regular file.
- `src/savectl/savectl.c`: retained worker-side unlink obligations and maintenance
  retry; complete native identity comparisons during Windows reload. The public
  legacy `file_id` reduction is still a dependency blocker, explicitly documented.
- `tests/savectl_test.c`: inherited unprivileged permissions, post-create metadata
  failure, unlink failure and foreign-collision ownership regressions.
- `tests/win/ipc_transport_test.c`, `ipc_win_test.c`: inherited startup/crash
  process races, actual launcher/UI processes, descriptor/security/trailer cases.
- New `tests/win/ipc_descriptor_test.c`: documented CRT callback injection plus
  real invalid descriptors, handler restoration, stdin read-error/LIMIT mode
  restoration. New `tests/win/savectl_identity_test.c`: high-word-only ID changes.
- `tests/win/savectl_test.c`, `mk/win.mk`: conditional real saves covering BUSY
  preservation/cleanup and fsynced-before-rename replacement with a mapped old
  target; `check-savectl-production` fails instead of accepting a dependency SKIP.
- `docs/decisions/edit-1kqw.md`: appended Round 2 finding/change/test table,
  exact generation-publication protocol and dependency/identity requirements.

`docs/worker-reports/edit-1kqw-review-final.md` was already untracked at entry and
was left unchanged.

## Red/green evidence

All finding numbers below are references to the review, not new measurements.
Exit statuses and quoted results are (M), [AC, desktop Spiral]. No BAT0 exists.
Load observations from `uptime`: at initial verification 13:24:45, load averages
7.80, 4.14, 3.36; at 13:33:55, 0.84, 1.92, 2.77. These are observations on a
shared desktop, not performance budgets. Timeouts reuse existing IPC/fixture
deadlines (G); no performance threshold was added.

Historical line references use the authorized interrupted-session transcript
`build/briefs/edit-1kqw-s12-r2.codex.log` in the main checkout. Its output is
interleaved where independent commands ran together. Existing `build/r2-red-*`
logs were retained; they were not overwritten by green runs.

| Finding | Red command and result | Green command and relevant result |
| --- | --- | --- |
| 1 | `tools/winrun.sh build/win/rel/tests/ipc_transport_test.exe --startup`, exit 1. Transcript lines 8034–8045: `ipc_client_send(...IPC_LOCK_TIMEOUT_MS)==IPC_OK` and contender wait assertion fail. | Same selector, exit 0: `startup publication/handoff race: ok`, `startup abandoned initializer takeover: ok`; also normal release/sanitizer suite. |
| 2 | `tools/winrun.sh build/win/rel/tests/ipc_transport_test.exe --orphan`, **exit 0 against round 1**, transcript 8036–8043 and 8670. Native crash/name retention failure was not reproduced. | Same selector, exit 0: `crash/rebind/handoff with surviving old client: ok`. Replacement uses a fresh generation, independent of old client handles. This is coverage and a protocol change, not a fabricated red. |
| 3 | `make TARGET=win -j64 build/win/rel/tests/ipc_win_test.exe`, exit 2. Transcript 9421–9440; `r2-red-launcher.log`: missing `ipc_server_connect_wait` and `ipc_launcher_prepare`. | `tools/winrun.sh build/win/rel/tests/ipc_win_test.exe --launcher-cases`, exit 0: git-style launcher/UI `primary`, `isolated`, `death` cases `ok`. Historical greens at transcript 11068–11070. |
| 4 | Native invalid-descriptor children already returned errors on this host (transcript 11079 onward, 15117); no crash is claimed. Additional `tools/winrun.sh build/win/rel/tests/ipc_descriptor_round1.exe`, exit 1: `ipc descriptor validation escaped to caller's fatal handler` (`r2-red-descriptor-callback.log`). | `tools/winrun.sh build/win/rel/tests/ipc_descriptor_test.exe`, exit 0: `invalid CRT validation callback scoped; error returns and caller handler restored: ok`, plus stdin mode restoration after read failure and wire limit. Normal child cases also pass in release/sanitizer. |
| 5 | `tools/winrun.sh build/win/rel/tests/ipc_transport_test.exe --sqos`, exit 1. Transcript 11060–11100; `r2-red-sqos.log`: `client SQOS flags missing: 0x40000000`. | Same selector, exit 0: `all client opens request identification SQOS: ok`. |
| 6 | `tools/winrun.sh build/win/rel/tests/ipc_transport_test.exe --security`, exit 1. Transcript 11995–12020; `r2-red-security.log`: `acl && acl->AceCount==1` fails. | Same selector, exit 0: protected primary/launcher ACL and restricted-token read/duplex denial `ok`. This restricted token is a deterministic access-check fixture, not a claim of testing a separately logged-in user. |
| 7 | `tools/winrun.sh build/win/rel/tests/ipc_transport_test.exe --trailers`, exit 1. Transcript 11064–11100; `r2-red-trailers.log`: `c.calls==0` fails. | Same selector, exit 0: `buffered garbage/second frame rejected before callback: ok`, with protocol ACK. |
| 8 | `build/tests/savectl_test --r2-access`, exit 134. Transcript 15086–15105: `file_error=1`, `m.file_error==FILE_OK` fails. | `DISPLAY=:94 build/tests/savectl_test --r2-access`, exit 0: `file_error=0`, search/write-only source and unwritable cache `ok`; also full sanitizer suite. |
| 9 | `build/tests/savectl_test --r2-post-create` and `--r2-unlink`, each exit 134. Transcript 15088–15111: `owned temporary residue=1`, `residue==0` fails. | `DISPLAY=:94 build/tests/savectl_test --r2-post-create` and `--r2-unlink`, exit 0, `owned temporary residue=0`. `--r2-collision` was already green and remains so, preserving the foreign entry. |
| 10 | `make TARGET=win -j64 check-savectl-production`, exit 2: `savectl production save required: file/journal Windows dependencies unavailable` (`r2-red-production-save.log`). | **Blocked**. Conditional test body compiles with `-DEDIT_WIN_SAVECTL_REAL_DEPS` and all required warning flags (`build/r2-production-save-test.o`); native production save has not run. |
| 11 | `tools/winrun.sh build/win/rel/tests/savectl_identity_test.exe`, exit 1, before the native guard: `high-half identity boundary 1: file_error=0`, changed-source assertion fails (`r2-red-identity.log`). | Same command, exit 0: boundaries 1 and 2 each report `file_error=5` (FILE_ERR_CHANGED); `full Windows object identity guards reload: ok`. Public baseline storage is still incomplete. |

The additional descriptor red was built from round-1 `args.c` and `ipc_win.c`
reconstructed by reverse-applying read-only `git diff` output to copies under
`build/r2-round1`, never to tracked sources. The new test's includes point at
those copies in `build/r2-descriptor-round1.c`; it links the current OS/core
library. Its API shim invokes the documented invalid-parameter callback after
a bad descriptor result, so the test establishes handler scoping even on a UCRT
that omits that callback. It does not claim a native default-handler crash.

## Verification

Measured command results (M), [AC, desktop Spiral]; logs stay in this worktree's
ignored `build/` directory:

| Command | Result / evidence |
| --- | --- |
| `make -j64 all` | Exit 0, `build/r2-linux-all.log`. |
| `DISPLAY=:94 ASAN_OPTIONS=detect_leaks=1 make -j64 check` | Exit 0, `build/r2-linux-check.log`: `check: 104 test binaries passed`, replay CLI checks and `display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS`. Count is the check driver's printed binary count. LeakSanitizer remained enabled. |
| Rebuilt `build/san/tests/os_test`, `savectl_test`, `savectl_reload_test`, each with `DISPLAY=:94 ASAN_OPTIONS=detect_leaks=1` | All pass after the Windows-only reload guard was added. Logs `r2-os-linux.log`, `r2-savectl-linux.log`, `r2-savectl-reload-linux.log`; OS prints `17 cases, 0 failed`. |
| Final `make TARGET=win -j64 check` with WSL interop access | Exit 0, `build/r2-win-check.log`: 70 test executions (35 release + 35 sanitizer, counted from the driver's RESULT lines), every RESULT exit=0; `ASan fired: ok` for the expected-failure check. Includes IPC, descriptor, identity, both savectl suites and both OS suites in each configuration. |
| Initial `make TARGET=win -j64 check` with WSL interop access | Also exit 0, `build/r2-win-check-initial-interop.log`, before adding the new descriptor/identity suites. |
| `make TARGET=win -j64 check-savectl-production` | Exit 2, intentional dependency refusal, `build/r2-red-production-save.log`; this is an unmet acceptance condition, not a green integration claim. |
| Conditional production test compilation with C11 / Wall / Wextra / Werror / Wshadow / Wconversion and REAL_DEPS | Exit 0, `build/r2-production-save-test.o`; compilation does not establish native save behavior. |
| `git diff --check` | Exit 0. |

The OS changes were therefore retained for their named findings. On Windows
`os_test` prints `17 cases, 0 failed`; `os_win_test` prints `18/19 passed,
1 unavailable` and exits 0 in both configurations. The unavailable file-symlink
fixture reports native error 1314 (Developer Mode/elevation absent), as before;
the WOF/reparse fixture runs through the standard check driver.

Tracked benches were also run: `DISPLAY=:94 build/bench/ipc_bench` and
`DISPLAY=:94 build/bench/savectl_bench --track --probe`, both exit 0. Logs are
`r2-ipc-bench.log` and `r2-savectl-bench.log`. IPC measured p50 382030 ns and p99
666753 ns across its printed n=200 handoffs (M), including callback, ACK and
child reap. Savectl used the repo's configured probe mode; its printed n=1 per
row is below its population requirement and supplies no performance acceptance
claim. The saving-frame endpoint is still reported UNAVAILABLE by that bench.
Their automatic battery field says unknown on this desktop; the manual stamp is
[AC, desktop Spiral], with `uptime` at 13:41:46 reporting load averages 3.62,
2.77, 2.93. No performance gate was inferred from these observations.

At final Windows completion (13:52:35), `uptime` reported load averages 0.78,
1.19, 2.02 (M), [AC, desktop Spiral]. These green checks cover the dependencies
actually present in this worktree; they do not supersede the explicitly red
production-save gate or complete finding 11's baseline representation.

The first sandboxed Windows check could not read `%TEMP%` through WSL interop;
the first sandboxed Linux check failed before tests could run under LeakSanitizer
(`LeakSanitizer does not work under ptrace`). Both were rerun outside that
restriction, with Linux `ASAN_OPTIONS=detect_leaks=1` explicitly set. No leak
detection suppression or package installation was used.

## Found, not fixed / coordinator decisions

- Bring the landed W2.1/W2.2 dependencies into this worktree, retain their
  WIN_MODULES/build-list changes, run `make TARGET=win -j64
  check-savectl-production`, and complete native save validation. The ordinary
  Windows suite still prints a production dependency SKIP here. An asynchronous
  request for the dependencies was issued while independent work continued.
- W2.1 owns the full identity representation: preserve volume and both 64-bit
  index words from os.h, with consistent timestamp/metadata conversion. Replace
  the controller and fixture's legacy conversion together; cover baseline,
  check/keep/rebase and source/save identity, not only the reload guard fixed here.
  `src/file` and the frozen `src/os/os.h` were not edited to invent a competing ABI.
- Decide whether finding 2's fresh-generation protocol plus green native crash
  coverage is acceptable without a reproduced round-1 failure on this host.
- Review the narrowly scoped OS implementation changes for findings 8/9. Both
  platform implementations are retained only with their OS suites passing.
- Findings 12–16 remain outside scope for coordinator backlog triage. Full
  Windows main/editor launcher wiring is W2.4. No claim of production save/journal
  crash integration or complete public Windows baseline identity is made here.

## Coordinator verification (session 23, 2026-10-26)
- Rebased onto main a4feb31; mk/win.mk rebuilt from main plus the bead's three hunks (`savectl ipc` in WIN_MODULES, the PHONY entry, the REAL_DEPS / `check-savectl-production` block): 30f1d80.
- Production-save gate red read: state FAILED, file_error FILE_ERR_IO (1), modified. Correct behaviour; the test expected FILE_ERR_BUSY, which file.h defines as a save job in flight. Test now asserts FILE_ERR_IO with err_no == ERROR_SHARING_VIOLATION (31a79cf). `make TARGET=win check-savectl-production` rel and san green.
- 19:05:46-19:29:47 (M)[AC, desktop Spiral], load1 0.10 at start: `make all` 0; Linux `make check` 0 on :95 (110 test binaries, test_replay_cli, display lock contract PASS, leaks on); `make TARGET=win check` 0 (98 RESULT lines, all exit=0).
