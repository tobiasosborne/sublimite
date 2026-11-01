# Windows parity audit, 2026-11-01 (edit-177r.12; Codex gpt-6.1-sol xhigh, read-only, on W2.4c tree 37150c3)

Triage: coordinator, same day. Items became edit-177r.13 to edit-177r.26 (law 8).

**Linking and typing will establish a Windows smoke test. Daily parity still needs editor-level recovery and file-safety evidence, a resilient presentation lifecycle, large-file responsiveness checks, and the missing Windows desktop integration.** Most editing logic is already shared.

This is a source audit of the supplied tree. I changed nothing and ran no build, application, or test executable. `works` below means implemented and exercised by a selected Windows test; it does **not** claim a fresh passing run. `untested` means implementation exists but Windows coverage does not establish the stated user behaviour.

**1. Matrix**

The inventory starts from `PRD.md:39`, `src/config/actions.inc:2`, the bindings and capability flags in `src/keys/keys.c`, and the editor dispatcher. Windows includes component modules but excludes `editor` from `WIN_MODULES`; its editor-loop gate is compilation only (`mk/win.mk:14`, `mk/win.mk:284`). That is the common coverage evidence for the untested editor rows below.

*Launch and routing*

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| Application startup, opening, typing, saving | Full application route | **blocked by .9** — application is not a Windows build target yet | `src/main.c:277`; `mk/win.mk:56`; `mk/win.mk:284` |
| Unicode command-line arguments | Native CLI arguments | **untested** — explicit UTF-16→UTF-8 conversion exists; application CLI suite absent | `src/main.c:329`; `tests/cli_test.c:1`; `mk/win.mk:20` |
| Paths with line/column suffixes | Parsed and routed to tabs | **works** at parser/IPC level; actual caret placement remains untested | `src/ipc/args.c:203`; `tests/win/ipc_win_test.c:45`; `src/editor/buffers.c:865` |
| Relative, drive, UNC and long paths | POSIX canonicalization | **untested** as complete application routes; Windows canonicalization exists | `src/ipc/args.c:23`; `src/editor/commands.c:14`; `src/os/os.h:610` |
| Stdin into a buffer | CLI reads and opens stdin | **untested** end to end; binary stdin reading itself is tested, including CRT newline/EOF handling | `src/main.c:125`; `src/main.c:142`; `tests/win/ipc_win_test.c:62`; `src/editor/buffers.c:878` |
| Single-instance transport | Unix socket | **works** — named-pipe requests, duplicate server, acknowledgement, rejection and endpoint reuse tested | `src/ipc/ipc_win.c:81`; `tests/win/ipc_win_test.c:197` |
| New invocation opens tabs in running editor | IPC reaches editor request handling | **untested** — Windows transport tests stop before the application | `src/main.c:115`; `src/editor/buffers.c:849`; `tests/win/ipc_win_test.c:213` |
| `--wait`, including forwarding to existing editor | Launcher waits for tab closure | **untested** end to end; launcher transport and process helpers are tested | `src/main.c:28`; `src/main.c:118`; `tests/win/ipc_win_test.c:138`; `tests/win/ipc_win_test.c:213` |
| Separate instance option | `--new-instance` supported | **untested** application route | `src/main.c:63`; `src/main.c:115` |
| Font CLI overrides | Family, file/collection index, fallback chain | **untested** application route | `src/main.c:233`; `src/ipc/args.c:184`; `mk/win.mk:20` |

