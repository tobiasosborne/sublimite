# edit-cuh: library-only keymap exemptions and power-stamp verification

Narrow the exemption. A worker-wide or helper-wide bracket would also hide
our cancellation checks, future allocations in state preparation, and future
control-flow helpers. The exemption belongs around direct library calls.

`rebuild_keyboard` brackets context/device discovery and keymap construction,
with cancellation checks and `x11_input_prepare_state` outside those brackets.
State preparation must still exempt libxkbcommon's state creation and lazy
action-filter growth: simply removing the outer bracket would count those
library allocations. Its existing setup/worker-only helper therefore brackets
the direct XKB calls inside each warm-up iteration, leaving our loop control
and function body counted. Publication stays counted. The existing thread-local
guard API and sanitizer no-op behavior are unchanged.

The server-free regression includes the implementation and substitutes
library discovery plus cancellation/publication/state-preparation entry
probes. Real libxkbcommon state preparation still runs. Deliberate own-code
allocations must count while library probes do not; logical scope checks also
run under sanitizers, whose allocation interposition is inert. Cancellation
and construction failures must leave scopes balanced. No production test
hooks, display connection, worker scheduling, or timing allowance is added.

Both requested STAMP paths already read BAT0 and classify its value at runtime
in the supplied worktree. Preserve that implementation: Discharging means
`[bat]`; Charging, Full and Not charging mean `[AC]`; missing, empty or unknown
evidence means `[unknown]`. The fixture script compiles the bench stamp
function verbatim and the editor cost function through its stamp print,
substituting only the opened files. This isolates the stamp from the editor
workload and display. Temporary fixed-AC mutations of the extracted snippets
demonstrate that the regression detects the reported behavior; they are not
changes to the worktree's production sources.

Other fixed-AC measurement labels are outside this bead's STAMP scope and are
reported without changing them. No Wayland implementation change is needed.
