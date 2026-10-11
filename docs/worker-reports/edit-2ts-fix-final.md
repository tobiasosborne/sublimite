Fixed a test-fixture bug in [editor_test.c](/home/tobias/Projects/sublimite/.wt/edit-2ts/tests/editor_test.c:1746): the injection expected the ordinary worker pool, so independent GPU discovery returned `RENDER_ERR_ARG`. It now injects `RENDER_ERR_INIT` consistently. Production code is unchanged.

`make all`, headless fallback/logging checks, startup probes, `--minimap-flash`, `--clipboard`, and `--ck4-save` pass. The full suite still hits existing reload/native failures; X11 `EPERM` prevents native fallback and `--require-gl` verification.

Cause, fix, and outputs appended to [the report](/home/tobias/Projects/sublimite/.wt/edit-2ts/docs/worker-reports/edit-2ts.md:114). No git or bd commands ran.