*Editing and discoverability*

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| Default bindings, chord lookup, configurable key names | Shared binding tables | **works** at component level; Windows skips the libxkbcommon differential oracle | `src/keys/keys.c:349`; `tests/keys_test.c:527`; `mk/win.mk:19` |
| Text insertion, backspace/delete, word deletion, overwrite | Editor executors present | **untested** through Windows editor | `src/editor/input.c:449`; `src/editor/input.c:621`; `src/editor/input.c:1175` |
| Character, word, subword, page and document navigation | Shared view/editor execution | **untested** through Windows editor; view component suite runs | `src/editor/input.c:611`; `src/editor/input.c:749`; `mk/win.mk:19` |
| Shift selection, select all/line, bracket selection | Editor execution present | **untested** | `src/editor/input.c:614`; `src/editor/input.c:749`; `src/editor/input.c:1215` |
| Mouse placement, drag, double/triple click, gutter selection | Editor pointer handling | **untested** as selection behaviour; native pointer events are tested separately | `src/editor/input.c:769`; `src/editor/editor.c:721`; `tests/win/win32_plat_test.c:287` |
| Select word / skip next occurrence | Single-selection subset | **untested**; same subset, not multicursor support | `src/editor/input.c:607`; `src/keys/keys.h:107` |
| Undo, redo, redo-or-repeat | Shared history and dispatch | **untested** through editor; undo component suite runs | `src/editor/input.c:1140`; `src/editor/input.c:1177`; `mk/win.mk:19` |
| Soft undo/redo of selection | Editor execution present | **untested** | `src/editor/input.c:1179`; `src/keys/keys.c:105`; `src/keys/keys.c:161` |
| Duplicate/delete/move/join lines; insert line before/after | Editor execution present | **untested** | `src/editor/input.c:588` |
| Delete to line boundaries, transpose, sort, upper/lower case | Editor execution present | **untested** | `src/editor/input.c:595` |
| Marks, select/swap/delete to mark, yank | Editor execution present | **untested** | `src/editor/input.c:615` |
| Auto-indent, indent/unindent, indentation detection, comments | Shared detection/planning plus editor mutation | **untested** through editor; indent component suite runs | `src/editor/buffers.c:271`; `src/editor/input.c:410`; `src/editor/input.c:599`; `mk/win.mk:19` |
| Word wrap and visual navigation | Layout/view/editor implementation; configuration controls wrap | **untested** as editor behaviour; layout/view components run | `src/config/settings.c:96`; `src/config/settings.c:143`; `src/editor/input.c:1605` |
| Line numbers, bracket match, trailing-whitespace display | Shared layout and theme application | **untested** on Windows editor | `src/editor/preferences.c:173`; `src/config/settings.c:89`; `tests/editor_p4_test.c:188` |
| Go to line, preview, accept/cancel | Decimal line prompt implemented | **untested** | `src/editor/input.c:809`; `src/editor/input.c:837`; `src/editor/input.c:859` |
| Go to byte offset / percentage from a prompt | PRD requirement; no such prompt found. Internal byte seeking exists | **missing**, also on Linux; not a Windows parity task | `PRD.md:56`; `src/editor/input.c:840`; `src/editor/large.c:441` |
| Find engine: literal, regex, case, whole word, streaming/cancellation | Shared worker engine | **works** at component level | `mk/win.mk:19`; `tests/findui_test.c:1424` |
| Find/replace panel, highlights, next/previous, replace one/all, slurp selection | Editor integration present | **untested** — Windows explicitly omits panel/chord/clipboard cases | `src/editor/findpanel.c:330`; `src/editor/findpanel.c:487`; `src/editor/findpanel.c:503`; `tests/findui_test.c:1406` |
| Find in files traversal/filter/search | Shared worker with Windows directory iterator | **works** at component level; unreadable-directory ACL behaviour lacks the POSIX fixture’s replacement | `src/fif/fif_win.c:3`; `tests/fif_test.c:234`; `mk/win.mk:19` |
| Find Results buffer, opening hits, next/previous result | Linux editor implementation exists despite older PRD non-goal wording | **untested** | `src/editor/findpanel.c:474`; `src/editor/findpanel.c:482`; `src/editor/input.c:1129` |
| New file, open-path prompt, Save As prompt | Shared editor prompts and maintenance operations | **untested** | `src/editor/input.c:1158`; `src/editor/input.c:1190`; `src/editor/commands.c:298` |
| Tabs: selection, adjacent/MRU cycling, direct index | Shared tabs model and editor execution | **untested** through editor; tabs component suite runs | `src/editor/input.c:1180`; `src/editor/buffers.c:444`; `mk/win.mk:19` |
| Tab reorder by dragging | Shared editor pointer implementation | **untested** | `src/editor/paint.c:428` |
| Close modified tab, save/discard/cancel, retained closed tabs, reopen | Linux close safety exists | **untested** — Windows does not run editor close/session suites | `src/editor/input.c:1067`; `src/editor/input.c:1183`; `src/editor/buffers.c:994`; `tests/editor_close_test.c:27` |
| Command palette | Shared action names, filtering, bindings and editor panel | **untested** as a usable panel; palette model suite runs | `src/config/actions.inc:2`; `src/palette/palette.c:77`; `src/editor/input.c:1126`; `mk/win.mk:19` |
| Alt menu, mnemonics, toggle-menu action | Shared menu/editor implementation | **untested** as a usable menu; menu model suite runs | `src/editor/input.c:1069`; `src/editor/input.c:1127`; `src/keys/keys.c:272`; `mk/win.mk:19` |
| Cut/copy/paste, large paste, line-ending normalization and undo | Shared editor clipboard implementation | **untested** through editor; native clipboard coverage alone does not establish safe cut/paste | `src/editor/input.c:1192`; `src/editor/clip.c:283`; `src/editor/clip.c:308` |
| Syntax highlighting, stale-result rejection, editability before parsing finishes | Shared editor/worker implementation | **untested** through editor; grammar/parser suites run, mapped-fault worker case skips | `src/editor/highlight.c:174`; `src/editor/highlight.c:188`; `mk/win.mk:43`; `tests/hl_worker_test.c:73` |
| Font zoom/reset | Shared asynchronous editor implementation | **untested** | `src/editor/input.c:1173`; `src/editor/font_size.c:170` |

The action table contains commands Linux itself cannot execute. These must remain distinguishable from Windows regressions:

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| Multicursor/column selection, split selections, select all occurrences | Capability-marked unavailable | **stub**, same Linux limit | `src/keys/keys.c:154`; `src/keys/keys.c:204`; `src/keys/keys.c:216`; `src/editor/input.c:1202` |
| Paste-and-indent and paste history | Unavailable | **stub**, same Linux limit | `src/keys/keys.c:162`; `src/keys/keys.c:248`; `src/editor/input.c:1196` |
| Quick file/symbol/workspace/definition/reference navigation and jump history | Unavailable | **stub**, same Linux limit | `src/config/actions.inc:53`; `src/keys/keys.c:100`; `src/keys/keys.c:158`; `src/editor/input.c:1202` |
| Reindent, smart/scope selection, tag completion/snippets, macros, paragraph reflow | Unavailable binding families | **stub**, same Linux limit | `src/config/actions.inc:42`; `src/config/actions.inc:127`; `src/keys/keys.c:197`; `src/keys/keys.c:221`; `src/keys/keys.c:237` |
| Bookmarks, spelling, modification/diff/revert commands | Unavailable | **stub**, same Linux limit | `src/config/actions.inc:81`; `src/config/actions.inc:121`; `src/keys/keys.c:125`; `src/keys/keys.c:225` |
| Panes/groups/sidebar, extended tab-selection commands, new window, fullscreen/distraction-free | Unavailable | **stub**, same Linux limit | `src/config/actions.inc:104`; `src/config/actions.inc:146`; `src/keys/keys.c:35`; `src/keys/keys.c:206`; `src/keys/keys.c:254` |
| Build/console/autocomplete/show-scope/context menu; separate incremental-find action; previous-line selection | Unavailable; ordinary find field still searches incrementally | **stub**, same Linux limit | `src/config/actions.inc:97`; `src/keys/keys.c:26`; `src/keys/keys.c:93`; `src/keys/keys.c:195`; `src/editor/input.c:1202` |

