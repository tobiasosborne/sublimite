# v2-he7.22 — Per-module system imports: only os talks to the kernel

Supersedes the global allowlist paragraph of `docs/decisions/v2-he7.18.md`
("Each system symbol entry is an exact strong import..."). The rest of
that design stands.

## Invariant

For every release object of module M (the layer archive members and the
loose `src/main.o`), each strong undefined symbol S satisfies exactly one
of these conditions:

1. S belongs to a project module on a permitted DAG edge (unchanged).
2. `tools/system-symbols.txt` lists `S *`. S then neither enters the
   kernel nor touches process-global state.
3. `tools/system-symbols.txt` lists `S M`, a grant to M itself.

Anything else fails. A system symbol granted only to other modules fails
as `[restricted]`. A symbol with no entry fails as `[unknown]`. Kernel
entry and process-global state are reachable only by direct import from
modules with grants. In the current tree those modules are `os`, `base`
(the allocation interposer and CPU detection, v2-0fv.3) and the reported `hl`
exception below.

## Format

```
SYMBOL SCOPE # reason
```

SCOPE is `*` (any module) or one module from the layer map. Each grant
takes one line. A symbol that two modules need gets two lines, each with
its own reason, because the reasons differ. For example, `fdopen` serves
os stdio transfer and tree-sitter dot graphs. A module's grants form one
sorted block. A worker landing a new module appends its own block, so
parallel landings do not edit the same lines. The file is read as a
configuration and fails as `[input]` in these cases:

- An entry has a missing field or reason.
- SCOPE names a module that is not in the layer map.
- A `*` entry shares its symbol with any other entry.
- A (symbol, module) pair is duplicated.
- The symbol is a project symbol. This rule was already there.

Lines in the old global format (`name # reason`) fail closed with
`expected SYMBOL SCOPE # reason`. To merge the base and trace beads,
rewrite their additions as `name base # reason` and `name trace # reason`.

## Classification criterion

Use `*` only when an import is a function of its arguments and the
caller's memory, with no kernel entry and no read or write of mutable
process-global state. Everything else is restricted. The default is
restricted. A new import is `[unknown]` until someone writes a scoped line
for it.

Borderline decisions:

| Symbol(s) | Scope | Why |
|---|---|---|
| `malloc` `calloc` `realloc` `free` | restricted (hl) | The heap is process-global and grows through `brk` and `mmap`. Law 2 says pools and arenas come from `src/base`, and this rule enforces that at link level. `base` gets its grant when it lands, since a grant before use would be speculative. |
| `abort` | restricted (hl) | CLAUDE.md: "no `abort()` outside `ASSERT`". The link check now enforces it. If base's `ASSERT` expands to `abort`, base gets a grant. |
| `__assert_fail` | `*` | This is the `<assert.h>` failure path, a C11 facility used at the call site. It is terminal. The process dies, and no module can depend on a returned kernel result. |
| `__stack_chk_fail` | `*` | The compiler inserts it in any function with a stack array under the toolchain's stack protector, so source cannot choose it. It is terminal. |
| `_GLOBAL_OFFSET_TABLE_` | `*` | This is a linker-generated symbol with no runtime code. |
| `__ctype_b_loc`, `isw*`, `towlower` | `*` | They read the current locale, which is process-global. The writers `setlocale`, `uselocale` and `newlocale` are granted to no module, so the locale stays at C and the result depends on the argument only. A grant of a locale writer to any module must reclassify these as restricted. |
| `snprintf` `__snprintf_chk` `__vsnprintf_chk` | restricted (hl) | glibc's printf core can use the heap for large widths and wide conversions. It also reads `LC_NUMERIC`. A future status-line or trace formatter needs its own grant. |
| `__errno_location` | restricted (os) | errno is per thread, but its only meaning is the result of a kernel call. os maps it to `os_err` (`docs/decisions/v2-0fv.1.md`, native error). |
| `environ` `getenv` `setenv` `unsetenv` | restricted (os) | This is process-global state. os serializes readers against mutation. |
| `stderr` and stdio | restricted | These calls run kernel I/O through process-global streams. CLAUDE.md also forbids `printf` on the UI thread. |
| `pthread_*`, clocks, `sched_yield`, `nanosleep` | restricted (os) | This is kernel or process-global scheduling. Other modules use `os_thread`, `os_mutex` and the os clocks. |

