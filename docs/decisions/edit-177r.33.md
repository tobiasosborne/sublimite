# edit-177r.33 — hidden Windows modal tests and the shared pointer

Private window stations and desktops hide windows and scope some resources;
they do not isolate the real pointer. Tobias observed hidden native move/size
tests moving it. No Windows executable was run for this change.

Native move/size, Alt+Space and system-menu test entry points now begin with
`LIVE_ONLY_NATIVE_LOOP`. The macro queries actual native isolation, returns
unless it is `TEST_ISOLATION_LIVE`, and prints the shared-pointer SKIP reason.
Native input injection and capture theft use the same conservative rule. The
live selectors retain the native cases for coordinator-authorized execution.

Hidden suites directly send modal entry/exit notifications, events and timer
ticks on the UI thread. Test-only callback registration reaches production
modal servicing and the editor's `on_modal_turn`. Assertions check that turn
stops post no cancel, later turns still run, work/input/frame/journal progress,
and close/session/quit do cancel and close preserves accepted text. Watchdog
coverage also uses direct notifications. This does not prove that a real
DefWindowProc modal loop survives; only live cases can do that.

The Python source guard scans every test C source, header and include fragment,
a conservative superset of Windows inputs, so an incoming unreferenced header
is covered immediately. It accepts hazardous tokens only inside functions
whose first statement is the canonical live macro. It also recognizes posted
Alt+Space. The guard and its Python regressions are prerequisites of Windows
`check`, before even the reparse fixture runs, and can run alone without
Windows. It is a lexical review guard, not a C preprocessor or a proof against
deliberately obfuscated numeric commands. Production platform code is outside
its scope. Maximize/restore/minimize/close and caption double-click do not start
a pointer loop and retain quiet command-path coverage.
