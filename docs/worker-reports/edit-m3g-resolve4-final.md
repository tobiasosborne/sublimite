Resolved the conflicts, kept `embedded.h` as the sole embedded face, and integrated DPI/zoom atlases with the asynchronous Unicode owner. No `git` or `bd` ran.

`make all` and the targeted ASan probes passed. The full editor suite still fails on native display initialization and the existing launcher/reload assertion; native font-size testing is blocked by socket restrictions.

[Resolver report](/home/tobias/Projects/sublimite/.wt/edit-m3g/docs/worker-reports/edit-m3g-resolve4.md)