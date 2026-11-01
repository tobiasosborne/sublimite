**BLOCKER: None found.**

**MAJOR — Unsupported enumeration refuses ordinary saves.** [src/file/file_win.c:348](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.22-s28/src/file/file_win.c:348)

On a redirector returning `STATUS_INVALID_PARAMETER`, `STATUS_NOT_IMPLEMENTED`, or `STATUS_INVALID_INFO_CLASS` for `FileStreamInformation`, every overwrite reaches `FILE_ERR_IO`. These statuses bypass the buffer-growth branch; unchanged source/path identities leave the error intact; `transaction_prepare` discards the temporary and reports failure. A missing destination bypasses enumeration, so creating a document can succeed while its next save fails.

These are documented unsupported-stream responses, not evidence of damaged data. [Microsoft enumeration documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stream_information), [object-store specification](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/39679ad3-2ea1-4e5b-8af9-0fb88129b24e).

**Fix:** Treat these unsupported-information responses as an empty stream list unless a capability check establishes that preservation is required. Keep access-denied and genuine I/O failures fatal. Add successful-overwrite tests injecting each unsupported status; `r2_query` currently injects only access denial.

**MAJOR — A documented default-stream representation refuses saves.** [src/file/file_win.c:431](/home/tobiasosborne/Projects/sublimite/.wt/edit-177r.22-s28/src/file/file_win.c:431)

A filesystem may successfully enumerate a streamless file using a record with an empty `StreamName`. Microsoft explicitly permits this representation. The `units < 7` condition classifies it as malformed, producing `ERROR_INVALID_DATA` and refusing an ordinary overwrite. Existing tests use NTFS’s canonical `::$DATA` response and miss this case. [Microsoft record contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_stream_information).

**Fix:** Recognize an empty counted name as the default stream before named-stream validation, while retaining record/offset checks. Explicitly skip case-insensitive `::$DATA` aliases as well. Add synthetic successful-enumeration tests asserting that snapshot bytes survive and no default-stream record reaches `stream_copy`.

**MINOR: None found.**