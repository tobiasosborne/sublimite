# v2-he7.18 — Link-level module dependency check

Run GNU `nm -g -P` on the release layer archives and the loose
`src/main.o` under `make check-lints`. This covers link references created
by declarations, including declarations without an include. Keep the
isolated layer links to ensure references actually resolve.

Make supplies the module-to-layer map from `L<n>_MODS` in
`mk/layers.mk`, along with each archive's ordered object paths from the
same prerequisite lists used to build it. Match the member names and
count against the manifest. The order distinguishes repeated names such
as the vendored `parser.o` and `scanner.o`. Diagnose failed reads, any
`nm` diagnostic, unexpected members and malformed symbol output.

The sole Python import from `check_dag.py` is `INTRA_RULES`. Apply its
transitive order to same-layer references between distinct modules. A
defined project global must belong to its defining module's prefix.
Strong imports may point to the same module, an allowed lower module, or
an exact entry in `tools/system-symbols.txt`. The L5 modules, `app` and
`main`, are subject to the additional ban on imports from `x11`, `wayland`,
`gl` and `raster`. Reject weak undefined functions and objects
regardless of the target module.

The loose entry-point object owns the C ABI name `main`. The vendored
tree-sitter runtime and grammar objects in `libui.a` belong to `hl` for
edge checks. The runtime owns `ts_*` and the exact upstream exports
`_ts_dup`, `range_intersects` and `range_within`. The grammar objects
own `tree_sitter_*`. Those definitions are accepted only from the
corresponding vendor paths. A reference to one of those names still
creates an edge to `hl`.

Each system symbol entry is an exact strong import with an inline reason.
The initial entries come from the release ELF objects, including the
vendored runtime and parsers. Add an entry only for a verified libc,
system library or compiler/linker ABI import and document its provider
or purpose. Reject duplicate entries and names assigned to project
modules. Weak imports and project exports cannot use the allowlist.

Each `src/<module>/LDLIBS` must be a regular file containing only
whitespace-separated `-lNAME` tokens. Names start with an ASCII letter
or digit and may also contain ASCII underscores, periods, plus signs
and hyphens. Reserve the `-l` name of every layer archive. Reject
explicit archive paths, `-l:` filenames, `-L` search paths, linker
flags and shell syntax. For example, `-lxcb -lxkbcommon` is accepted.
The symbol audit validates these files, and direct isolated links also
require validation before their recipes run.

The audit covers ELF global symbols in the release build. Indirect calls
through runtime lookups such as `dlsym` and dependencies present only in
other compiler configurations remain outside its scope. The archive
manifest relies on Make and `ar` preserving member order. The LDLIBS rule
assumes Make's compiler and system library search environment and a stable
checkout during the build.

## W0.7-B7 (2026-11-23): one module map and closure

Finding 22 of `docs/reviews/wave0-code-astra.md`. `check_symbols.py` now imports
`parse_layers` (the `L<n>:<module>` map with the implicit `main` in L5),
`build_order` (the transitive intra-layer closure, given `vendor_ts < hl` as one
extra rule) and `L5_BACKENDS` from `check_dag.py`. It no longer keeps its own
copies. Make passes `$(LAYER_MODULES)` from `mk/layers.mk` and has no second
`symbol_modules` expansion. Include evidence (`.d` files) and link evidence (`nm`)
stay separate checks. The symbol fixture `ldlibs` still requires the complete
`[inter-layer]` line for an upward call with no header.