*Files, recovery and resource limits*

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| UTF-8/invalid-byte preservation; LF/CRLF detection and byte-exact save | Shared byte-oriented buffer/file implementation | **works** at file layer; editor edit/save round trips remain untested | `tests/file_win_cases.inc:57`; `tests/file_win_cases.inc:139`; `mk/win.mk:19` |
| Optional final-newline enforcement | PRD mentions it; no control or implementation found | **unknown** on both. A reachable setting/executor and test would settle it | `PRD.md:46`; `src/config/settings.c:89`; `src/editor/commands.c:298` |
| Copy/mapped acquisition, prefix, threshold, retained backing | Implemented | **works** at file layer | `tests/file_win_cases.inc:82`; `tests/file_win_cases.inc:99`; `tests/file_os_cases.inc:113` |
| Large-file first viewport, background index, estimated line numbers | Editor implementation exists | **untested** on Windows | `src/editor/buffers.c:289`; `src/editor/editor.c:268`; `src/editor/editor.c:288`; `mk/win.mk:20` |
| Atomic replacement, asynchronous completion, sharing violations | POSIX save transaction | **works** at file/controller level, including preserving original target and dirty edits on sharing failure | `tests/file_win_cases.inc:139`; `tests/file_win_cases.inc:277`; `tests/win/savectl_test.c:40` |
| Save indication and editor/journal coordination | Editor only reports completion after controller progress | **untested** in Windows editor | `src/editor/editor.c:486`; `src/editor/session.c:596`; `src/editor/session.c:686` |
| Owner/group/DACL/integrity label and ordinary attributes | POSIX metadata preservation | **works** for implemented Windows metadata transaction | `src/file/file_win.c:417`; `tests/win/file_win_test.c:233`; `tests/win/file_win_test.c:304`; `tests/win/file_win_test.c:325` |
| Saving EFS files or files with named NTFS streams, including `Zone.Identifier` | Linux has its own metadata transaction | **stub** — deliberately refuses before creating a temporary; preservation transaction is missing | `src/file/file_win.c:318`; `tests/win/file_win_test.c:244`; `tests/win/file_win_test.c:266` |
| Directory/rename durability across power loss | Directory sync completes the Linux recipe | **stub** for directory flush; accepted Windows endpoint is data-flushed atomic replacement, with rename durability provisional | `src/os/os_win.c:934`; `perf/01-perf-target.md:391`; `perf/01-perf-target.md:397` |
| Parent-directory change notifications, replacements and absent targets | inotify plus identity checks | **works** at file layer; short-name notification gap remains | `src/file/file_win.c:62`; `src/file/file_win.c:189`; `tests/file_win_cases.inc:218` |
| Silent reload of clean buffer; conflict banner / keep / reload for dirty buffer | Shared editor policy | **untested** as user behaviour | `src/editor/editor.c:707`; `src/editor/input.c:1089`; `src/editor/buffers.c:973`; `tests/win/savectl_reload_test.c:3` |
| Reload private backing and clone fallback | POSIX clone/copy route | **works** — Windows tests actual copy/map/install and view clamping | `tests/win/savectl_reload_test.c:21`; `src/os/os_win.c:832` |
| Complete identity after reload/adoption | File layer carries complete identity | **untested**, with a current integration defect: controller conversion drops `ino_hi` despite the shared converter supporting it | `src/file/file.c:230`; `src/savectl/savectl.c:107`; `src/savectl/savectl.c:141`; `src/savectl/savectl.c:887` |
| Mapping-fault containment | SIGBUS service | **works** for native fault-service fixtures; real network/removable backing loss remains unknown | `tests/file_win_cases.inc:99`; `tests/file_win_cases.inc:343`; `src/os/os.h:692`; `.beads/issues.jsonl:395` in the main checkout |
| Journal transport, replay boundaries and process death | Journal suites | **works** at journal layer, including Windows process-kill campaigns | `tests/win/journal_kill_test.c:6`; `tests/win/journal_kill_test.c:115`; `tests/win/journal_kill_test.c:120` |
| Hot exit of the actual editor and restart with unsaved tabs | Linux integration suites | **untested** beyond .9’s intended initial integration | `mk/win.mk:228`; `src/editor/session.c:497`; `src/editor/recovery.c:995` |
| Creator/session lease ownership and cleanup | Consistent descriptor lifecycle | **blocked by .9** — Windows restore rejects the supplied lease; `open.c` still treats `os_file` fields as integers | `src/editor/recovery.c:1005`; `src/editor/private.h:370`; `src/editor/open.c:36`; `src/editor/open.c:48` |
| Restore tab order, cursor/selection/scroll/window; prefer unsaved tabs when capacity is exceeded | Current Linux recovery policy | **untested** on Windows; source contains the newer preservation policy | `src/editor/recovery.c:557`; `src/editor/recovery.c:641`; `src/editor/recovery.c:779`; `src/editor/recovery.c:826` |
| Open bursts, resource exhaustion and recoverable refusal | Raised Linux descriptor limit and editor refusal notice | **blocked by .9** for missing Windows `os_fd_limit_raise`; native exhaustion handling remains untested and checks POSIX error numbers | `src/main.c:70`; `src/os/os.h:796`; `src/os/os_posix.c:1022`; `src/editor/buffers.c:301`; `src/editor/buffers.c:892` |
| WSL-share editing, notifications, mapping, replacement and degraded mode | Linux local filesystem route | **unknown** — explicitly unverified; no share-specific degraded mode found. Native share tests must establish capabilities and select safe fallbacks | `docs/design/windows-port.md:11`; `docs/design/windows-port.md:66`; `src/file/file_win.c:62`; `src/os/os.h:692` |

