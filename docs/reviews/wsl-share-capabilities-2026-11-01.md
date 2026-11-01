# WSL-share capabilities — native Windows measurement

**A naïve share-save fallback destroys executable permission bits.** In the
scratch experiment, closed-target `MoveFileExW` replacement changed Linux mode
`0751` to `0644`; `os_file_set_mode(0751)` returned success without restoring it
(M, Linux `lstat` before/after). **Current production saves fail safely instead:**
`FILE_ERR_IO / ERROR_INVALID_FUNCTION`, leaving original bytes, mode, owner and
group unchanged. Do not turn that failure into a successful save by merely
skipping metadata preservation or changing the rename call.

## Scope, reproduction and interpretation

Run from this worktree:

```sh
taskset -c 48-63 nice -n 10 make TARGET=win -j16 win-wsl-share-probe
tools/wsl_share_probe.sh
```

The wrapper itself enforces the same CPU lane and nice level. It supplies a
`mkdtemp` directory under this worktree's `build/`, both UNC spellings, and a
Linux marker helper. The executable runs only through `tools/winrun.sh`, verifies
the actual private station, and creates its NTFS control/profile under
`%TEMP%\\sublimite-test-wsl-probe-*`. Cleanup follows no symlinks and removes only
these newly created trees. No live-desktop work, registry changes or WSL lifecycle
operations occur. The target is deliberately outside `all` and `check`.

The OS implementation is unchanged, compiled into this test with only the
Known Folder lookup disabled so its real environment fallback uses the private
profile. Merely setting `%LOCALAPPDATA%` would not isolate `os_dir_try_lock`.
`src/file` is the production archive, including asynchronous acquire, attach,
edit, save prepare/commit and completion decode. The test does not fake a
filesystem result. See the [decision note](../decisions/edit-177r.21.md).

All observations below are **(M) [AC, desktop Spiral], while Tobias uses the
machine**. The log includes native Windows build, WSL kernel, start/end `uptime`
loads and power evidence. Final log and timing summary are recorded below.
The sample policy is three trials (E: initial trial plus two repeats); watch
experiments take three samples per trial, hence nine samples per origin/backend
(M: emitted samples; `3 × 3 = 9`). Mapping spans three native allocation units
(M: `3 × 65536 = 196608 bytes`). Bounds are probe policy (E), derived in the
decision note: an observation window, a helper allowance and a child watchdog.
None is a performance or correctness threshold.

**Boolean Win32 success is `result=1`; OS/file success is `result=0`.** Native
error is captured immediately on failure, and printed as zero on success.
`FileStreamInformation` also prints the actual NTSTATUS. A fault is logged by
the child or by its supervisor; every mapping case has its own process/watchdog.
Synthetic `*-trigger` cases prove containment/recovery only and are excluded
from real truncation/backing-loss conclusions.

NTFS's `/mnt/c` mount is read-only by policy. Its analogous mutations therefore
run natively in the Windows child; no Linux write to that mount is attempted.
NTFS native atomic replacement has its own notification samples. This control
does not establish Linux/NTFS interoperation.

## Capability matrix

Labels are proposed handling policy for the share. **Safe as is** is limited to
the tested operation and environment; **needs degraded mode** requires a later
implementation; **refuse** means the current operation must not be advertised
as a safe successful share write. `localhost` and `dollar` agree on the capability
outcomes throughout the repeated trials; they also give equal identity for the
same anchor file. Evidence groups below contain exact output lines.

