# edit-177r.6 — editor dependency modules on Windows

## Inventory before editing

(M) Source/header include and call scan, followed by reading each matching implementation, before production edits. The nine modules are the brief’s enumerated scope (G).

| Module | Call family found | Planned mapping |
|---|---|---|
| config/runtime.c | getenv(XDG_CONFIG_HOME/HOME), absolute-path validation | config_posix.c retains the exact Linux resolver; config_win.c uses os_app_dir(OS_APP_CONFIG). Validation uses os_path_is_absolute |
| config/runtime.c | mkdir(0700), stat directory | os_mkdir, os_stat_path |
| config/runtime.c | open(READ/CLOEXEC/NONBLOCK/NOFOLLOW), fstat, pread, close | os_file_open, os_file_stat, os_file_pread, os_file_close; ISDIR remains CONFIG_ERR_IO |
| config/runtime.c | aligned_alloc/free | config_posix.c retains aligned_alloc/free; config_win.c uses _aligned_malloc/_aligned_free for work_pool alignment |
| config/runtime.c | work_pool_eventfd compatibility | Linux guard for config_runtime_eventfd; no config_runtime_wake (owned by W2.4c) |
| fif/worker.c, private.h | opendir, dirfd/fstat, readdir, closedir; dev_t/ino_t | os_dir_open, os_identity; fif_posix.c retains native iteration/checked closes and fstat; fif_win.c uses os_stat_at(".") and os_dir_iter_open/next/close |
| fif/worker.c | lstat descendants, stat root | os_stat_at(NOFOLLOW) via opened parent, os_stat_path(root); skip descendant OS_KIND_LINK, follow explicit root |
| fif/worker.c | open(READ/CLOEXEC/NOFOLLOW/NONBLOCK), fstat, read, close | os_file_open_at via opened parent, os_file_stat, os_file_read, fif_close_file (native checked close on Linux, os_file_close on Windows); ISDIR counts the existing nonregular-file error |
| fif headers/filter | PATH_MAX | OS_PATH_MAX, identical Linux capacity; Windows avoids CRT’s shorter PATH_MAX |
| findui/findui.c | work_pool_eventfd used to test active pool | work_pool_wake presence, same active/inactive outcome |
| findui/findui.c | clock_gettime(MONOTONIC) | os_now_ns |
| findui/findui.c, plan.c | open(/tmp,O_TMPFILE/RDWR/CLOEXEC,0600), close, ftruncate, pread/pwrite | os_dir_open + os_file_open_anon, os_file_close, os_file_set_size, os_file_pread/pwrite |
| findui/worker.c | unused time.h include | remove |
| tabs/tabs.c | sysconf(PAGESIZE) | os_page_size |
| keys, menu, minimap, palette, indent | no POSIX calls | unchanged pure C / SSE2, generated keysym names already portable |

## Platform choices

Windows config uses os_app_dir(OS_APP_CONFIG): `%APPDATA%\sublimite\config.toml`, with the same OS resolver family as journal’s OS_APP_DATA (`%LOCALAPPDATA%\sublimite`). Roaming settings are user preferences; journal state remains local. Linux retains XDG_CONFIG_HOME then HOME/.config. Missing roots remain CONFIG_ERR_ARG and insufficient output space CONFIG_ERR_LIMIT.

Fif currently follows a symlink explicitly supplied as project root. Beneath it, lstat skips **all symlinks**, to both files and directories, counting DONE.symlinks; `.git` is omitted before stat. Windows matches name-surrogate reparse points through OS_KIND_LINK; data-serving non-surrogate reparse tags remain files per frozen os.h. Traversal sorting, limits, cancellation and nonregular/error classification stay unchanged. Parent-relative stat/open replaces path acquisition at descendant file sites.

Fif recognizes drive/UNC absolute filters and treats slash/backslash as equivalent in their project-root prefix. After stripping that prefix, native absolute-rule separators become `/`; relative glob backslash escapes keep their original meaning. Trailing native project separators are trimmed, while a drive-root separator is retained (`C:` alone has different semantics). The Windows-only native-path regression selects the same four a.txt hits as the original slash-path fixture (E: counted in its literal).

Findui retains `/tmp` on Linux (the existing spool location, regardless of TMPDIR); Windows uses os_temp_dir. Each slot’s unnamed file is acquired at setup and reused without typing-path allocation.

## Verification status

Inventory complete; red compile recorded and green win-compile passed. Final Linux all/check and Windows check passed (M: 110 Linux binaries; 59 release + 59 sanitizer Windows binaries, from make summaries; full evidence in docs/worker-reports/edit-177r.6-s13.md). No frozen OS header changes and no editor/main changes.

## Test portability

The existing config/fif/findui/tabs/keys/menu/minimap/palette/indent suites build as release and ASan/UBSan Windows executables. Test-only Windows fixtures create scratch under `%TEMP%/sublimite-test-deps-<random>` and remove it. Fif’s directory links are junctions (no symlink privilege), covering root-follow and descendant-skip with the same hit/symlink expectations. Only POSIX chmod’s unreadable-directory witness is skipped with a reason on Windows. Protected-page oracles in findui/minimap/indent use VirtualAlloc/VirtualProtect/VirtualFree with the original assertions. Findui skips only editor-dependent panel clipboard/chord integration, with an explicit reason; standalone panel, worker, replacement, journal and protected-spool tests remain. Keys reports its existing Windows skip of the Linux libxkbcommon differential name oracle; independent binding rows remain.

(M) llvm-objdump reports replacement_large_match’s release frame as `0x2002e8` bytes (the fixture contains two `1024*1024` byte arrays plus locals), exceeding PE’s default reserve. Both findui test variants use the existing layout suite’s stack setting, `8388608 = 8192*1024` bytes, matching the host Linux stack. Fif’s sanitizer main also needs this reserve: llvm-objdump reports `0x14f838` bytes after fixture inlining (M), and the native ASan run reports stack overflow with the default. No production stack reserve is changed.

The frozen close APIs return void. Config already ignored close results. Fif counted failed closedir/close, so fif_posix.c retains a native iterator and checked file close behind module-private helpers; fif_win.c uses os_dir_iter_* and os_file_close. Linux directory identity still uses fstat of the opened directory via a platform-only borrowed file wrapper, avoiding a new search-permission requirement from fstatat("."). No private OS fields are accessed and no OS header extension is made.