*Rendering, fonts and appearance*

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| Cell colours, R8 coverage, inverse, underline, wide glyphs, damage and retained rendering | GL/raster contract | **works** at D3D11 component level; native suite currently depends on successful presentation | `src/render/render.h:30`; `src/d3d11/d3d11_win.c:214`; `tests/win/d3d11_test.c:213`; `tests/win/d3d11_test.c:222` |
| Surface background and fractional-cell margins | Current render-contract feature | **works** in D3D11 implementation and pixel oracle | `src/render/render.h:107`; `src/d3d11/d3d11_win.c:507`; `tests/win/d3d11_test.c:307`; `tests/win/d3d11_test.c:329` |
| Fractional scrolling, overscan, fixed chrome | GL/raster support | **works** at D3D11 component level | `src/d3d11/d3d11_win.c:527`; `tests/win/d3d11_test.c:331` |
| Independent minimap RGB region: change, move, shrink, remove, restore | Shared render contract | **works** at D3D11 component level | `src/d3d11/d3d11_win.c:582`; `tests/win/d3d11_test.c:349` |
| Actual minimap: index-derived content, click/drag, accurate viewport | Shared editor implementation | **untested** on Windows; minimap component tests run | `src/editor/paint.c:329`; `src/editor/paint.c:420`; `mk/win.mk:19` |
| New palette tokens, syntax colours and cached theme colours | Current Linux design | **untested** as complete Windows editor appearance; theme ladder suite is omitted | `src/config/theme.c:12`; `src/config/theme.c:45`; `src/editor/preferences.c:173`; `tests/config_theme_test.c:36` |
| Insets, gutter spacing, aligned panels, tab tone, spacer row, placeholders | Current Linux design implementation | **untested** on Windows. These are largely shared cell composition, not missing D3D primitives | `src/editor/open.c:237`; `src/editor/paint.c:145`; `src/editor/paint.c:182`; `tests/editor_design_test.c:44` |
| Hover ink for tabs, find controls, palette/menu and minimap | Shared hover/composition | **untested** on Windows; leave event is missing separately | `src/editor/hover.c:56`; `src/editor/paint.c:169`; `src/editor/paint.c:382`; `tests/editor_design_test.c:95` |
| Bar caret | Deferred Linux design candidate | **missing** on both; not a Windows regression | `docs/decisions/edit-cxho.24.md:64`; `src/render/render.h:20` |
| Hardware failure → software rendering | Linux GL→raster; Windows hardware→WARP | **works** for D3D11 device selection; complete startup/recovery experience remains untested | `src/d3d11/d3d11_win.c:149`; `tests/win/d3d11_test.c:252` |
| Responsive startup and usable failure fallback | Linux has raster bootstrap/probe | **blocked by .9** for integration; Windows explicitly disables that bootstrap and has no independent raster application path | `src/editor/open.c:84`; `src/editor/backend.c:14`; `src/editor/backend.c:66` |
| Presentation timing, refresh observation, suspension and recovery | Linux platform/backend integration | **blocked by .9** — isolated D3D protocol exists, but editor recovery, suspend and latency-handle consumers are absent | `src/d3d11/d3d11.h:28`; `src/editor/editor.c:364`; `src/editor/refresh.c:35`; `src/editor/editor.c:122` |
| Full input→mutation→layout→submit allocation/typing guard | Linux editor tests | **blocked by .9** — platform and renderer allocation tests do not cover the editor pipeline | `tests/editor_test.c:34`; `tests/editor_test.c:1809`; `tests/win/win32_plat_test.c:337`; `mk/win.mk:20` |
| Resident large-file copies retain interactive scheduling | Linux residency probe can choose foreground lane | **missing** Windows optimization; Windows always uses bulk. Actual lag is unmeasured | `src/scroll/resident.c:98`; `src/scroll/resident.c:122`; `tests/work_foreground_test.c:310` |
| Embedded ASCII font | Baked/shared rasterizer | **works** at font component level; ASCII deliberately stays on the existing rasterizer | `src/font/font.c:849`; `tests/font_test.c:1`; `mk/win.mk:20` |
| DirectWrite glyphs, grayscale coverage, fallback discovery and rasterizer fallback | Linux font discovery/raster path | **works** at component level | `src/font/font.c:854`; `src/font/discovery_win.c:123`; `tests/win/font_dwrite_test.c:12`; `tests/win/font_dwrite_r2_test.c:277` |
| Combining marks, CJK/emoji, custom fonts, collection faces and zoom in the editor | Shared layout/font/editor pipeline | **untested** end to end; native component tests cover useful subsets, not all placement/face-selection cases | `src/font/unicode.c:52`; `src/font/selection.c:30`; `src/editor/fonts.c:9`; `.beads/issues.jsonl:394` in main |
| Hot-reloaded configuration and key overrides | TOML runtime plus editor application | **works** for config runtime; **untested** for application of changes to a live Windows editor | `src/config/config_win.c:5`; `tests/config_test.c:133`; `src/editor/preferences.c:155`; `mk/win.mk:231` |
| Dark native title bar | X11 hint implemented | **missing** — Windows has no `plat_set_dark` definition, although editor calls it | `src/x11/x11.c:637`; `src/x11/plat.h:132`; `src/editor/open.c:300`; `src/editor/preferences.c:172`; `src/win32/win32_plat.c:433` |

*Platform contract and Windows integration*