The same criterion applies to system libraries. A future `xcb_*` import
is a grant to `x11`, the module whose `LDLIBS` links the library. That
library's own kernel entry runs on x11's behalf.

## Counts (M)

Measured with `grep -v '^#' tools/system-symbols.txt | awk 'NF{print $2}' | sort | uniq -c`.
When this bead landed, the symbol set matched the previous global list (107 symbols,
an empty `diff` of the sorted names). Later beads changed the lines as shown below.
Current counts are re-measured at the v2-0fv.13 send-back.

| Scope | At he7.22 | Now | Changed by |
|---|---|---|---|
| `*` | 18 | 21 | v2-0fv.6 added `__memcpy_chk`, `strnlen` and `strrchr` |
| `os` | 75 | 93 | v2-0fv.13 added 16 stream and wait-set imports; v2-0fv.2 added 10 fault-service imports (`abort gettid raise sem_destroy sem_init sem_post sem_wait sigaction sigemptyset syscall`); the v2-0fv.13 send-back removed 8 process and poll imports (`environ kill poll posix_spawn posix_spawn_file_actions_{init,destroy,addclosefrom_np} waitpid`) |
| `hl` | 15 | 15 | |
| `base` | — | 5 | v2-0fv.3 (allocator interposer, CPU detection) |
| `ipc` | — | 0 | v2-0fv.6 added 29 grants; v2-0fv.13 deleted them, so ipc makes no kernel call |
| **Total** | 108 lines, 107 symbols | 134 lines, 132 symbols | `fdopen` (os, hl) and `abort` (os, hl) each have two lines |

## Current violation (reported, granted as found)

`hl`, the vendored tree-sitter runtime `vendor/tree-sitter/lib/src/lib.o`
and grammar scanners, imports 15 restricted symbols:

- `dup`, `fdopen`, `fclose`, `fputc`, `fwrite`, `__fprintf_chk` and
  `stderr`. These come from the debug entry points
  `ts_tree_print_dot_graph(tree, fd)`, `ts_parser_print_dot_graphs` and
  `ts_stack_print_dot_graph`, plus the allocation-failure message in
  `alloc.c`.
- `malloc`, `calloc`, `realloc` and `free`, the default
  `ts_current_*` allocator.
- `abort`, from `alloc.c` and the tree-sitter-yaml scanner.
- `snprintf`, `__snprintf_chk` and `__vsnprintf_chk`, from node-name
  strings and the lexer debug log.

The vendored sources cannot be edited here, so these imports are granted
to `hl` with reasons. Two consequences need a decision:

- `dup` is a direct syscall wrapper outside os. The editor never calls
  the dot-graph APIs, but the archive member still references them.
- The grant scope is the module. When `src/hl/*.c` lands, it inherits the
  vendor's heap, stdio and `dup` grants. A separate pseudo-module for the
  vendored objects in the he7.18 module map would close that gap.

Apart from `base` (above), no other non-os module imports a restricted symbol. `piece` imports
`strlen`, which is `*`. `edit`, `render`, `viewport`, `app` and `main`
import only project symbols.

## Simplest alternative and its counter-example

The simplest alternative is two lists, `pure` (any module) and `kernel`
(os only), with no per-module scopes. The current tree defeats it. `hl`
needs `malloc`, `dup` and stdio, while `os` needs `fdopen` too. One
option is to put `malloc` in `pure`, but then every module may use the
heap and Law 2's "pools from base only" is lost. The other option is to
make it os-only, but then the vendored runtime cannot link. A binary
split forces one of those. Per-module scopes state the true relation: an
import can belong to a set of modules that is neither "all" nor "os".

Other rejected alternatives:

