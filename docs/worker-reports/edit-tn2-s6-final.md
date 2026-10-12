Implemented edit-tn2: estimated labels retain byte anchors, and wrapped jumps use certified placement.

`make all`, focused regressions, and sanitizer/release headless editor tests pass. Full checks remain incomplete due to sandbox X/IPC restrictions and an unrelated `journal_kill_test` failure.

Red/green runs and remaining verification are in the [worker report](/home/tobias/Projects/sublimite/.wt/edit-tn2-s6/docs/worker-reports/edit-tn2-s6.md). Stopped after this bead.