| Feature | Linux | Windows status | `file:line` evidence |
|---|---|---|---|
| Backend selection, native window access, init/map/shutdown | X11/Wayland implementations | **works** for Windows platform lifecycle | `src/win32/win32_plat.c:433`; `src/win32/win32_plat.c:476`; `tests/win/win32_plat_test.c:830` |
| Expose, resize, focus, close events | Native platform events | **works** at platform level | `src/win32/win32_plat.c:400`; `src/win32/win32_plat.c:412`; `tests/win/win32_plat_test.c:308`; `tests/win/editor_wait_test.c:181` |
| Key/text events, modifiers, AltGr, dead keys, surrogates, keymap changes | XKB route | **works** for tested native/synthetic platform cases; physical layout behaviour is only partly established | `src/win32/win32_plat.c:79`; `src/win32/win32_plat.c:351`; `tests/win/win32_plat_test.c:227`; `tests/win/win32_plat_test.c:591` |
| Repeat configuration, focus cancellation, blink callback | Platform implementation | **works** at platform level; implementation owns repeat and suppresses native duplicate repeats | `src/win32/win32_plat.c:61`; `tests/win/win32_plat_test.c:353` |
| Button/motion events, capture loss, high-resolution wheel | Native implementation | **works** at platform level; editor scrolling/selection remains untested | `src/win32/win32_plat.c:285`; `src/win32/win32_plat.c:312`; `tests/win/win32_plat_test.c:287`; `tests/win/win32_plat_test.c:518` |
| Pointer cursor shape | Linux text/default cursors | **untested** — Windows implements arrow/I-beam selection; no Windows assertion found | `src/win32/win32_plat.c:40`; `src/win32/win32_plat.c:388`; `src/win32/win32_plat.c:498` |
| Pointer leave event / clearing hover | Linux delivers `PLAT_EV_LEAVE` | **missing** — no Windows tracking/leave handler | `src/x11/plat.h:22`; `src/editor/editor.c:728`; `src/win32/win32_plat.c:391`; `.beads/issues.jsonl:282` in main |
| Clipboard ownership/request/result/data and errors | Linux clipboard implementation | **works** at platform level, including contention, newline conversion and rejected embedded NUL | `src/win32/win32_plat.c:537`; `tests/win/win32_plat_test.c:478`; `tests/win/win32_plat_test.c:549`; `tests/win/win32_plat_test.c:575` |
| PRIMARY selection / middle-click paste | Available on Linux | **stub** — intentional Windows platform difference: setting succeeds as no-op, requesting fails | `src/win32/win32_plat.c:540`; `src/win32/win32_clip_win.c:101`; `tests/win/win32_plat_test.c:331` |
| File drag and drop | Linux URI-list event→editor route | **stub** — Windows accessor always returns no data; no drop message handler | `src/editor/editor.c:695`; `src/win32/win32_plat.c:553`; `.beads/issues.jsonl:290` in main |
| Event polling, bounded run, quit, worker wake callbacks and native modal loops | Linux loop | **works** at platform/wait-set level | `src/win32/win32_plat.c:46`; `src/win32/win32_plat.c:603`; `tests/win/win32_plat_test.c:744`; `tests/win/editor_wait_test.c:158` |
| Present-event subscription and completion callback bridge | Linux real presentation integration | **works** as isolated Windows bridge; real editor connection remains **blocked by .9** | `src/win32/win32_plat.c:441`; `src/win32/win32_plat.c:448`; `tests/win/win32_plat_test.c:350`; `src/editor/editor.c:381` |
| Display/work-area/DPI/refresh queries | Platform queries | **works** for queried/synthetic cases; physical mixed-DPI behaviour **unknown** | `src/win32/win32_plat.c:563`; `src/win32/win32_plat.c:576`; `tests/win/win32_plat_test.c:318` |
| DPI transition rescales text and hit geometry | Initial font sizing exists | **blocked by .9** — platform emits resize, editor only changes surface geometry; font DPI stays captured | `src/win32/win32_plat.c:413`; `src/editor/editor.c:702`; `src/editor/font_size.c:140` |
| Console close and session-end notification | Signal/window close route | **works** at bridge/event level; final editor flush/restart remains untested | `src/win32/win32_close_win.c:43`; `tests/win/editor_wait_test.c:185`; `src/main.c:281` |
| Blink stops at idle; idle workers stay asleep | Linux editor policy/tests | **untested** in Windows editor | `src/editor/editor.c:794`; `src/editor/editor.c:881`; `tests/editor_test.c:1815` |
| User config/data directories | XDG profile locations | **works** for Windows directory resolution/config path | `src/os/os_win.c:1243`; `src/config/config_win.c:5`; `tests/config_test.c:136` |
| Installation, shell discovery, file associations and Open With | Linux install/desktop/MIME rules | **missing** Windows delivery/registration route | `Makefile:170`; `docs/design/windows-port.md:54`; `mk/win.mk:56` |
| Application/taskbar/file icon | Linux installed icon assets | **missing** Windows icon resource and class icon assignment | `Makefile:169`; `src/os/utf8.manifest:5`; `src/win32/win32_plat.c:497`; `mk/win.mk:138` |
| WSL launcher with path conversion | Linux executable uses the ordinary command | **missing** — only test runner uses `wslpath`; product launcher remains planned | `docs/design/windows-port.md:55`; `docs/design/windows-port.md:70`; `tools/winrun.sh:20` |
| Supported Windows version and DPI declaration in manifest | Linux does not need these declarations | **missing** manifest declarations; current manifest declares UTF-8 only | `src/os/utf8.manifest:7`; `src/win32/win32_plat.c:471`; `.beads/issues.jsonl:395` in main |

*What the Windows check does not establish*

The suite difference below is derived from `Makefile:62`’s top-level `tests/*_test.c` wildcard, minus the explicit sources and basename substitutions selected at `mk/win.mk:19`. Names below are relative to `tests/`.

| Suites never selected on Windows | Behaviour left unverified |
|---|---|
| All top-level `editor*_test.c`; `modal_matrix_test.c`, `keyboard_guard_test.c`, `dataloss_test.c` | Full editor dispatch, recovery, checkpoint/Save As interactions, prompts, undo branches, large files, highlighting, scrolling and allocation guard |
| `cli_test.c`, `startup_test.c`, `runtime_test.c`, `window_size_test.c` | Actual application startup/routing, launcher lifecycle, window/session geometry |
| `config_editor_test.c`, `config_find_test.c`, `config_theme_test.c` | Live editor preference changes, panel integration, current theme-token contract |
| `font_cli_test.c`, `font_selection_test.c`, `font_size_test.c`, `unicode_render_test.c` | Selected fonts and fallback rendered through the real editor, zoom and Unicode damage/placement |
| `select_test.c`, `wrap_test.c`, `scroll_test.c`, `scroll_display_test.c`, `render_test.c`, `dnd_test.c` | Standalone selection/wrap/scroll/render-contract checks and complete drop route; some lower-level behaviour is covered elsewhere |
| `file_acquire_test.c`, `file_fault_test.c`, `file_kill_test.c` | Original Linux acquisition/fault/crash campaigns; Windows substitutes useful native cases, not the entire integration coverage |
| `journal_test.c`, `journal_cancel_test.c`, `journal_group_test.c`, `journal_hot_exit_test.c`, `journal_retire_test.c`, `ipc_test.c` | Original lifecycle/integration campaigns. Windows journal-kill includes the portable hot-exit source, so this is not absence of all crash testing |
| `gl*_test.c`, `raster_test.c`, `raster_scroll_test.c`, `wayland*_test.c`, `x11*_test.c`, `xcursor_test.c` | Linux backend coverage; their omission is expected, but equivalent Windows *user outcomes* still need coverage |
| `harness_test.c`, `prewake_test.c`, `refproto_test.c`, `refwin_test.c`, `refwin_timeout_test.c`, `replay_test.c` | Supporting measurement/replay protocols and application replay route |

