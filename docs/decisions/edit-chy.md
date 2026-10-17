# edit-chy: local installation and display policy

The production launcher preserves the shell's display by default.
`EDIT_REFUSE_REAL_DISPLAY` being present, including an empty value, enables the
agent guard. The old `EDIT_ALLOW_REAL_DISPLAY` variable is accepted as a no-op
by the launcher and cannot override refusal. Explicit X display refusal stops
before IPC, journal creation or any display connection; tests and benches keep
their constructor's existing redirect to a private Xvfb. Missing X display
under refusal retains the existing private-display fallback. Wayland uses the
same refusal policy through its existing allow parameter, clearing inherited
socket descriptors whenever refusal is present.

The pure X selector takes refusal instead of opt-in. Its private alternate
selection remains available to the test constructor. This updates the policy
recorded in `rename-sublimite.md` and `edit-1zm.md` for the production launcher;
the test constructor retains its explicit bench opt-in convention and always
exports refusal for any launched production child.

Install depends only on the release editor, release tracedump, desktop entry
and local SVG. It does not depend on `all`, which also builds tests and benches.
`PREFIX` defaults to `$(HOME)/.local`. Desktop and icon caches are refreshed when
their tools exist; missing tools produce the corresponding command. The MIME
default is `sublimite.desktop` for `text/plain`. Uninstall removes only the
installed product files and refreshes caches, leaving shared directories and
user profiles intact. It leaves MIME preference selection to the desktop after
removal, without guessing the user's previous editor.

The icon is a simple local monochrome path. Its SVG coordinates are (E) visual
construction values for a square normalized canvas, not performance bounds.
A polished wordmark remains outside this bead. A display-free `--help` supports
the installed-command smoke test. Install tests isolate XDG data, configuration,
cache and runtime paths so registration does not change the user's preferences.

The previous X policy already permits a private nonzero display, so that CLI
startup assertion alone cannot be red. The red regression is the pure default
selector, plus the install contract. The CLI regression covers startup from
`DISPLAY` with an invalid alternate and explicit refusal with a diagnostic;
all connections use the fixture's private display. No timing budget is added.
