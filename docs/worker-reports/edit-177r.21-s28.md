# edit-177r.21 phase 1 — WSL share capability measurement

Measurement only. No production source changes. Worktree: `.wt/edit-177r.21-s28`.

The explicit `win-wsl-share-probe` target builds `tests/win/wsl_share_probe.c`;
`tools/wsl_share_probe.sh` supplies private worktree WSL UNC scratch and runs it
through `tools/winrun.sh` on a private station. The Python marker helper performs
Linux mutations and reports actual Linux metadata. The executable creates and
removes its own `%TEMP%\\sublimite-test-wsl-probe-*` NTFS control/profile.

Files: the probe, shell/Python wrapper, explicit target in `mk/win.mk`, capability
review at `docs/reviews/wsl-share-capabilities-2026-11-01.md`, and decision note
`docs/decisions/edit-177r.21.md`. The target is outside `all`, `check`, and
`check-live`; the filename deliberately does not match `*_test.c`.

The unchanged OS source is compiled into the probe with only
`SHGetKnownFolderPath` made unavailable, activating its real environment
fallback to the private `%LOCALAPPDATA%`. This is necessary because the native
Known Folder otherwise overrides the environment and `os_dir_try_lock` would
write the real profile. Filesystem, mapping, fault, lock and file-save calls
are unmodified production code. `src/file` links from the production archive.

Red/green and verification (M; statuses captured from the shell):

| Command | Exit | Evidence |
|---|---|---|
| `taskset -c 48-63 nice -n 10 make TARGET=win -j16 win-wsl-share-probe` before registration | 2 | `build/worker-logs/wsl-probe-red.log`: `No rule to make target 'win-wsl-share-probe'` |
| Same after implementation | 0 | `build/worker-logs/wsl-probe-build.log` |
| `taskset -c 48-63 nice -n 10 make -j16 all` | 0 | `build/worker-logs/linux-all.log`, final unchanged-target check in `linux-all-final.log` |
| `taskset -c 48-63 nice -n 10 make TARGET=win -j16 all` | 0 | `build/worker-logs/win-all-final.log` (also initial `win-all.log`) |
| `taskset -c 48-63 nice -n 10 make -j16 check` in sandbox | 2 | `build/worker-logs/linux-check.log`: private Xvfb socket creation blocked |
| Same with execution permission for private Xvfbs | 0 | `build/worker-logs/linux-check-escalated.log`: all enumerated binaries and shell contracts passed |
| `taskset -c 48-63 nice -n 10 tools/wsl_share_probe.sh` with WSL interop execution permission | 0 | Final `build/worker-logs/wsl-share-probe-z340vs9v.log`, mirrored as `wsl-share-probe.log` |
| `taskset -c 48-63 nice -n 10 make TARGET=win -j16 all win-wsl-share-probe` final unchanged-target check | 0 | `build/worker-logs/win-final-targets.log` |

There is no production implementation to drive from a deliberately false
filesystem expectation. The red case verifies the missing deliverable; capability
failures are measured results, not assertions to fix. Initial harness runs found
missing child stdout inheritance and a reused-marker creation/deletion race;
both were fixed before the final evidence run. Unique request/ready/response
names remain until wrapper cleanup. Synthetic IN_PAGE_ERROR controls separately
verify raw-fault containment and production fault recovery; they are never
reported as real backing-loss measurements.

Final measurement (M) [AC, desktop Spiral], while Tobias uses the machine:
Windows `10.0 build=19045`, WSL kernel `6.18.54.1-microsoft-standard-WSL2`.
The raw run starts at `12:23:10`, load averages `3.42, 4.95, 4.90`, and ends at
`12:27:08`, loads `0.72, 2.44, 3.86` (M: emitted `uptime`; no timing gate).
BAT0 is absent, desktop AC is per the brief. The log enumerates 225 successful
supervisor exits: 25 cases × three trials × three backends (M: source enumeration
and log count). No helper failure, unexpected child exit, timeout or cleanup
error occurred. Synthetic raw faults were explicitly contained; synthetic OS
faults recovered. No real backing-loss exception was induced by the Linux file
mutations. Both UNC spellings agree on capability/error results; the same anchor
has equal identity through both spellings.

The [capability review](../reviews/wsl-share-capabilities-2026-11-01.md) embeds
representative output, the comparison matrix, all measured NTFS notification
delay samples, repeat-stability interpretation and a proposed degraded mode.
The raw log is a build artifact; the embedded evidence survives without it.

Most dangerous finding: naïve fallback replacement changes Linux mode `0751` to
`0644`, and `os_file_set_mode(0751)` reports success without restoring executable
bits (M, all share trials). Current production ordinary and stream-named saves
fail safely with `FILE_ERR_IO`, native `ERROR_INVALID_FUNCTION`, preserving the
target's bytes/mode/UID/GID. Native security queries fail as well; extended
replacement independently fails `ERROR_INVALID_PARAMETER`. A native closed-target
legacy rename works, but fails over open/mapped destinations and loses Linux
mode without a Linux-aware metadata transaction.

Mapping works but retains stale bytes after Linux writes/truncation without a
fault epoch; fresh reads see changed bytes. Both native/production watches fail
explicitly with `ERROR_INVALID_FUNCTION`; explicit `file_check` detects every
sampled mutation. `FileIdInfo` is unsupported, so the stable local fallback has
zero volume/upper word. Share byte locks fail; directory locks work only through
their actual Windows-profile NTFS lock files. Stream enumeration returns
`STATUS_NOT_SUPPORTED`, outside edit-177r.22's current empty-list status set;
writing `Zone.Identifier` creates a literal Linux colon-named sibling. Relative
Linux symlink acquisition fails without changing its target; the NTFS symlink
control could not be created because the token lacks the privilege.

The coordinator must choose the later share-save architecture and refusal policy.
The proposal is copy acquisition and explicit checks, with share writes refused
until Linux mode/ownership and atomic replacement can be preserved and verified.
No part of that degraded mode is implemented here.

Found, not fixed: notification, replacement, metadata and stream support differ
on the share; details and implementation proposals belong in the capability
review. No WSL lifecycle operation was attempted. Stopped-distro availability,
cross-distro identity collisions, provider outages and power-loss persistence
remain unmeasured; a separate disposable Windows-driven environment is needed.
Linux ownership was checked for the fixture's real UID/GID only; other owners,
ACLs and xattrs are not a preservation claim. NTFS native creation controls show
that trailing dots/spaces can work through `os.h`'s NT path conversion, so they
must not be called universally impossible Windows names. No FAT/multi-client
lock guarantee or transient identity-fallback guarantee is established.

The full Windows `check` recipe was not run: the requested Windows verification
was `all` plus this explicit share probe. Linux `check` additionally passed its
117 enumerated test binaries and shell contracts (M: runner summary). Initial
sandbox attempts to launch WSL interop and private Xvfbs failed before useful
measurements; the approved execution-permission reruns passed. No automatic
approval rejection occurred. No production code or live desktop was changed.

CPU policy: all builds/tests use the brief's `taskset -c 48-63 nice -n 10`, with
`-j16` for make. Measurements are stamped `[AC, desktop Spiral]`, with load and
the fact Tobias is using the machine. Counts and durations in the review are
either emitted measurements or explicitly derived probe-policy estimates.
