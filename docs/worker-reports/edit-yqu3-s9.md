# edit-yqu3 / session 9 worker report

Scope: edit-yqu slice 3, P4-modules-2 sections 31–39. Git was read-only; no bd,
production source, HANDOFF.md, or worklog edits. Display is Xvfb :99 only.

Current status: implementation complete, final verification in progress. This
report will be updated with all final build, bench and fuzz outputs before exit.

Bench changes preserve the shared 10000-sample minimum (G), real shared-harness
verdicts, and default nonzero failures. Workloads are explicitly smaller/repeated;
large-file and displayed-frame gates are not established. See the design choices
in ../decisions/edit-yqu3.md.

Finding 35: fully indexed delegated source errors now cover line seeks, byte
seeks, idle relabelling, cursor following and relative motion, staged callbacks,
and positive-count/NULL spans. Errors preserve pending viewport state and index
publication; retry validates scalar byte/line results. Existing fragmented-source
fuzz assumptions were corrected to pump the owned continuation contract.

Finding 36: broader regex syntax and independent endpoint oracle; true default
folded long queries are generated. Module #21 remains a known failure.

Finding 37: actual mapped backing token acquisition, sticky recovery faults at
selected source validations, multi-chunk in-place reload rewrites with restored
mtime, and repeated running/queued slot reuse and cancellation schedules.
IN_ACCESS is a racing schedule, not a deterministic read barrier. Module #12
remains a known failure.

Known failures are opt-in expected-behavior unit regressions in the existing
module tests using EDIT_YQU_KNOWN_FAILURES=1. Neither module was fixed here.
LeakSanitizer cannot run in this sandbox: sanitizer and fuzz invocations use
ASAN_OPTIONS=detect_leaks=0; coordinator must rerun with leak detection enabled.

Final evidence pending.