`journal_ack_test`, `journal_kill_test`, `savectl_test` and `savectl_reload_test` use Windows-specific source replacements rather than their top-level Linux sources (`mk/win.mk:187`).

Actual Windows skips and weakened cases matter more than suite names:

| Selected test / check | Skip or limitation | Consequence |
|---|---|---|
| `findui_test.c:1406` | Omits editor field, clipboard, focus, chords, window commands and save-prompt cases | Search engine success does not establish a usable find panel |
| `hl_worker_test.c:73` | Skips faulted mapped snapshot | Highlighting after backing invalidation unverified |
| `lineidx_test.c:416` | Skips combined file-backed ownership census | Mapped index/find/save ownership integration unverified |
| `work_foreground_test.c:310` | Skips real clients and editor jump | Worker isolation under real file/search/font load unverified |
| `file_win_cases.inc:343` | Skips real truncation/SIGBUS and editor/journal/find integration | Native fault trigger is not a network-disconnect or editor-recovery oracle |
| `tests/win/savectl_reload_test.c:27` | Skips syscall-race suites | Short transfers and reload failure/rollback paths remain incomplete |
| `tests/win/ipc_win_test.c:213` | Skips actual main/platform launcher integration | `--wait` helper tests do not prove git/editor use |
| `tests/win/journal_ack_test.c:50` | Skips hard-link/symlink fixtures | Journal alias protections incompletely exercised |
| `fif_test.c:234` | Skips chmod-based inaccessible directory | Needs a Windows ACL fixture, not a claim of equivalent coverage |
| `keys_test.c:527` | Skips libxkbcommon name oracle | Binding expectations run; independent generated-name oracle does not |
| `trace_pairs_test.c:15` | Skips without native Python configuration | Offline trace-join checks can silently disappear |
| `lineidx_test.c:885` | Skips absent corpus | A green run need not have exercised the large-file corpus |
| `tests/win/win32_plat_test.c:261`, `:647`, `:664`, `:848` | German layout conditional; foreground Unicode/SendInput conditional; real IME skipped | Physical/native input coverage is narrower than synthetic coverage. IME remains a PRD non-goal |
| `tests/win/os_win_test.c:722` | Symlink case can report unavailable | Privilege-dependent filesystem coverage must be reported explicitly |
| `tests/win/file_win_test.c:249` | EFS may use deterministic attribute injection | Does not always establish native EFS behaviour |
| `tests/font_test.c:983` | Windows replaces short-read rollback fixture with another loader failure | Production short-read rollback remains untested |
| Allocation guards | Sanitizer builds print guard skips | Release guard execution is required; platform-only guards do not cover editor typing |
| `mk/win.mk:228` | Summary still says file/editor integration is unavailable | Some wording is stale: file tests now exist. Read selected cases, not the success summary |

Windows ASan/UBSan and the ASan negative control exist (`mk/win.mk:9`, `mk/win.mk:213`). LeakSanitizer is unavailable on this toolchain, as recorded at `docs/toolchain.md:54`; Windows lifetime checks need explicit handle/resource oracles.

The current D3D11 native suite cannot be treated as reliable hidden-desktop coverage: it requires successful `Present`, exact display completion and timing capability (`tests/win/d3d11_test.c:151`, `:182`, `:201`). The port document records occluded presentation on the permitted isolated desktops (`docs/design/windows-port.md:24`).

**2. What would hurt him first**

Ranked by consequence and the evidence available, not by review severity labels:

1. **Unsaved work that fails to return after a crash or interrupted save.** Windows journal-process tests are substantial, but the actual editor’s session discovery, retained originals, Save As/checkpoint interleavings, closed modified tabs and over-capacity restore are untested. The creator-lease rejection is a concrete .9 prerequisite; fixing that rejection alone does not prove recovery. Evidence: `src/editor/recovery.c:1010`, `mk/win.mk:228`, `src/editor/recovery.c:779`.

2. **The window stops progressing after occlusion, lock/resume or presentation failure.** D3D11 explicitly requests reconstruction; the present tree feeds completion errors into the editor’s terminal error path. This is already .9’s responsibility and must survive beyond its first typed frame. Evidence: `src/d3d11/d3d11.h:37`, `src/d3d11/d3d11_win.c:658`, `src/editor/editor.c:376`, `src/editor/editor.c:122`.

3. **External changes or backing loss while editing Windows/WSL files.** Clean reload versus dirty-buffer preservation is untested through the editor, WSL guarantees remain unknown, and short-name writes can leave visible text stale until focus/save checking. Evidence: `src/editor/editor.c:707`, `src/file/file_win.c:189`, `docs/design/windows-port.md:11`. This is an unclosed safety risk, not evidence of observed data loss.

4. **Large-file navigation waits behind bulk work.** Windows always puts resident viewport acquisition on the bulk lane; the real-client foreground and editor-jump tests skip. That is a concrete scheduling disadvantage with unmeasured user impact. Establish a repro before implementing the residency API. Evidence: `src/scroll/resident.c:122`, `tests/work_foreground_test.c:310`.

A separate immediate usability blocker is **save refusal on files carrying named streams**, including `Zone.Identifier`. The refusal protects existing data, so it does not belong above as a proven data-loss defect (`tests/win/file_win_test.c:266`).

**3. Job list**

Each entry is a separate worker scope. All acceptance runs must use `tools/winrun.sh`. Use a private desktop for layout/window tests and a private window station for clipboard tests; the desktop isolation mode shares Tobias’s clipboard. Pixel/content assertions must not require a real present. Simulated presentation completion must be labelled simulated and must not manufacture real display timing.

The coordinator should own changes to `mk/win.mk` after .9, integrating each worker’s requested test registration. This avoids turning the build file into a shared editing hotspot.

