# v2-he7.24 — Vendored tree-sitter is its own pseudo-module, `vendor_ts`

Closes the gap reported in `docs/decisions/v2-he7.22.md` ("When
`src/hl/*.c` lands, it inherits the vendor's heap, stdio and `dup`
grants"). Supersedes the he7.18 sentences that attribute the vendored
objects and the `ts_*` / `tree_sitter_*` names to `hl`. The rest of
he7.18 and he7.22 stands.

## Invariant

Every release archive member is attributed to exactly one module, and the
attribution follows the object's path, never its archive:

- `src/<m>/*.o` belongs to `m`, and `m` is never `vendor_ts`.
- `vendor/tree-sitter/lib/src/lib.o` and `vendor/tree-sitter-*/**.o`
  belong to `vendor_ts`, and to nothing else.

`vendor_ts` owns the names `ts_*`, `tree_sitter_*`, `_ts_dup`,
`range_intersects` and `range_within`. It sits in hl's layer and is
ordered directly below hl (`vendor_ts < hl`). A grant to `vendor_ts`
therefore covers the upstream objects only. `src/hl` objects are checked
against hl's own grants, which are empty. hl may call the upstream API.
The upstream code may not call into hl or into any other L4 module.

## Design

`vendor_ts` is implicit in `tools/check_symbols.py`, like `main`. It is
not listed in `mk/layers.mk`.

- **Layer.** `read_module_map` sets `layers['vendor_ts'] = layers['hl']`,
  as it sets `main` from `app`. The layer is not restated anywhere. The
  existing membership check (`module is not an archive member of L<n>`)
  ties it to the archive. If hl moves layer while `L4_SRCS += $(TS_SRCS)`
  stays, the check fails on every vendored object. A module map without
  `hl`, or one that lists `main` or `vendor_ts` explicitly, fails as
  `[input]`.
- **Order.** `intra_order` adds `(vendor_ts, hl)` before the transitive
  closure. `INTRA_RULES` in `check_dag.py` stays unchanged, because
  check_dag sees includes under `src/` only and has no `vendor_ts`.
- **Attribution.** `object_module` maps the vendor paths to `vendor_ts`
  and refuses `src/vendor_ts/`, which would otherwise read as a module
  directory. `symbol_module` maps the vendor names to `vendor_ts`.
  `owns_symbol`'s generic branch drops its `not vendor_symbol` clause. A
  `src/` object defining `ts_x` now fails as `owner != module`, so the
  clause added nothing.
- **Grants.** The 15 `hl` lines in `tools/system-symbols.txt` become
  `vendor_ts` lines with unchanged reasons. `hl` holds none.

## Simplest alternative and its counter-example

The simplest alternative is to list `vendor_ts` in `L4_MODS` and add
`vendor_ts < hl` to `INTRA_RULES`. Both checkers would then share one map.
The counter-example: `L4_MODS` is also the source map. `mk/layers.mk`
compiles `$(wildcard src/<m>/*.c)` for every listed module, and check_dag
accepts every listed module as a `src/` directory. Then
`src/vendor_ts/shim.c` compiles into `libui.a`. Both lints accept it,
and it inherits the heap, stdio, `abort` and `dup` grants. That is the
leak this bead closes, moved one directory over. With `vendor_ts`
implicit, both checkers reject it. check_dag reports `src/vendor_ts/` as
an unassigned directory. check_symbols reports
`unrecognized object path src/vendor_ts/lib.o`, the probe below.

Other rejected alternatives:

- **Path-scoped grants (`malloc vendor/tree-sitter* # ...`), with no new
  module.** Grants would then attach to paths and edges to modules. The
  upstream names still need an owner for edge checks, so the objects
  would carry two attributions, `hl` for edges and a path for grants. With
  two attributions, a grammar calling `hl_*` stays legal. One module gives
  one attribution, and the `[intra-layer]` rejection below is free.
- **Defer until `src/hl` has code (it holds only `.gitkeep` today).** The
  `hl_heap` red run below is the counter-example. The first `src/hl` file
  that imports `malloc` passes with `check_symbols: ok`.

## Scope limits

- The order is transitive, like every module edge. If a later rule adds
  `hl < X`, X may call `ts_*` directly. A ban that keeps tree-sitter
  hl-private would need a non-transitive edge kind, and nothing asks for
  one.
- The check sees link-level imports only. Includes of
  `vendor/tree-sitter/lib/include` from `src/` are not under `src/`, so
  check_dag does not see them.
- Grants are still not checked for use (he7.22 "Scope limits"). The 15
  `vendor_ts` lines were exact on landing, because the full-tree
  `make check-symbols` passes with them.

## Wave 3 hl bead (advisor)

The Wave 3 hl bead carries this line:

> tree-sitter runs on an allocator supplied from base (`ts_set_allocator`)
> with the debug dot-graph code compiled out; the `vendor_ts` heap
> (`malloc`, `calloc`, `realloc`, `free`), stdio (`__fprintf_chk`,
> `fclose`, `fdopen`, `fputc`, `fwrite`, `stderr`), `abort` and `dup`
> grants are deleted when that lands.

That leaves `snprintf`, `__snprintf_chk` and `__vsnprintf_chk`, from node
names and the lexer log. The same bead either keeps them with their
reasons or removes their callers.

## Counts (M)

Measured with `grep -v '^#' tools/system-symbols.txt | awk 'NF{print $2}' | sort | uniq -c`.

| Scope | Lines |
|---|---|
| `*` | 21 |
| `os` | 91 |
| `vendor_ts` | 15 |
| `base` | 5 |
| `hl` | **0** |
| **Total** | 132 lines, 131 symbols. `fdopen` has both an `os` and a `vendor_ts` line. |

## Tests

The fixtures in `tests/symbols/` are driven by `run.py`. A layer tuple
entry that contains `/` is a directory of vendored sources relative to
the case, so a fixture object has its release path, for example
`vendor/tree-sitter/lib/src/lib.o`. `fixtures.mk` compiles those paths
too.

| Case | Content | Verdict |
|---|---|---|
| `hl_heap` | `src/hl` object imports `malloc` | `[restricted] hl imports malloc, granted only to vendor_ts`, rejected |
| `vendor` | runtime `lib.o` imports `malloc`/`free`, grammar scanner imports `calloc`, hl calls `ts_probe_parser_new` and `tree_sitter_probe` | accepted, imports asserted |
| `vendor_owner` | runtime `lib.o` imports `read`, grammar scanner calls `hl_probe_callback` | `[restricted] vendor_ts imports read` and `[intra-layer] vendor_ts (L4) references ... hl`, rejected |
| `granted` (changed) | the second `fdopen` grant is now exercised by a vendored `lib.o`, not by `src/hl` | accepted |
| `restricted` (changed) | `app → malloc` now names `vendor_ts` as the holder | rejected |

Red, measured with the new fixtures and runner, and with the old checker
and grant list (`make -s -j8 check-symbols`, exit 2):

```text
restricted: expected exit 1 and (..., '[restricted] app imports malloc, granted only to vendor_ts'), got 1; fixture imports missing: []
build/rel/tests/symbols/restricted/layer5.a(src/app/app.o): error: [restricted] app imports malloc, granted only to hl
hl_heap: expected exit 1 and ('[restricted] hl imports malloc, granted only to vendor_ts',), got 0; fixture imports missing: []
check_symbols: ok (1 archives plus LDLIBS)
vendor: accepted as expected
vendor_owner: expected exit 1 and ('lib/src/lib.o): error: [restricted] vendor_ts imports read, granted only to os', 'scanner.o): error: [intra-layer] vendor_ts (L4) references hl_probe_callback from hl (L4)'), got 1; fixture imports missing: []
build/rel/tests/symbols/vendor_owner/layer4.a(vendor/tree-sitter/lib/src/lib.o): error: [restricted] hl imports read, granted only to os
```

In the red run, the old check accepts `hl → malloc` and attributes the
vendored `lib.o` to hl. It also accepts a grammar calling into hl.

Green, with the same command (exit 0, on AC):

```text
check_symbols: ok (6 archives plus LDLIBS)
restricted: rejected as expected
granted: accepted as expected
hl_heap: rejected as expected
vendor: accepted as expected
vendor_owner: rejected as expected
```

The diagnostics, measured by running the checker directly on the fixture
archives:

```text
build/rel/tests/symbols/hl_heap/layer4.a(src/hl/hl.o): error: [restricted] hl imports malloc, granted only to vendor_ts
build/rel/tests/symbols/vendor_owner/layer4.a(vendor/tree-sitter/lib/src/lib.o): error: [restricted] vendor_ts imports read, granted only to os
build/rel/tests/symbols/vendor_owner/layer4.a(vendor/tree-sitter-probe/src/scanner.o): error: [intra-layer] vendor_ts (L4) references hl_probe_callback from hl (L4): dependency is outside INTRA_RULES
```

Counter-example probes, run once by hand on the same archives (not
fixtures):

```text
--module-map <make map> L4:vendor_ts          -> error: [input] module map must include app and hl and leave main and vendor_ts implicit
--module-map L0:os L5:app                     -> error: [input] module map must include app and hl and leave main and vendor_ts implicit
member src/vendor_ts/lib.o                    -> error: [input] unrecognized object path src/vendor_ts/lib.o
```
