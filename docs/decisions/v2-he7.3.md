# v2-he7.3 — Include checks from compiler dependencies

The include lints read the `.d` files produced by the existing `-MMD -MP`
object recipes. This uses the compiler's actual flags and header search paths
without a second preprocessing pass. `make check-lints` builds the release
objects for `src/` and `tests/` and the fuzzer objects for `fuzz/` before
running the lints. Missing dependency files and files older than their source
or any listed prerequisite fail the check.

The DAG lint checks the full closure of reached files under `src/` for each
source translation unit. The layer order and the intra-layer partial orders
are transitive. For app and the L5 entry point `src/main.c`, reaching x11,
wayland, gl, or raster through any include path is an error. Files outside
`src/`, including vendored headers, are ignored for ordering. An outside
header can still bring in a checked `src/` header indirectly. The test and
fuzz lint rejects any reached `.c` file other than the translation unit
itself. Both lints use compiler dependencies for includes. `clex.py` serves
the source-level identity string check in `check_isolation.py`.

Make expands the module-to-layer assignments in `mk/layers.mk` for the
archive source lists and the DAG lint. The standalone DAG lint obtains the
same assignments through `make print-layer-map`.

An include under an `#if` branch that is false in our single build
configuration is not seen. This is acceptable because v2 has one
configuration by rule.