| Capability | WSL share result, both spellings (M) | NTFS control (M) | Consequence / state |
|---|---|---|---|
| Regular open, copied bytes | Native and `os_file_open`/`src/file` copy acquisition work; fresh reads see Linux writes. | Works. | **safe as is** for copied regular-file reads, retaining acquisition/version checks. E1/E2/E6. |
| `FileIdInfo`, volume, upper word | `ERROR_NOT_SUPPORTED (50)`. Legacy query succeeds with volume zero; `os_file_stat` takes that fallback and `file_id.ino_hi=0`. | Full identity succeeds; measured upper inode word is also zero. Full volume serial has more bits than legacy serial. | **needs degraded mode** for namespace scope/transient capability changes. Upper word carries no share identity information in this fallback. E1. |
| Reopen, rename, edit identity | Same legacy identity after reopen, real Linux rename and in-place Linux edit. Hard-link names share the identity; an independent file differs. Production `os_rename` itself fails with `87`. | Stable through native production rename/reopen/edit; hard links equal, independent file differs. | **safe as is** within the measured single filesystem. Hard links must compare equal, not distinct; cross-device/distro uniqueness is unverified because share volume is zero. E1. |
| Native/production map acquisition | `CreateFileMapping`/`MapViewOfFile`, `os_map_file` and registration all succeed. | Succeed. | **needs degraded mode**: ability to map does not establish coherence. E2. |
| Linux truncate, replace, delete while mapped | Each Linux mutation succeeds. Both view types still read old `A` bytes, including the tail; no real exception and production epoch remains zero. A fresh read sees empty/replaced/missing data. | Native truncate is refused while mapped (`1224`); replacement by legacy `MoveFileEx` is refused (`5`); delete succeeds and retained view stays readable. | **needs degraded mode**: copy originals; detection cannot rely on a fault epoch. Real provider-loss exceptions remain unknown. E2/E3. |
| `os_fault_service` | Real mutations above produce no exception to catch. Synthetic `IN_PAGE_ERROR 0xc0000006`, backing status `0xc000009c`, is recovered with zero tail/epoch increment; raw synthetic case is contained and logged. | Same synthetic controls pass. | **safe as is** for measured synthetic recovery; **needs degraded mode** for share consistency. Synthetic success proves no outage guarantee. E3. |
| Mapped coherence with Linux writes | Fresh read sees `B`; warm mapped byte remains `A` through the bounded observation, for native and OS views. | Native Windows write is visible in the view. | **needs degraded mode**: explicit private copy plus authoritative rechecks. E2. |
| Rename a mapped source | Linux rename and Windows `MoveFileExW` rename to a missing name succeed with the mapping alive. | Windows rename to missing name succeeds. | **safe as is** for this operation; it says nothing about replacing an open destination. E2/E4. |
| Native parent notifications | `ReadDirectoryChangesW` immediately returns `ERROR_INVALID_FUNCTION (1)` for Windows writes, Linux writes and Linux atomic replace experiments. No armed request, no delay sample. | Native writes and native atomic replace deliver parent records. | **needs degraded mode**. Failure is an explicit setup error, not successful registration followed by silence. E5. |
| Production path/file watches and polling | Both watch starts fail with `1`. Actual file members stay unchanged via watch; explicit `file_check` detects Windows write, Linux write and Linux atomic replace in every sample. | Path notices and actual file checks work after native writes. | **needs degraded mode**: poll on focus, before save and before bulk reads. Watch failure cannot disable conflict detection. E5. |
| §2.8 `FileRenameInfoEx` | `POSIX_SEMANTICS | REPLACE_IF_EXISTS` fails with `ERROR_INVALID_PARAMETER (87)`, whether destination is closed, open or mapped. Production `os_rename` also fails `87`. | Native extended recipe and production rename work over closed, shared-open and mapped destinations. | **refuse** the current share replacement recipe. E4. |
| Legacy replacement fallbacks | `FileRenameInfo` and `MoveFileExW(REPLACE_EXISTING)` work only over a closed target; open/mapped targets fail `ERROR_ACCESS_DENIED (5)`, even with delete sharing. | Legacy fallbacks have the same open-target limitation; extended recipe works. | **needs degraded mode**, never an unconditional retry. No unlink-then-rename/in-place-overwrite fallback. E4. |
| Flush and acknowledgement | `FlushFileBuffers` succeeds on the share temporary. No directory persistence or power-loss experiment. | Succeeds. | **safe as is** to report that the data-flush call succeeded; **needs degraded mode** for durability claims. It does not prove rename persistence or end-to-end WSL/VHD storage ordering. E4. |
| Full production existing-target save | Acquire/edit/enqueue succeed; save completion is `FILE_ERR_IO (1)`, native `1`. All native security-query masks also return `1`; no save-step completion is reached. Original bytes remain `original\n`. | Saves complete successfully; bytes become `saved-original\n`. | **refuse** share saves until the transaction can preserve Linux metadata and replace atomically. Completion, rather than enqueue acknowledgement, determines success. E6. |
| Linux mode and ownership | Failed production save retains mode `0751`, UID/GID `1000/1000`. Naïve native fallback replaces with `0644`, same fixture owner/group. `os_file_set_mode(0751)` returns OS_OK but leaves `0644`. | Windows synthetic mode/security APIs work in their own model; this is no Linux-permission control. | **refuse** a fallback that cannot preserve and verify mode/ownership. Other owners/groups, Linux ACLs/xattrs and hard-link topology preservation were not tested. E6. |
| Linux symlink target | Production open of the relative Linux symlink fails `ERROR_CANT_RESOLVE_FILENAME (1921)`; target bytes/mode and link remain intact. | Fixture creation refused `ERROR_PRIVILEGE_NOT_HELD (1314)`; control is unavailable, not a passed symlink-save test. | **refuse** this unresolved share link path; never replace the link entry to conceal failure. Other reparse/provider transitions remain unknown. E6. |
| Stream enumeration | Both NT `FileStreamInformation` and Win32 `FileStreamInfo` fail: `STATUS_NOT_SUPPORTED (0xc00000bb)` / `50`. | Enumeration succeeds; ordinary named-stream save preserves the created stream. | **needs degraded mode**. This exact status is absent from edit-177r.22's current empty-stream status list; changing it alone still would not fix metadata or rename. E7. |
| `Zone.Identifier` write and share save | Native create/write succeed but Linux sees a separate literal `stream:Zone.Identifier` file. Ordinary and stream-named share-save attempts both fail safely with native `1`. | True ADS creation succeeds and is preserved through production save. | **needs degraded mode**: never interpret a successful colon-path write as stream support or MOTW preservation. E7. |
| `LockFileEx` | Both initial and contender calls fail `1`; Linux byte-range lock succeeds, so there was no Windows lock to contend with. | Initial succeeds, contender fails `ERROR_LOCK_VIOLATION (33)`. | **refuse** any feature requiring a share-file byte-range lock. No cross-Linux/Windows exclusion established. E8. |
| `os_dir_try_lock` | Initial succeeds, independent contender returns `OS_E_BUSY / 33`. Lock files actually live in the private NTFS profile. | Same result. | **safe as is** for this Windows profile's identity-keyed lock; **needs degraded mode** for multi-client/Linux coordination. This is not a lock on the Linux directory. E8. |
| Case-sensitive names | `a` opens `lower`, `A` opens `UPPER`. | Both spellings open the same `UPPER` object in the default directory. | **safe as is** for these exact opens; preserve case and avoid Windows case-fold deduplication for share paths. E9. |
| Colon, quote, trailing dot/space | Literal colon works; literal quote fails `123`. `wslpath` maps colon/quote to U+F03A/U+F022; those paths work. Literal trailing dot/space survive `os.h` conversion and open the intended files. | Explicit creation controls distinguish ADS/invalid quote/literal trailing names through `os.h`; details in E9. | **needs degraded mode** for translation/round-trip presentation; refuse ambiguous or lossy translations. Never strip trailing spaces. E9. |
| Long/explicit extended paths | Long file opens work, as does explicit `\\?\UNC\` through `os.h`; both paths identify the same object. | Long path and `\\?\` form also work. | **safe as is** for these measured names. E9. |
| Stopped distro, provider loss, reconnect | **Unknown; not provoked.** | No analogue measured. | **needs degraded mode**: preserve dirty/private data and fail closed on unavailable/changed identity. Do not infer an error code or recovery guarantee. E10. |

## Data-loss risks, in priority order

1. **Permission loss is demonstrated in the native fallback experiment (M).**
   Replacing an executable Linux file with the default temporary clears its
   executable bits. The current Windows mode API cannot restore them. Preserving
   this fixture's owner does not establish preservation of another owner/group.
2. **Lost external updates are a risk (inference).** Notifications fail, mapped
   bytes stay stale, and truncation sets no fault epoch. Skipping explicit
   generation checks or acknowledging an unsuccessful save could overwrite an
   agent's changes or discard dirty edits. Current production failures retained
   the target; no production overwrite/data loss was observed.
3. **False exclusivity/identity is a risk (inference).** Share byte locks fail;
   directory locks are per Windows profile; a zero volume serial is no proof of
   cross-device/distro uniqueness. Do not extend single-filesystem identity or
   single-profile lock results to independent clients.
4. **Power-loss/reconnect outcomes are unknown.** Successful flush is not evidence
   that the subsequent rename survives host/distro/storage failure. Keep recovery
   data until a transaction's actual endpoint is known.

## Proposed degraded mode — implementation deferred

**Classification and identity.** Recognize both WSL UNC servers, including their
extended-prefix forms, before choosing acquisition/save policy. Cache scoped
capabilities, not a generic “network drive” guess. Retain existing generation
checks; include a trustworthy Linux filesystem/distro namespace where identities
must compare globally. Capability/identity representation changes invalidate
the baseline and require reacquisition, rather than silently switching identity
formats. Zero `ino_hi` is not an extra uniqueness check.

**Acquisition and notification.** Use the production read-copy pipeline for share
originals, retaining prefix/full-read pre/post validation. Keep copies, derived
metadata and snapshots private. Treat unavailable notifications as a known
capability, not a successful watcher. Enqueue authoritative checks on focus,
before saving and before bulk work; while active, use a measured/tunable polling
policy chosen in the follow-up. Compare version/metadata and source generation,
mark changed, cancel dependent work and offer reload/keep. Do not poll on the
typing path. Existing explicit `file_check` is a measured starting point.

**Save and Linux metadata.** Refuse share writes until a Linux-aware transaction
can preserve and verify mode, ownership and required Linux metadata. Prefer a
Linux-side helper/backend that owns the actual source/parent descriptors, checks
the expected Linux identity, creates a private sibling, writes, reapplies and
verifies supported metadata, fsyncs, atomically renames, then fsyncs the parent.
Preserve the symlink entry and resolve its intended target explicitly; refuse
unresolved/retargeted paths. A native legacy rename is only a candidate for a
closed-target transaction after metadata preservation is proved. Never unlink
the destination first, truncate it in place, ignore metadata failures, or clear
dirty state on enqueue/failure. Preserve a private recovery copy on NTFS.

**Streams, locks and names.** Mark ADS unavailable on this provider; never create
colon-named siblings as an ADS fallback. Audit the exact unsupported NTSTATUS
separately from genuine enumeration/copy errors; this must not authorize a save
that still lacks metadata/rename support. Scope directory locks to the Windows
profile and refuse workflows requiring cross-client exclusion until a compatible
Linux protocol is measured. Preserve exact case, trailing characters and
`wslpath` escape information; fail rather than guess an ambiguous pathname.

**Availability and durability.** On I/O/identity failure retain dirty state and
private recovery data; avoid retries that can target a reconnected replacement.
Report the measured acknowledgement endpoint without a durability claim beyond
it. Settle stopped-distro errors, reconnect, mapping faults and flush persistence
on a disposable second host/distro, driven from Windows with bounded supervised
calls. Do not stop or restart Tobias's active WSL instance.

## Evidence and repeat stability

Final run details and exact representative lines follow. Each capability result
is repeated across the emitted trials, not inferred from a single successful
call. Timing values are observations under load, not gates.


Raw run: `build/worker-logs/wsl-share-probe-z340vs9v.log`, also copied to
`build/worker-logs/wsl-share-probe.log`. These build artifacts remain in the
worker worktree; representative evidence is embedded here so the review survives
without the build tree.

```text
measurement | [AC, desktop Spiral] | Tobias using the box | 12:23:10 up  3:57,  1 user,  load average: 3.42, 4.95, 4.90
power | BAT0 absent; desktop AC per brief
kernel | 6.18.54.1-microsoft-standard-WSL2
windows | 10.0 build=19045 (M)
load-after | 12:27:08 up  4:01,  1 user,  load average: 0.72, 2.44, 3.86
```

(M) The final log contains 225 completed children, derived as 25 enumerated cases
× three trials × three backends. All supervisor waits/exits are zero; no helper
failure, timeout or cleanup error is present. Identity comparison and every
explicit file-check sample were audited from the emitted lines. The two UNC
spellings agree on the capability/error results. Times and notification record
coalescing vary and are not claimed repeat-identical.

(M) NTFS notification delay samples below are **all nine values**, sorted, in
milliseconds, derived from each emitted nanosecond value ÷ 1,000,000. They include
mutation execution and observation-loop scheduling; actual file-member values
also include worker identity checking. The share produces no delay distribution:
registration is rejected before any event can be observed. No threshold or tail
claim is inferred from these small samples.

| NTFS experiment | All measured delay samples, ms (M) |
|---|---|
| windows-write / native_delay_ns | 0.1754, 0.1846, 0.1908, 0.2102, 0.2309, 0.2693, 0.2927, 0.4466, 0.5294 |
| windows-write / file_delay_ns | 0.1994, 0.2029, 0.2113, 0.2912, 0.3326, 0.3492, 0.3807, 0.4817, 0.5475 |
| windows-replace-control / native_delay_ns | 0.5216, 0.5271, 0.5373, 0.5524, 0.5817, 0.6548, 0.6784, 0.7856, 0.9166 |
| windows-replace-control / file_delay_ns | 8.9212, 8.9576, 14.5626, 15.0207, 15.0339, 15.0471, 15.0474, 15.1267, 15.1590 |
| actual file member / windows-write | 14.6346, 14.7356, 14.7476, 14.7626, 14.8776, 14.8906, 14.8973, 15.0055, 15.0860 |

The native flush result establishes only successful completion of the specified
file-buffer flush call. The [Microsoft FlushFileBuffers contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
describes sending buffered file information to its device/pipe; it does not
establish this WSL provider’s storage/rename ordering. The existing §2.8
qualification remains. The [Microsoft rename flags contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)
defines the extended POSIX replacement behavior; the measured provider rejects
that recipe here.

### E1 — identity

Identity words were compared for each trial: initial = reopen = hard-link = after rename = after edit; the independent file differs (M). The same result holds for the other UNC spelling. NTFS’s full and legacy volume representations differ; a transient fallback there is not tested and must not be assumed identity-preserving.

```text
aliases | localhost_error=0 dollar_error=0 same_identity=1 (M)
localhost | identity | 0 | initial.FileIdInfo | result=0 native=50 | volume=0000000000000000 ino=0000000000000000 ino_hi=0000000000000000
localhost | identity | 0 | initial.legacy | result=1 native=0 | volume=00000000 ino=00000000003d1af0 links=2
localhost | identity | 0 | initial.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af0 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
localhost | identity | 0 | reopen.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af0 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
localhost | identity | 0 | hardlink.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af0 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
localhost | identity | 0 | independent.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af1 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
localhost | identity | 0 | rename | result=11 native=87 | OS_E_INVAL
localhost | identity | 0 | identity-rename | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4004592 links=2 size=32 bytes_hex=4141414141414141414141414141414141414141414141414141414141414141
localhost | identity | 0 | after-rename.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af0 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
localhost | identity | 0 | after-edit.os_file_stat | result=0 native=0 | volume=0000000000000000 ino=00000000003d1af0 ino_hi=0000000000000000 file.ino_hi=0000000000000000 size=32 mode=666 uid=0 gid=0
ntfs | identity | 0 | initial.FileIdInfo | result=1 native=0 | volume=4c5a4c175a4bffe6 ino=002e0000001789a0 ino_hi=0000000000000000
ntfs | identity | 0 | initial.legacy | result=1 native=0 | volume=5a4bffe6 ino=002e0000001789a0 links=2
ntfs | identity | 0 | rename | result=0 native=0 | OS_OK
```

### E2 — real mapping behavior

Byte values are ASCII (P): A = 65, B = 66. Each real destructive condition and view type repeated in every trial (M). Stale Linux-write views remained A over the emitted observation interval; fresh file reads saw B. This is observed coherence failure, not a claimed maximum staleness lifetime.

```text
localhost | map-native-truncate | 0 | CreateFileMapping | result=1 native=0 | PAGE_READONLY
localhost | map-native-truncate | 0 | MapViewOfFile | result=1 native=0 | FILE_MAP_READ
localhost | map-os-truncate | 0 | os_map_file | result=0 native=0 | OS_OK
localhost | map-os-truncate | 0 | truncate | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4040717 links=1 size=0 bytes_hex=
localhost | map-os-truncate | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=3900 (M)
localhost | map-os-truncate | 0 | disk-after-action | result=0 native=0 | size=0 read=0 bytes=
localhost | map-native-write | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=2001999400 (M)
localhost | map-native-write | 0 | disk-after-action | result=0 native=0 | size=196608 read=64 bytes=BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
localhost | map-os-write | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=2005305000 (M)
localhost | map-os-replace | 0 | replace | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4041289 links=1 size=8 bytes_hex=4242424242424242
localhost | map-os-replace | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=5800 (M)
localhost | map-os-delete | 0 | delete | result=0 native=0 | OK deleted
localhost | map-os-delete | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=6200 (M)
localhost | map-native-rename | 0 | rename | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4041456 links=1 size=196608 bytes_hex=41414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141
localhost | map-native-winrename | 0 | rename | result=1 native=0 | Windows control mutation
ntfs | map-os-write | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=66 final=66 tail=65 exception=0x00000000 backing_status=0x00000000 epoch=0 observation_ns=3100 (M)
```

### E3 — containment controls and NTFS mutation contrast

The injected exception is explicitly synthetic. Real share truncate/replace/delete yielded no contained exception and no epoch increment. The raw injected case exits through the test containment handler; production recovery returns to the read with a zero tail.

```text
localhost | map-native-trigger | 0 | synthetic-safety-control | result=0 native=0 | injected IN_PAGE_ERROR; not a filesystem capability observation
localhost | map-native-trigger | 0 | contained-exception | result=-1 native=0xc0000006 | backing_status=0xc000009c epoch=0
localhost | map-os-trigger | 0 | synthetic-safety-control | result=0 native=0 | injected IN_PAGE_ERROR; not a filesystem capability observation
localhost | map-os-trigger | 0 | mapped-read | result=0 native=0 | length=196608 before=65 immediate=65 final=65 tail=0 exception=0xc0000006 backing_status=0xc000009c epoch=1 observation_ns=6900 (M)
ntfs | map-native-truncate | 0 | truncate | result=0 native=1224 | Windows control mutation
ntfs | map-native-replace | 0 | replace | result=0 native=5 | Windows control mutation
ntfs | map-native-delete | 0 | delete | result=0 native=5 | Windows control mutation
```

### E4 — replacement, sharing and flush

The deny-delete handle prevents extended NTFS replacement with ERROR_SHARING_VIOLATION; delete sharing is meaningful there. On the share, the unsupported extended recipe fails first, and legacy replacement still fails over shared-open targets. Every combination repeats with the same success/error result (M).

```text
localhost | atomic | 0 | FlushFileBuffers | result=1 native=0 | source data before rename; not a power-loss test
localhost | atomic | 0 | FileRenameInfoEx.closed | result=0 native=87 | target_open=0 mapped=0
localhost | atomic | 0 | FileRenameInfoEx.open-share-delete | result=0 native=87 | target_open=1 mapped=0
localhost | atomic | 0 | FileRenameInfoEx.mapped | result=0 native=87 | target_open=1 mapped=1
localhost | atomic | 0 | FileRenameInfo.closed | result=1 native=0 | target_open=0 mapped=0
localhost | atomic | 0 | FileRenameInfo.open-share-delete | result=0 native=5 | target_open=1 mapped=0
localhost | atomic | 0 | FileRenameInfo.mapped | result=0 native=5 | target_open=1 mapped=1
localhost | atomic | 0 | MoveFileEx.closed | result=1 native=0 | target_open=0 mapped=0
localhost | atomic | 0 | MoveFileEx.open-share-delete | result=0 native=5 | target_open=1 mapped=0
localhost | atomic | 0 | os_rename.closed | result=0 native=87 | target_open=0 mapped=0
ntfs | atomic | 0 | FileRenameInfoEx.closed | result=1 native=0 | target_open=0 mapped=0
ntfs | atomic | 0 | FileRenameInfoEx.open-share-delete | result=1 native=0 | target_open=1 mapped=0
ntfs | atomic | 0 | FileRenameInfoEx.mapped | result=1 native=0 | target_open=1 mapped=1
ntfs | atomic | 0 | FileRenameInfoEx.deny-share-delete | result=0 native=32 | target_open=1 mapped=0
```

### E5 — notifications and explicit checks

Share registration fails immediately, so zero delay fields mean no observation, not instantaneous delivery. Every Linux mutation has a separate successful helper acknowledgement in the raw log. Each origin has nine samples per backend (M: three samples × three trials). NTFS Linux-origin writes are explicitly skipped because /mnt/c is read-only; native writes/replacements are the control.

```text
localhost | notifications | 0 | file_watch_path_start | result=-1 native=1 | production shared watch service
localhost | notifications | 0 | windows-write | result=0 native=1 | sample=0 mutation=1 armed=0 arm_error=1 completed=0 completion_error=0 bytes=0 native_delay_ns=0 file_notice=0 file_delay_ns=0 (M) window_ms=2000 (E) events=
localhost | notifications | 0 | linux-write | result=0 native=1 | sample=0 mutation=1 armed=0 arm_error=1 completed=0 completion_error=0 bytes=0 native_delay_ns=0 file_notice=0 file_delay_ns=0 (M) window_ms=2000 (E) events=
localhost | notifications | 0 | linux-replace | result=0 native=1 | sample=0 mutation=1 armed=0 arm_error=1 completed=0 completion_error=0 bytes=0 native_delay_ns=0 file_notice=0 file_delay_ns=0 (M) window_ms=2000 (E) events=
localhost | file-watch | 0 | file_watch_start | result=-1 native=1 | actual file member + worker identity checks
localhost | file-watch | 0 | windows-write | result=0 native=1 | sample=0 mutation=1 start=-1 start_error=1 changed_via_watch=0 delay_ns=4431200 (M) explicit_file_check=1 reasons=33
localhost | file-watch | 0 | linux-write | result=0 native=1 | sample=0 mutation=1 start=-1 start_error=1 changed_via_watch=0 delay_ns=7689100 (M) explicit_file_check=1 reasons=33
localhost | file-watch | 0 | linux-replace | result=0 native=1 | sample=0 mutation=1 start=-1 start_error=1 changed_via_watch=0 delay_ns=7620000 (M) explicit_file_check=1 reasons=37
ntfs | notifications | 0 | windows-write | result=1 native=0 | sample=0 mutation=1 armed=1 arm_error=0 completed=1 completion_error=0 bytes=24 native_delay_ns=210200 file_notice=1 file_delay_ns=332600 (M) window_ms=2000 (E) events=3:notice
ntfs | notifications | 0 | windows-replace-control | result=1 native=0 | sample=0 mutation=1 armed=1 arm_error=0 completed=1 completion_error=0 bytes=48 native_delay_ns=537300 file_notice=1 file_delay_ns=8921200 (M) window_ms=2000 (E) events=3:notice,3:notice
ntfs | file-watch | 0 | windows-write | result=1 native=0 | sample=0 mutation=1 start=0 start_error=0 changed_via_watch=1 delay_ns=15086000 (M) explicit_file_check=1 reasons=33
```

### E6 — actual save, metadata and symlink results

Each ordinary, stream-named and symlink experiment repeats in every trial (M). The native fallback is a separate experiment, never a changed production save path. Fixture owner/group values are actual Linux observations; other ownership combinations were not tested.

```text
localhost | save | 0 | prepare-save | result=0 native=0 | OK mode=0751 uid=1000 gid=1000 dev=2096 ino=4041726 links=1 size=9 bytes_hex=6f726967696e616c0a
localhost | save | 0 | GetSecurityInfo.owner-group-dacl | result=0 native=1 | read-only native metadata query
localhost | save | 0 | GetSecurityInfo.production-mask | result=0 native=1 | read-only native metadata query
localhost | save | 0 | file.open-completion | result=0 native=0 | \\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.21-s28\build\wsl-share-probe-z340vs9v\localhost\save-0\save
localhost | save | 0 | file_save_begin | result=0 native=0 | ordinary non-FORCE existing-target save
localhost | save | 0 | file.save-completion | result=1 native=1 | production prepare/commit transaction
localhost | save | 0 | inspect-save | result=0 native=0 | OK mode=0751 uid=1000 gid=1000 dev=2096 ino=4041726 links=1 size=9 bytes_hex=6f726967696e616c0a
ntfs | save | 0 | file.save-completion | result=0 native=0 | production prepare/commit transaction
ntfs | save | 0 | save.target-bytes | result=0 native=0 | size=15 read=15 bytes=saved-original
localhost | fallback-metadata | 0 | prepare-save | result=0 native=0 | OK mode=0751 uid=1000 gid=1000 dev=2096 ino=4041819 links=1 size=9 bytes_hex=6f726967696e616c0a
localhost | fallback-metadata | 0 | naive-fallback.MoveFileEx | result=1 native=0 | native experiment only; production save is unchanged
localhost | fallback-metadata | 0 | inspect-save | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4041818 links=1 size=6 bytes_hex=73617665640a
localhost | fallback-metadata | 0 | os_file_set_mode.0751 | result=0 native=0 | OS_OK
localhost | fallback-metadata | 0 | inspect-save | result=0 native=0 | OK mode=0644 uid=1000 gid=1000 dev=2096 ino=4041818 links=1 size=6 bytes_hex=73617665640a
localhost | save-symlink | 0 | file.open-completion | result=1 native=1921 | \\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.21-s28\build\wsl-share-probe-z340vs9v\localhost\save-symlink-0\save-link
localhost | save-symlink | 0 | inspect-symlink | result=0 native=0 | OK target=mode=0751 uid=1000 gid=1000 dev=2096 ino=4041753 links=1 size=9 bytes_hex=6f726967696e616c0a link=mode=0777 uid=1000 gid=1000 dev=2096 ino=4041756 links=1 size=4 symlink=save
ntfs | save-symlink | 0 | prepare-symlink | result=0 native=1314 | Windows control
```

### E7 — streams are not colon-named Linux files

The current stream-query implementation accepts INVALID_PARAMETER / NOT_IMPLEMENTED / INVALID_INFO_CLASS as an empty stream list. The actual share status is NOT_SUPPORTED instead. The full production save currently fails earlier as well; a change to this list alone is not a measured working save.

```text
localhost | streams | 0 | FileStreamInformation | result=0 native=50 | NTSTATUS=0xc00000bb information=0
localhost | streams | 0 | FileStreamInfo | result=0 native=50 | Win32 information class
localhost | streams | 0 | Zone.Identifier.create | result=1 native=0 | CREATE_NEW, scratch only
localhost | streams | 0 | Zone.Identifier.write | result=1 native=0 | scratch MOTW stream
localhost | streams | 0 | inspect-streams | result=0 native=0 | OK entries=['stream', 'stream:Zone.Identifier'] file=mode=0644 uid=1000 gid=1000 dev=2096 ino=4041707 links=1 size=8 bytes_hex=4141414141414141
localhost | save-stream | 0 | file.save-completion | result=1 native=1 | production prepare/commit transaction
localhost | save-stream | 0 | save.stream-after | result=0 native=0 | size=4 read=4 bytes=zone
ntfs | streams | 0 | FileStreamInformation | result=1 native=0 | NTSTATUS=0x00000000 information=38
ntfs | save-stream | 0 | file.save-completion | result=0 native=0 | production prepare/commit transaction
ntfs | save-stream | 0 | save.stream-after | result=0 native=0 | size=4 read=4 bytes=zone
```

### E8 — lock scope

The directory lock is the real production implementation, but its lock file is in this probe’s private NTFS profile. No cross-profile, cross-host, Linux-directory or multi-client exclusion is established.

```text
localhost | locks | 0 | LockFileEx.first | result=0 native=1 | byte zero
localhost | locks | 0 | LockFileEx.contender | result=0 native=1 | independent handle, same byte
localhost | locks | 0 | linux-lock | result=0 native=0 | OK linux_lock_blocked=0
localhost | locks | 0 | os_dir_try_lock.first | result=0 native=0 | OS_OK
localhost | locks | 0 | os_dir_try_lock.contender | result=4 native=33 | OS_E_BUSY
ntfs | locks | 0 | LockFileEx.first | result=1 native=0 | byte zero
ntfs | locks | 0 | LockFileEx.contender | result=0 native=33 | independent handle, same byte
ntfs | locks | 0 | os_dir_try_lock.contender | result=4 native=33 | OS_E_BUSY
```

### E9 — names and prefixes

The wrapper obtains each escaped path from wslpath, preserving trailing spaces. Colon and quote map to U+F03A and U+F022 in this run (M; the full prepare-names output is retained in the raw log). NTFS can hold literal trailing dots/spaces through the production NT path conversion; default Win32 naming restrictions are not a universal filesystem limit. NTFS a:b is a stream, whereas the share counterpart is a Linux filename.

```text
localhost | names | 0 | prepare-names | result=0 native=0 | OK wslpath='a'='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\a';'A'='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\A';'a:b'='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\a\uf03ab';'quote"'='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\quote\uf022';'trailing.'='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\trailing.';'space '='\\\\wsl.localhost\\Ubuntu\\home\\tobiasosborne\\Projects\\sublimite\\.wt\\edit-177r.21-s28\\build\\wsl-share-probe-z340vs9v\\localhost\\names-0\\space '
localhost | names | 0 | a | result=0 native=0 | read=5 bytes=lower
localhost | names | 0 | A | result=0 native=0 | read=5 bytes=UPPER
localhost | names | 0 | a:b | result=0 native=0 | read=5 bytes=colon
localhost | names | 0 | quote" | result=11 native=123 | read=0 bytes=
localhost | names | 0 | wslpath.a:b | result=0 native=0 | read=5 bytes=colon
localhost | names | 0 | wslpath.quote" | result=0 native=0 | read=5 bytes=quote
localhost | names | 0 | trailing. | result=0 native=0 | read=3 bytes=dot
localhost | names | 0 | space  | result=0 native=0 | read=5 bytes=space
localhost | names | 0 | wslpath.space  | result=0 native=0 | read=5 bytes=space
localhost | names | 0 | long.length | result=0 native=0 | utf8_bytes=390 (M)
localhost | names | 0 | long.os_file_open | result=0 native=0 | read=4 bytes=long
localhost | names | 0 | extended-prefix.os_file_open | result=0 native=0 | read=4 bytes=long
localhost | names | 0 | extended-prefix.same-identity | result=1 native=0 | normal versus explicit extended path
ntfs | names | 0 | control-create.a:b | result=0 native=0 | OS_OK
ntfs | names | 0 | control-create.quote" | result=11 native=123 | OS_E_INVAL
ntfs | names | 0 | control-create.trailing. | result=0 native=0 | OS_OK
ntfs | names | 0 | control-create.space  | result=0 native=0 | OS_OK
ntfs | names | 0 | a | result=0 native=0 | read=5 bytes=UPPER
ntfs | names | 0 | A | result=0 native=0 | read=5 bytes=UPPER
ntfs | names | 0 | long.length | result=0 native=0 | utf8_bytes=352 (M)
ntfs | names | 0 | extended-prefix.same-identity | result=1 native=0 | normal versus explicit extended path
```

### E10 — availability and supervisor

Unknown by instruction: stopped-distro error/latency, provider/reconnect failures, mappings after actual transport loss and power-loss flush persistence. Settle these using an expendable distro/host controlled from a separate Windows supervisor; preserve this running instance. Capture native errors, exception code plus in-page status, deadlines, dirty-data recovery and identity after reconnect.

```text
availability | UNKNOWN | distro shutdown/restart forbidden; settle on a disposable second host/distro from Windows
supervisor | failed=0
wrapper | exit=0 (M)
```
