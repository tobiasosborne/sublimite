# edit-pyo: savectl fuzz model alignment

The model follows the landed edit-mdv contracts; controller behavior is unchanged.
An installation allocation failure exposes FAILED/FILE_ERR_NOMEM and preserves
the old tree and output arguments. Clearing the fault does not make the retired
private snapshot installable. The acquisition schedule checks BUSY, drains
retirement, checks both recovery actions, then explicitly reloads and installs a
new acquisition.

JOURNAL_BASE_CHANGED during prepare is an external conflict. The journal
schedule expects EXTERNAL_MODIFIED, a banner, reload/keep actions, the original
journal diagnostic, no file error and no pending finish. Its unprepared token
has no retained previous path. JOURNAL_INVALID and JOURNAL_IO still expect
FAILED. No existing journal, mapped-fault, acquisition, rewrite, slot-reuse or
partial-drain schedule is removed or relaxed.

These are oracle changes to existing fuzz regressions, after reproducing both
old expectations as failures. No production source, typing-path allocation,
global state or API is added. Complete red/green evidence is in
[the worker report](../worker-reports/edit-pyo-s9.md).