| Job; worker | Exact scope | Work and acceptance | Dependencies |
|---|---|---|---|
| **Finish .9’s complete acceptance — Codex xhigh** | Existing .9 scope: `src/main.c`, `src/editor/{open,backend,editor,refresh,recovery}.c`, `editor.h`, `private.h`, `src/win32`, `src/os/os_win.c`, `mk/win.mk` | Resolve typed lease ownership/cleanup, missing symbols including dark hint and fd-limit entry, D3D refresh/latency/suspend/recovery, DPI rescaling and application link. Acceptance: posted input edits and saves byte-exact; close/restart retains unsaved text; injected recovery request rebuilds and accepts another edit; synthetic DPI change rebuilds font/hit geometry; release editor allocation guard runs. Hidden tests must progress without authenticated display completion. | Existing worker; do not duplicate |
| **Make renderer tests independent of visibility — Codex xhigh** | `src/d3d11/d3d11_win.c`, `d3d11.h`, `tests/win/d3d11{,_win}_test.c` | Separate draw/readback verification from real presentation acknowledgement. Retain raster-oracle comparisons for colours, margins, wide/combining images, underlines, scrolling, atlas lifetime and region damage. Test occlusion, missing statistics, acquisition failure and teardown with native shims. Acceptance: pixel suite passes on hidden WARP window; occlusion produces bounded protocol outcomes without fake T6; real timing is explicitly unavailable when unavailable. | Coordinate any D3D edits with .9; otherwise independent of editor jobs |
| **Create reusable Windows editor fixture — Sonnet** | New `tests/win/editor_fixture.h`; new fixture test only | Provide isolated profile/scratch directories, deterministic backend/completion control, input injection, buffer/grid inspection and child restart helpers using production editor APIs. Acceptance: an edit, delayed completion, close and reopen work without foreground focus or display statistics; helpers clean only their owned artifacts. | After .9; freeze fixture before consumers |
| **Prove hot exit and session recovery — Codex xhigh** | `src/editor/{recovery,restore,retain,checkpoint}.c`; Windows adaptations/new counterparts of editor hot-exit/recovery/checkpoint/session tests | Exercise actual editor process death, unsaved/untitled and closed modified tabs, retained BASEs, window/view state, partial recovery and exhausted tab capacity. Acceptance: independent byte/view oracle after restart; source journal survives failed/deferred recovery; interrupted checkpoints select a complete recoverable generation. Reuse Linux cases rather than inventing weaker Windows smoke cases. | .9 and fixture |
| **Prove save/reload and resource-pressure behaviour — Codex xhigh** | `src/editor/{buffers,commands,session}.c`, `src/savectl/savectl.c`, `src/os/{os.h,os_win.c}`; new Windows editor-file tests | Cover Save As, dirty close, external replace/delete/recreate, keep/reload, sharing failures and open bursts. Use the shared identity converter through reload/adoption. Map native resource exhaustion into a recoverable refusal and verify handle cleanup. Acceptance: failures preserve dirty bytes and original targets; clean reload preserves/clamps view; injected high-half identity survives adoption; exhausted opens refuse cleanly and later opens work after resources are released. | .9 and fixture; serialize with WSL OS work |
| **Exercise daily commands and modal ownership — Codex xhigh** | `src/editor/{input,findpanel,palettepanel,menubar,fif}.c`; Windows command/panel tests | Port Linux command scripts and modal matrix for implemented actions: edits, selection/history, tab cycling, prompts, palette/menu, find/replace, results navigation and clipboard commands. Acceptance: compare buffer bytes, selection, undo state, active tab and command outcome; fields never mutate the document behind them. Run clipboard portions on a private station. Keep Linux-unimplemented commands explicitly unavailable. | Fixture; integrate after save/reload work because dispatch and prompts meet |
| **Verify large-file interaction under load — Codex xhigh** | `src/editor/{large,scrollctl,minimap}.c`, `src/scroll/resident.c`; Windows large/scroll/display tests; Windows cases in `work_foreground_test.c` | Port first-viewport, far seek/find, wrap, estimated-index progress and minimap oracles. Run while real index/find/save/font jobs occupy bulk lanes. Acceptance: deterministic progress and correct final viewport/text, cancellation and continued input; report traces for observed waits. Implement `edit-uunn` only if these tests or use establish the need. | Fixture; independent of recovery and command files |
| **Verify fonts, settings and current design together — Codex xhigh** | `src/font/{selection,async,unicode,discovery_win,dwrite_win}.c`, `src/editor/{font,font_size,fonts,preferences,paint}.c`; Windows font/design/config tests | Port actual editor Unicode, custom-face, collection-face, fallback-chain and zoom tests; include true-em combining placement and production-loader rollback. Add dark/light token, spacer, panel, placeholder and hover grid checks. Acceptance: selected face has a discriminating oracle; expected glyph placement/grid damage survives settings/zoom changes; D3D readback matches composed cells without requiring present. | Fixture and renderer test support; serialize with command work if fixes cross into panel painting |
| **Finish native pointer/input details — Codex xhigh** | `src/win32/{win32_plat.c,private.h,keysyms.h}`, `tools/win32_keysyms.py`, `src/os/utf8.manifest`, `tests/win/win32_plat_test.c`; new drop tests | Implement leave tracking, drop delivery and dark title hint; verify cursor transitions. Reconcile DPI manifest/creation/query behaviour and keypad repeat identity using the existing backlog evidence. Acceptance: posted wide-path drop with non-BMP/long names reaches the editor unchanged; leave clears hover; cursor switches correctly; DPI/layout/keypad tests run privately. Use a Windows path payload contract that can represent all accepted names; do not force arbitrary Windows filenames through an unsafe delimiter format. | .9; owns `win32_plat.c` exclusively until finished |
| **Establish WSL-share safety/degraded mode — Codex xhigh** | `src/os/os_win.c`, `src/file/{file.c,file_win.c}`; native share fixture/test | Test the actual supported WSL route: identity stability, mapped reads/backing loss, notification/reconnect and atomic replacement. Add per-capability copy/poll/degraded behaviour where guarantees fail. Acceptance: operation either meets its stated guarantee or explicitly selects safe fallback/refusal; stale reads never become accepted edits/saves. Publish measured capability results, not a generic “UNC supported” claim. | Serialize after save/resource OS edits and before named-stream transaction |
| **Preserve named streams during save — Codex xhigh** | `src/file/file_win.c`, `tests/win/file_win_test.c`, `tests/file_win_cases.inc` | Add identity-checked named-stream preservation to the save transaction, including `Zone.Identifier`, cancellation and failure cleanup. Acceptance: edited default stream and original named streams survive success; failure preserves old target and dirty edits; crash-boundary tests retain the transaction’s promised generation. Keep EFS refusal explicit unless a separate EFS transaction is implemented. | After WSL file-layer work; disjoint from editor recovery/commands |
| **Windows registration, associations and icon — Sonnet** | New Windows registration/unregistration script and resource files; `src/os/utf8.rc`; icon loading in `src/win32/win32_plat.c`; packaging documentation | Provide per-user installer-free registration, quoted Open With/file-association command, discoverable launcher/shortcut and embedded application icon. Acceptance: disposable HKCU registration round trip preserves unrelated registrations; command quoting reaches the parser correctly; resource/window icon is inspectable in a hidden-window test. Default-app choice remains a user action where Windows requires it. | .9 and native-platform job; coordinator integrates resources/build rules |
| **WSL product launcher — Sonnet** | New `tools/sublimite-win` launcher and shell contract test | Translate each file path while preserving line/column suffixes, options, stdin, `--wait` exit status, spaces and Unicode. Acceptance: shell tests capture exact Windows argv/stdin; private application test verifies routing to the existing instance. | Command name decision; .9 for application acceptance |
| **Make check describe actual coverage — Haiku** | `mk/win.mk` summary text, `tools/winrun.sh`, corpus/interpreter configuration and affected skip messages | Remove stale “module not ported” explanations, distinguish unavailable native capability from successful verification, and make required fixtures explicit. Acceptance: a captured check transcript identifies every omitted/unavailable case and contains no success claim for an unexecuted editor case. | After suite registrations stabilize; coordinator owns build-file merge |

