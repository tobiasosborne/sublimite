Implemented edit-dr9. `make -j3 all` and non-display sanitizer checks pass; the known data-loss race passed on its single rerun.

Final display and malloc-guard checks remain incomplete because `:99`’s lock timed out.

[Worker report](docs/worker-reports/edit-dr9-s8.md) includes red/green evidence and coordinator rerun commands.