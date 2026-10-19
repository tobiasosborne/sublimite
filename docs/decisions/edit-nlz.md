# edit-nlz: focused-test selector tables

Use immutable `{name, fn}` tables and allocation-free lookup in `tests/select.h`.
Lookup returns the first matching callback, or a null pointer. The caller runs
the callback directly, preserving its exit code and its own unknown-selector
policy. Small adapters retain parameterized calls and grouped calls, including
their existing assertion behavior and short-circuiting. Default run lists stay
explicit and retain their original order.

The editor's headless mode and parameterized `--o20-` prefix remain special
paths; the exact `--o20-all` case is a table entry. Dataloss's probe remains a
default-run modifier. No selector names or argument-count policies change.

The large-file suite actually selects through environment variables. Its
tables occupy the original selection points between default cases, retaining
the prerequisite runs and precedence. Environment lookup treats an empty value
as present, just as the former `getenv` conditions did. The existing jump into
the fixture and optional early-index assertion remain control-flow modifiers.
Tracing initialization is common to the formerly separate initial paths.

The helper contract test labels exact lookup, missing/empty lookup, first-match
precedence, callback exit-code preservation, and environment presence behavior.
No production code, allocation policy, or performance budget changes.
