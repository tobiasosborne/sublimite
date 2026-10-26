# Windows port (M3) — scope and decisions

Status: scope v1, 2026-10-25, session 21. Owner: Tobias. Written on the desktop "Spiral" (Windows 10 22H2 build 19045, WSL2 Ubuntu 24.04, Threadripper 3970X, RTX 3090, 5120×1440 at 165 Hz, 96 DPI). Companion documents: `PRD.md` §7 and §9 (M3), `perf/01-perf-target.md` (Target B, §2.8 Windows save, §3 D3D11/DXGI present), `CLAUDE.md`.

## 1. Goal

A native Windows build of sublimité that is as good as or better than Sublime Text 4 on Windows (build 4213, installed at `C:\Program Files\Sublime Text`), from the same source tree as the Linux build. Linux stays a fully supported target: every bead keeps the Linux `make all` and `make check` green.

## 2. Decisions (Tobias, 2026-10-25)

1. **Files live in both places, mostly on Windows drives.** NTFS paths are the primary case and get the full file layer (copy or map, change notification, atomic save). Paths under `\\wsl.localhost\` / `\\wsl$\` are supported too. Whether mapping, change notification and the §2.8 rename recipe work on that share is unverified; a W2 bead measures it and picks a degraded mode where they don't (read-copy instead of map, polling on focus and before save instead of notification).
2. **Linux stays a first-class target.** No Windows bead may change Linux behaviour; Linux implementations behind the new OS layer are today's calls.
3. ~~The Astra vs Sublime drive runs on Tobias's real Windows desktop.~~ **Retired 2026-10-26 (Tobias): the drive is no longer a requirement; it was a bootstrap loop for the first Linux builds.** W3 exits by Tobias's own use. If a drive is ever wanted on Windows it would run Windows-side (`codex.exe` is installed) and take over the screen, since no Xvfb equivalent exists.
4. **Glyphs come from DirectWrite on Windows, with our own rasteriser as the fallback.** DirectWrite also supplies font fallback (replacing `fc-match`). Our rasteriser (`src/font`) stays the path when DirectWrite is missing or fails, and for the baked ASCII atlas if DirectWrite's output is not wanted there.

Settled decisions that still hold (law 5): C11, no toolkit (raw Win32 here, as raw xcb on Linux), static link of our code, piece tree, UI-thread ownership with `src/work` mailboxes, no malloc on the typing path.

## 3. Development model

- **Build in WSL, run on Windows.** Cross-compile with llvm-mingw (clang, target `x86_64-w64-windows-gnu`), installed as a pinned release tarball under `~/opt` (no sudo). WSL interop runs the `.exe` directly; it opens on the Windows desktop and its exit code returns to the WSL shell.
- **Why not MSVC** (Visual Studio 2022 Community, MSVC 14.44, is installed): the code uses `<stdatomic.h>` across modules (still experimental in MSVC), `__builtin_*` in 12 files and `__attribute__` in 8 (M: `grep -lE` over `src/`). clang keeps one compiler dialect on both targets.
- **Sanitizers:** llvm-mingw supports ASan and UBSan on x86_64 Windows, so the `make check` rule (law 1) has a Windows equivalent. Whether LeakSanitizer is available there is to be checked in W0.1.
- **Test execution (Tobias, 2026-10-25):** the Windows drives are mounted read-only in WSL by choice (`/etc/wsl.conf` automount `ro`), and nothing on the Linux side writes to them. Test executables run in place from the WSL build folder (`\\wsl.localhost\...`), with their runtime DLLs copied next to them in that folder. A test that needs NTFS scratch files creates them itself, as a Windows process, under `%TEMP%\sublimite-test-*`, and removes them afterwards, so file-layer tests exercise NTFS rather than the `\\wsl.localhost` share (decision 1 makes that share a separate, measured case).
- **Windows-side helpers:** a Claude Code session on Windows (`claude.exe` is installed) is available for anything that has to run natively and interactively; it is the exception, not the default.
- **Shaders:** HLSL is compiled at build time with `fxc.exe` from the installed Windows Kits through interop and committed as a bytecode header, or compiled at init on a worker with `d3dcompiler_47.dll`; W1 decides.

## 4. What carries over unchanged

Pure C11 with no OS calls, or only a clock read: `utf8`, `scan` (SSE2 intrinsics compile under clang), `piece`, `undo`, `lineidx`, `layout`, `view`, `find`, `hl` and the vendored tree-sitter grammars, `tabs`, `minimap`, `menu`, `palette`, `indent`, the rasteriser core of `font`. The frozen render contract `src/render/render.h` names no platform type (`struct plat` is opaque to it), so a Windows renderer is one more `render_backend_ops`. Key handling is data keyed by XKB keysyms (`keys.h`: `uint32_t keysym; /* XKB_KEY_* */`); the Windows platform layer translates into keysyms so the keymap, config and every `XKB_KEY_*` comparison in `src/editor` stay as they are.

## 5. Work, by layer

### 5.1 OS layer (new module `src/os`, one header `os.h`)
POSIX call sites outside `src/x11` and `src/wayland`: about 745 (M: `grep -ohE` of a list of file, thread, clock, wakeup, socket, spawn and mapping calls, per module: journal 215, editor 162, file 77, ipc 58, font 47, work 45, piece 40, savectl 24, fif 15, main.c 13, gl 10, findui 8, config 7, scroll 5, base 5, undo 4, raster 3, lineidx 3, view/trace/tabs/layout 1 each; total 802 including x11 21 and wayland 36). Approach: `os.h` names the operations the modules actually need; the Linux implementation is today's calls (no behaviour change); the Windows implementation is Win32. Hard parts:

- **Directory-relative operations** (`openat`, `unlinkat`, `renameat2(RENAME_NOREPLACE)`, `fstatat`, `dirfd`: 83 hits in 10 files (M)). Windows has handle-relative opens only through `NtCreateFile`; the layer may hold the directory path plus a handle instead.
- **Rename and delete of open or mapped files.** Windows refuses these unless every handle was opened with `FILE_SHARE_DELETE`; the §2.8 recipe (`SetFileInformationByHandle(FileRenameInfoEx, POSIX_SEMANTICS | REPLACE_IF_EXISTS)`) needs Windows 10 1709 or later, which this box has. Journal rotation and retirement (`src/journal/retire.c`) rely on POSIX rename semantics throughout.
- **Mapped originals and truncation.** The Linux SIGBUS service (`file_bus`, docs/decisions/P1.7c.md §10) becomes a vectored exception handler for `EXCEPTION_IN_PAGE_ERROR` over the same fixed mapping registry. Windows also lets an editor deny write sharing while a file is mapped; perf §2.14 calls that a product choice, not the default.
- **Locks:** `flock` → `LockFileEx`. **Durability:** `fdatasync` → `FlushFileBuffers`; no directory flush exists (perf §2.8 states the gap). **Clone:** `ioctl(FICLONE)` has no NTFS equivalent; plain copy. **Extended attributes** on save (`src/file/file.c` copies xattrs) → copy the security descriptor and file attributes. **Random:** `getrandom` → `BCryptGenRandom`.
- **Change detection:** `inotify` → `ReadDirectoryChangesW` on the parent directory, plus the existing identity check on save, focus and bulk operations.
- **Wakeups:** `eventfd`/`timerfd`/`epoll`/`signalfd` (98 hits in 27 files (M)) → event handles, waitable timers and `MsgWaitForMultipleObjectsEx` in the window loop.
- **Paths:** UTF-8 everywhere inside, UTF-16 at the Win32 boundary, long-path prefix where needed; `XDG_*` profile and config directories → `%LOCALAPPDATA%\sublimite` and `%APPDATA%\sublimite`.
- **Malloc guard:** `src/base/malloc_guard.c` interposes glibc's `__libc_*`; on Windows the guard wraps our own calls at link time (`--wrap=malloc` in lld's mingw mode) and counts those; Win32 and DirectWrite allocations go through `HeapAlloc` and are outside it.

### 5.2 Platform: `src/win32` implementing `x11/plat.h`
For scale, the X11 backend is 3,008 lines and Wayland 1,115 (M: `wc -l` of each directory). Scope: window with per-monitor DPI awareness v2 (`WM_DPICHANGED`), `WM_KEYDOWN`/`WM_CHAR` → XKB keysym plus UTF-8 text, modifiers, mouse, high-resolution wheel and precision-touchpad deltas (sub-120 `WM_MOUSEWHEEL` steps), focus, close, clipboard (`CF_UNICODETEXT`; Windows has no PRIMARY selection: `PLAT_CLIP_PRIMARY` is a no-op), key repeat (Windows supplies repeats; the contract's repeat flag maps to them). Traps: **AltGr arrives as Ctrl+Alt** on the German layout (AltGr+Q = `@` must insert, never fire a Ctrl+Alt chord); dead keys via `ToUnicodeEx`. Cleanups the port forces: `src/editor/display_size.c` queries X11 RandR directly and moves behind the platform layer; `xkb_keysym_from_name` (libxkbcommon, used by `src/keys/keys.c` and `src/config/settings.c`) gets a generated name table; `struct plat` carries X11 fields that become backend-private.

### 5.3 Renderer: Direct3D 11 backend
Modelled on the GL renderer (`src/gl/renderer.inc`, 1,358 lines (M)): one instanced quad per cell from the frozen cell grid, flip-model swapchain with a frame-latency waitable object and maximum latency 1 (perf §3), `D3D11_QUERY_EVENT` for T5 (perf §2.2), DXGI present statistics for T6. GL through WGL is not chosen: it presents through DWM without the waitable flip-model path. This display's refresh period is 6.06 ms (P: 1000 / 165 Hz), against 11.1 ms on Target A's 90 Hz panel (P: 1000 / 90), so the frame budget here is tighter than the one the Linux renderers were tuned for. The CPU raster kernel (`src/raster/raster_kernel.c`, `pixels.c`) is portable; its XShm present is replaced by a texture upload into the same swapchain, as the fallback when no D3D11 device exists (WARP covers machines without a GPU).

**Glyphs (decision 4):** DirectWrite rasterises into our R8 atlas pages (grayscale antialiasing, `IDWriteGlyphRunAnalysis`), and `IDWriteFontFallback::MapCharacters` replaces `fc-match` for fallback discovery, on a worker as on Linux. ClearType subpixel output needs three coverage channels; `render.h` defines coverage as linear R8 only, so subpixel text would be a contract bead (P2.0 rule: "changes require a new bead"). Which antialiasing Sublime uses on this screen is checked against the running Sublime before that bead is considered.

### 5.4 Windows integration
`sublimite.exe path[:line[:col]]`, `-` for stdin, single instance over a named pipe (replacing the Unix socket in `src/ipc`), `--wait` for `git`, per-user file associations and "Open with" under `HKCU\Software\Classes` (no admin), a launcher in WSL that translates paths with `wslpath -w` (its command name is an open question: `sublimite` in WSL is the Linux build).

### 5.5 Tests
102 test files, 56,799 lines (M: `wc -l tests/*.c`); 26 use `fork`/`kill`/`waitpid` (crash, kill and data-loss tests) and 23 need an X display (M: `grep -lE`). Process tests move to `CreateProcess`/`TerminateProcess` through `os.h`. Display tests on Windows open windows on the live desktop; they get windows that never take focus, and render tests can use the WARP device so they do not touch the GPU Tobias is using.

## 6. Phases

| | Scope | Exit |
|---|---|---|
| **W0 core** | toolchain; `os.h` spec with Linux and Windows implementations and a conformance suite; core modules migrated to `os.h` | the core suites (base, trace, work, utf8, scan, piece, undo, lineidx, layout, view, find, hl) pass as Windows executables in release and sanitizer builds; Linux `make check` unchanged |
| **W1 glass** | `src/win32` platform, D3D11 backend, DirectWrite glyphs and fallback | typing in one file on the Windows desktop; the G1 typing-latency guard runs on Windows |
| **W2 editor** | file, save, journal and hot exit, savectl, ipc, font discovery, editor loop on `os.h`; `\\wsl.localhost` measurement and degraded mode | the editor suites pass on Windows; hot exit survives `TerminateProcess` |
| **W3 daily** | Windows integration (§5.4), installer-free per-user registration | `make check` green on both targets, then Tobias's own use declares M3 done or lists blockers (milestones exit by use; the Astra drive was retired 2026-10-26) |

## 7. Open questions
- The WSL launcher's command name.
- Whether to deny write sharing on mapped originals (perf §2.14).
- Whether DirectWrite also renders the ASCII atlas, or only non-ASCII and fallback glyphs.
- Shader compilation: build time (`fxc.exe`) or init time (`d3dcompiler_47.dll`).