Parallel work is possible after the fixture freezes:

- Recovery, daily commands, large-file interaction and font/design work have disjoint primary source files.
- Renderer work can proceed alongside editor work, provided .9 hands over exclusive ownership of its D3D files.
- Native platform work is separate from those editor modules.
- Registration/icon work waits for native platform work because both touch `win32_plat.c`.
- Save/resource work, WSL safety and stream preservation form a serial chain because their OS/file ownership overlaps.
- The WSL launcher and coverage-message cleanup can proceed independently of C implementation files.

No blanket CPU-raster port is justified by code symmetry. Hardware→WARP already exists. Acceptance should establish responsive, correct rendering and recovery with the available Windows route.

The open questions in `docs/design/windows-port.md` §7:

| Question | Does Tobias need to decide? | Recommendation |
|---|---|---|
| WSL launcher command name | **Yes:** this affects his shell muscle memory | Use `sublimite-win`; preserve `sublimite` for the Linux build |
| Deny write sharing on mapped originals | **Yes, if changing the current product policy** | Keep read/write/delete sharing and validate changes/faults. Explicit denial would obstruct other tools; settle share deficiencies through the measured degraded mode |
| DirectWrite for ASCII as well as other glyphs | **Only for a requested visual change** | Keep current baked/shared ASCII rasterization and DirectWrite non-ASCII/fallback route; it is already explicit in `src/font/font.c:849` |
| Build-time versus initialization-time shader compilation | **No product decision required** | Prefer committed/precompiled bytecode generated by a reproducible tool step. Current code compiles during initialization and requires `d3dcompiler_47.dll` (`src/d3d11/d3d11_win.c:229`); removing that runtime dependency is an engineering choice |

Physical mixed-DPI behaviour and real desktop presentation remain **unknown** after private-desktop tests. Controlled synthetic tests can establish state transitions; Tobias’s eventual use, or an explicitly authorized hardware session, must settle physical display behaviour.

**4. Backlog triage**

These are triage decisions about real use, not a request to turn every deferred finding into a job. Bead evidence refers to `/home/tobiasosborne/Projects/sublimite/.beads/issues.jsonl`.

- **edit-1q3a — line 392:** DPI sizing/awareness and keypad identity can affect visible size or key behaviour; cover them in native-platform work. Missing native-input coverage belongs in the test work. Timer failures, synthetic timestamps, trace markers and hostile callback-drain pressure remain backlog absent a repro.
- **edit-77o7 — line 391:** Short-name notifications can leave text stale during ordinary external-tool use; add a Windows regression if Tobias’s tools produce those names. Focus/save identity checks reduce the exposure. Audit-ACE preservation remains a specialized metadata limitation, not an immediate trial blocker.
- **edit-kv2g — line 393:** **Pull the occlusion-dependent test failure now:** it conflicts directly with required quiet testing. Retained native initialization diagnostics help real startup/recovery failures; extend acquisition/lifetime coverage within the renderer job rather than opening findings individually.
- **edit-l3qd — line 390:** Reload short-transfer/failure coverage belongs in save/reload work. Normal CLI parsing rejects root-relative input, so raw-wire root-relative paths and the UNC trailing-separator corner remain backlog. The timestamp-range corner is outside ordinary use; IPC identity/capacity/Unicode coverage should expand with actual application routing.
- **edit-o1l0 — line 395:** Reparse/provider changes, transient identity fallback and network/backing-loss behaviour matter to Windows/WSL use and belong in the WSL capability job; minimum-version declaration belongs in delivery. FAT/multi-client lock scope needs explicit unsupported behaviour when encountered. Long spawn arguments matter to bulk launches; remaining environment, sleep, process-accounting and exceptional-cleanup corners stay backlog. The concurrent fixture-directory finding is already recorded fixed.
- **edit-y63l — line 394:** Face-selection, combining placement, Unicode font names and production-loader rollback are relevant to prose/custom-font use; strengthen their oracles in the font job. A ligature-only case is coverage work, not evidence that current text is wrong.
- **edit-uunn — line 396:** A plausible responsiveness gap, not a measured regression. Add Windows large-file/load coverage first; pull the residency contract only when use or traces show the bulk-lane choice hurts.
- **edit-osn3 — line 282:** Windows hover visibly sticks after pointer exit; pull its leave-event portion into native-platform work. Wayland cursor protocol work does not advance Windows parity.
- **edit-cxho.20 — line 290:** Explorer file drop is a real missing opening route; implement it with Unicode/long-path coverage in native-platform work. OLE virtual-file/drag-feedback expansion can remain deferred.