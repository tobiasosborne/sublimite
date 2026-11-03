# edit-b6ai.11: sublimité as Omarchy's default editor, without breaking $EDITOR

Research: [desktop-integration.md](../design/omarchy-2026-11-02/desktop-integration.md)
section 4 and Q5. Facts re-read from `/usr/share/omarchy` (Omarchy 4 alpha) on 2026-11-03.

## The constraint

`$EDITOR` is `omarchy-launch-editor --inline` (`default/uwsm/default`, `default/bash/envs`).
The launcher reads one word from `~/.local/state/omarchy/defaults/editor` and
branches on `${editor##*/}`:

| basename | `--inline` | no `--inline` |
|---|---|---|
| nvim vim nano micro hx helix fresh | `exec "$editor" -- "$@"` (in place, the caller waits) | `omarchy-launch-tui` (a terminal window running the editor) |
| anything else | `exec setsid uwsm-app -- "$editor" -- "$@"` | same |

`uwsm-app` is a client of a daemon (`/usr/bin/uwsm-app`, a pipe protocol): the
command runs under the daemon, so the caller can never wait for it, whatever
the editor does. Writing `sublimite` into the file therefore gives `git commit`
its file back at once (the coordinator tried and reverted this).

Only the first row can wait. That is where the wrapper has to live.

## Options

1. **Set EDITOR/VISUAL/git `core.editor` to `sublimite --wait`.** Works for git
   and crontab, needs no trick. But it edits the user's shell/session
   environment (`~/.config/uwsm/env.d/`, shell rc) or git config, which is a
   visible change in files install should not own, and every Omarchy script
   that calls `omarchy-launch-editor --inline` (and `SUDO_EDITOR="$EDITOR"`)
   still gets an editor that returns at once. It does not make
   "`$EDITOR` = Omarchy's default" work; it replaces it.
2. **Shadow `omarchy-launch-editor` earlier on `PATH`.** Rejected: replaces a
   system command, and `PATH` order differs between the Hyprland binds, the
   Quickshell menu and a shell.
3. **A wrapper with a basename the launcher treats as in place (chosen).**
   `defaults/editor` holds the absolute path of
   `<prefix>/libexec/sublimite/omarchy/fresh`, a 25-line POSIX shell script.
   `fresh` is the least likely real editor of the seven names (`nvim vim nano
   micro hx helix fresh`); the launcher accepts a path (`omarchy-cmd-present`
   uses `command -v`, the case uses `${editor##*/}`). `$EDITOR`, the Omarchy
   menus, bindings and `omarchy-launch-editor` are untouched; nothing the user
   owns outside Omarchy's own default-editor file and menu extension changes.

## What the wrapper does

It has to tell its two callers apart, because the name that makes the inline
branch wait also routes the non-inline branch into a terminal:

* `--inline` (git commit, crontab -e, `eff`, scripts): the process was exec'd
  by the caller. Wrapper runs `exec sublimite --wait -- "$@"`: blocks until the
  tab closes, exits 0 (non-zero statuses of sublimite pass through).
* No `--inline` (Super+Shift+N, Setup > Config rows, anything that calls
  `omarchy-launch-editor FILE`): `omarchy-launch-tui` has opened a terminal with
  `--app-id=org.omarchy.fresh` and the terminal runs the wrapper. The wrapper
  sees that string on its parent's command line (`/proc/$PPID/cmdline`),
  starts `setsid -f sublimite -- "$@"` with stdio on `/dev/null`, and exits, so
  the terminal closes and the caller was never blocked. If a terminal does
  not show the app-id on the parent (a single-instance server such as a
  `ghostty` daemon) the wrapper falls back to waiting, so the failure is a
  terminal window that stays until the tab is closed, never a lost edit.
* `SUBLIMITE_OMARCHY_WAIT=1|0` forces either behaviour.

### Cost (known, not measured)

A non-inline open from Omarchy flashes an empty terminal window (Omarchy's
`org.omarchy.*` TUI window class) for the time the terminal takes to start and
run a shell script. I could not measure it: no Hyprland session is available to
a worker and opening windows on Tobias's desktop was out of bounds. It is a
terminal start (the terminal is `foot` on this box) plus one `fork`. If that is
objectionable, uninstall-omarchy and option 1 on its own (`EDITOR='sublimite
--wait'` in `~/.config/uwsm/env.d/`) is the alternative, with the weakness
above. Opening via the Defaults menu row, the app launcher
(`sublimite.desktop` from plain `make install`) and the file manager (MIME
defaults) do not touch the launcher and do not flash.

## What `make install-omarchy` changes

All backed up under `$XDG_STATE_HOME/sublimite/omarchy`, restored exactly by
`make uninstall-omarchy` when the file is still the one install wrote:

* `<prefix>/libexec/sublimite/omarchy/fresh` (new).
* `~/.local/state/omarchy/defaults/editor` = that path. If the user picks
  another editor later through Omarchy's menu, uninstall keeps their choice and
  says so.
* `~/.config/omarchy/extensions/omarchy-menu.jsonc`: one marked block with the
  row `setup.default.editor.sublimite` ("sublimité", checked when it is the
  default). Inserted after the opening `{`; uninstall deletes the marked lines
  (or restores the file, or removes it when install created it). The shell's
  JSONC reader (`MenuModel.js stripJsonc`) drops whole-line comments and
  trailing commas, which is all the block needs.
* `$XDG_CONFIG_HOME/mimeapps.list`: `xdg-mime default sublimite.desktop` for
  the types in `sublimite.desktop`. If the file changed after install,
  uninstall rolls back only the types that still point at sublimite.
* Created directories are recorded and removed again if empty.

Guards: no `OMARCHY_PATH` (default `/usr/share/omarchy`) launcher gives the
message "Omarchy not found" and exit 0 with nothing written; missing
`<prefix>/bin/sublimite` asks for `make install` first; a launcher that no
longer mentions `fresh` refuses (Omarchy changed the list) and changes nothing.
`omarchy-default-editor` (the CLI) rejects names outside its list, which is why
the file is written directly; the menu row action does the same.

## Not done

* The install does not touch `EDITOR`, `VISUAL`, `SUDO_EDITOR` or git config.
* No bash completion or `omarchy-default-editor` patch.
* A real Hyprland/foot run of the non-inline path (flash duration) is open;
  `tools/test_install_omarchy.sh` models the launcher and terminal with
  fixtures that copy the launcher logic.

## Tests

`tools/test_install_omarchy.sh` (in `make check`, 5 s, no display): fake
Omarchy tree with copies of the launcher scripts, `uwsm-app` that detaches, a
terminal that keeps `--app-id` on its command line, and a sublimite stub that
blocks until released. `tools/test_install_omarchy_real.sh DISPLAYNUM` (opt-in
via `TEST_OMARCHY_REAL_DISPLAY`) runs the built `sublimite --wait` on a private
Xvfb and closes the tab with Ctrl+W.
