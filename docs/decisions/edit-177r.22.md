# edit-177r.22 — preserve named NTFS streams during atomic save

The Windows save transaction copies the existing destination's named data
streams onto its private temporary before final metadata application, flushing,
and atomic replacement. The default stream still comes exclusively from the
piece snapshot. EFS still refuses with `ERROR_FILE_ENCRYPTED` before creating a
temporary.

## Enumeration and names

Use `NtQueryInformationFile(FileStreamInformation)` on the retained metadata
handle. Unlike pathname-based `FindFirstStreamW`, this enumerates the captured
object even if its pathname is renamed or replaced. It also returns the actual
valid buffer length through `IO_STATUS_BLOCK.Information`, allowing every entry,
offset, and UTF-16 name to be checked against bytes the API actually returned.
The API documents that byte count in [NtQueryInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile).
The stream record layout and buffer-overflow behavior are documented in
[FILE_STREAM_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stream_information).
The SDK's `FILE_STREAM_INFO` has the corresponding counted record layout.

Start with a record-sized allocation and double on native truncated-buffer
statuses, checking the API's `ULONG` capacity boundary. There is no fixed
enumeration/name-size policy limit. Each name remains a counted UTF-16 slice
inside the returned allocation: its byte length must fit that entry, be aligned
to a `WCHAR`, and fit the native open's `UNICODE_STRING.Length` (`USHORT`). It
is never passed to an unbounded string routine or copied into a guessed-size
name array. Reject malformed names/records and unsupported stream types.

Open each `:name:$DATA` relative to the captured source file handle and the
temporary file handle with `NtCreateFile`. No stream pathname is reconstructed.
Skip only `::$DATA`, which is already written from the snapshot. Creation uses
`FILE_CREATE`, so an unexpected pre-existing stream is an error. Source stream
handles share read/delete but deny concurrent writers while being copied.

## Transaction and errors

The existing source security descriptor is applied and verified before any
document or named-stream writes. Defer the temporary's read-only attribute until
final metadata application, because new stream handles otherwise cannot be
created. Restore it before the existing default-stream flush and rename.

Stream data uses one reusable save-worker transfer buffer, with the existing
default-stream chunk size (P: `FILE_PREFIX_MAX / 4 = 256 KiB`). Read/write loops
handle short and interrupted transfers and poll cancellation between attempts.
Premature EOF or a changed stream size returns `FILE_ERR_CHANGED`. Flush every
destination stream before closing it, including empty streams. Enumeration,
allocation, open, read, write, stat, or flush errors fail the transaction with a
native error the editor can report. A failing copy rechecks the captured file
and destination pathname to distinguish a changed target from a genuine I/O
failure. Cancellation and every failure use the existing temporary-discard path.

The existing post-metadata identity check and final destination/source validation
still authorize replacement. No writes touch the old target. Process death can
leave a temporary under the existing policy; the public target is the old file
with its streams or the completed replacement with its streams. The existing
last-validation-to-rename race and Windows power-loss durability qualification
remain unchanged: the endpoint is data-flushed atomic replacement acknowledged,
without a directory flush.

## Save As and reload

Streams belong to the **destination being replaced**, matching Linux xattrs.
`metadata_capture` opens the destination named by `file_save_args.path`, and
Linux `metadata_xattrs` copies from that handle. Consequently Save As over an
existing target preserves that target's streams; source-only streams stay on
the source. Save As to a missing target does not import source streams. Tests
exercise both paths with a snapshot acquired from a different source file.

Reload need not copy streams into the buffer or its private reload backing.
The next save captures metadata from the current public destination. The
production save-controller regression externally rewrites the file and creates
`Zone.Identifier`, reloads, edits, fails a save on a locked ADS while retaining
dirty state, then retries successfully and checks the retained stream bytes.

## Scope and verification

All new allocations run in the save transaction, on the bulk worker (or an
explicit synchronous `file_save_write` caller). The UI snapshot/enqueue and
typing paths acquire no new allocations. Linux code has no behavior change:
the common-source addition and declaration are Windows-conditional, and the
Linux-preprocessed `file.c` is byte-identical to HEAD (M: worker comparison).
Native failure shims remain confined to the test translation unit. Validation
and red/green evidence are in [the worker report](../worker-reports/edit-177r.22-s28.md).
