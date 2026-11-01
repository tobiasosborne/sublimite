# edit-177r.24 (s28): tools/sublimite-win, WSL launcher

## Changed
- `tools/sublimite-win` (new, bash): translates file arguments with `wslpath -w`, then `exec`s the exe (no background job, so `--wait` status and `-` stdin are the exe's own).
- `tools/test_sublimite_win.sh` (new): 36 assertions against a stub exe (`SUBLIMITE_WIN_EXE`); argv compared NUL-exact; skips with a reason when `wslpath` is absent.
- `Makefile`: test registered in `check` and `check-serial` (same form as test_replay_cli); `install` copies `sublimite-win` only `if command -v wslpath`; `uninstall` removes it.

## Behaviour and decisions
- Suffix rule matches src/ipc/args.c: exact name that exists (`-e` or `-L`) is a file; only an absent name has up to two trailing `:<digits>` peeled (args.c peels the second without checking the name, so `a:3:9` becomes `a` + `:3:9` even if `a:3` exists; tested). Suffix is re-appended outside the translation.
- Absolute + resolved with `realpath -m`; a missing path has its deepest existing ancestor translated and the rest appended with `\`. /mnt/c gives `C:\...`, WSL fs gives `\\wsl.localhost\<distro>\...`; both tested.
- Path-valued option: only `--font-file` (value `PATH[#INDEX]`, `#digits` peeled when the name is absent; both `=` and separate forms). `--font-family`, `--font-fallbacks`, `--wait`, `--new-instance`, unknown options, and `--` pass through verbatim. `-` is always the stdin marker (as in args.c).
- Exe lookup: `SUBLIMITE_WIN_EXE`; else exactly one `/mnt/c/Users/*/AppData/Local/Programs/sublimite/sublimite.exe` (read-only filesystem look); else `<script dir>/../build/win/rel/sublimite.exe`; else message and exit 127. I did NOT use `cmd.exe /c echo %LOCALAPPDATA%` or `wslvar` (wslvar is not installed here, and running cmd.exe is forbidden by the s28 rules, so "shows no window" is unverified). The glob needs no Windows process. Several users with an install: error asking for `SUBLIMITE_WIN_EXE`.

## Red / green
- Red: launcher moved aside, `bash tools/test_sublimite_win.sh` exit 1, "32 FAILED".
- Intermediate: 5 failures were test-expectation errors (below), fixed in the test.
- Green: `bash tools/test_sublimite_win.sh` exit 0, "all passed".
- `make -j8 check` (taskset -c 48-55 nice -n 10): exit 0; log shows `== tools/test_sublimite_win.sh` then `all passed`.
- `make -j8 all`: exit 2, NOT from this change: `tools/win_quiet_run.c:15: fatal error: windows.h` (commit d451648, edit-177r.10; the Linux `all` globs tools/*.c). Found, not fixed.
- Load/stamp: [AC, desktop Spiral], Tobias using the box; no Windows process was started.

## Found, not fixed
1. `make all` on Linux fails compiling `tools/win_quiet_run.c` (see above); `check` is unaffected.
2. `wslpath -w` is lossy for names Windows cannot hold: `:` and `"` become private-use characters (invisible in a terminal), and a dangling symlink translates to its target. A file literally named `a:3` is therefore passed as an exact existing name (no suffix), but how the exe sees that name over `\\wsl.localhost` was not testable. If the exe does not find it, it will peel `:3` as a line number.
3. The installed copy of the script loses the repo-relative fallback; the per-user install glob or `SUBLIMITE_WIN_EXE` is needed then.

## To verify once sublimite.exe links
- Run through winrun-style isolation is not applicable (the launcher is the product); on Tobias's word: `SUBLIMITE_WIN_EXE=build/win/rel/sublimite.exe sublimite-win /mnt/c/... ~/file:3:2`, `--wait` exit status with `GIT_EDITOR`, `-` stdin, and that the exe resolves a `\\wsl.localhost\...` path with `:line:col` appended (its `os_stat_path` NOENT then suffix peel), plus a new (not yet existing) file in a WSL directory.
- Whether launching with a WSL cwd (UNC) triggers any Windows-side warning from the exe.
