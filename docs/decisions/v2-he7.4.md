# App identity isolation (v2-he7.4)

The Makefile's `APP_ID` supplies `EDIT_APP_ID` to the compiler and names the
editor binary. Make rejects any effective value other than `sublimite-v2`
before running recipes. The compile rule supplies `EDIT_APP_ID` independently
of `WARN`. `tests/isolation_test.c` checks the size and contents of the
compiler-provided literal at compile time with GCC and Clang, and checks the
value returned by `os_app_id()` at runtime. The Wave 0 scaffold has no app
directory or socket resolver yet.

As the Wave 1 modules land, derive the XDG and HOME paths for `OS_APP_DATA`
and `OS_APP_CONFIG` from `EDIT_APP_ID`. The journal default uses the app data
directory. The config runtime's default `config.toml` uses the app config
directory. Derive both the abstract socket name and the filesystem socket
leaf from the same definition, including the private filesystem fallback.
Test the resulting defaults and fallbacks through the production resolvers
with disposable XDG and HOME roots and independent expected v2 names.

The lint checks the literal Makefile assignment and scans `.c`, `.h`, and
`.inc` files under `src/`. It removes C line splices and decodes escapes before
checking literals. It also rejects the bare v1 identifier, identity text in
flat character-literal initializers or simple formats with literal `%s`
arguments, and source directives that change `EDIT_APP_ID`. This covers the
concrete spellings from F9 and F10.

The scan has no macro expansion or data-flow analysis. It cannot resolve
names assembled through token pasting, variables, byte arithmetic, or
general formatting and string operations. It does not inspect generated or
external sources, arbitrary compiler or Make overrides, runtime path values,
or filesystem aliases. Passing the scan alone does not prove that a resolver
uses the build definition. Review each new name-forming site and test the
paths and endpoints it actually produces.