- **A denylist of syscalls for non-os modules on top of the global
  allowlist.** This fails open. Adding `epoll_wait` for os makes it
  available to every module unless someone also extends the denylist.
  The old check accepted `piece → read` this way. That is the red run
  below.
- **Header policing (only os includes `<unistd.h>`).**
  `<stdlib.h>` declares both `qsort` and `getenv`, `malloc` and `system`,
  so headers cannot separate pure from impure. A bare `extern ssize_t
  read(int, void *, size_t);` bypasses any header rule. The link check
  sees both cases.

## Scope limits

The check does not see these paths:

- A `syscall` instruction in inline assembly. The libc `syscall()`
  function itself would need a grant.
- Calls resolved at run time through `dlsym`. gl loads GL this way.
- A system library entering the kernel inside a granted call.
- Grants that no module uses any more. Fixture runs audit partial trees,
  so an unused-grant check would need a separate full-tree mode. It was
  left out. The file was exact on landing: every line is an import of its
  module in the release build.

## Tests

The new fixtures in `tests/symbols/` are driven by `run.py`:

| Case | Content | Verdict |
|---|---|---|
| `restricted` | `piece → read` and `app → malloc` | `[restricted]`, rejected |
| `granted` | `os → read` and `fdopen`, `hl → fdopen` (two grants for one symbol) | accepted |
| `pure` | `piece → memcpy` and `app → strcmp` | accepted |
| `config_unknown_module`, `config_any_conflict`, `config_duplicate`, `config_no_reason` | one allowlist each in `tests/symbols/config/`, through `--system-symbols` | `[input]`, rejected |

Accepted cases also name the imports their objects must still make. The
runner checks them with `nm`. A compiler that folds the call away
therefore fails the case instead of passing it vacuously. `clean` now
asserts its `strlen`.

Red, measured with the fixtures and runner in place and the old checker
and global list:

```sh
make -s check-symbols
```

```text
restricted: expected exit 1 and ('[restricted] piece imports read, granted only to os', '[restricted] app imports malloc, granted only to hl'), got 0; fixture imports missing: []
check_symbols: ok (2 archives plus LDLIBS)
granted: accepted as expected
pure: accepted as expected
config_unknown_module: expected exit 1 and ('[input]', ":2: read is granted to unknown module 'nosuchmod'"), got 2; fixture imports missing: []
check_symbols.py: error: unrecognized arguments: --system-symbols tests/symbols/config/unknown_module.txt
...
make: *** [Makefile:226: check-symbols] Error 2
```

The old checker accepted the bypass with `check_symbols: ok`.

Green, with the same command plus `make all` and `make check` (exit 0, on AC):

```text
check_symbols: ok (6 archives plus LDLIBS)
restricted: rejected as expected
granted: accepted as expected
pure: accepted as expected
config_unknown_module: rejected as expected
config_any_conflict: rejected as expected
config_duplicate: rejected as expected
config_no_reason: rejected as expected
```

The diagnostics, measured by running the checker directly on the fixture
archives:

```text
build/rel/tests/symbols/restricted/layer1.a(src/piece/piece.o): error: [restricted] piece imports read, granted only to os
build/rel/tests/symbols/restricted/layer5.a(src/app/app.o): error: [restricted] app imports malloc, granted only to hl
error: [input] tests/symbols/config/unknown_module.txt:2: read is granted to unknown module 'nosuchmod'
```

## W0.7-B7 (2026-11-23): fixture verdicts are complete lines

Finding 18 of `docs/reviews/wave0-code-astra.md`. Each rejected case in
`tests/symbols/run.py` lists every error line that the checker must print,
including the object, tag and token. Both weak kinds and each LDLIBS token are
checked as separate lines. An accepted case names the imports each object must
make. The previous union over all objects accepted `calloc` as a `lib.o` import
in `vendor`, although it is made only by `scanner.o`. It also kept `granted` green
after the vendor object stopped importing `fdopen`. `tests/isolation/run.py`
binds each spelling to its line and reason and pairs it with a legal near neighbour.
The previous filename-only match passed 23/23 against a checker that rejected
every nonempty source.
