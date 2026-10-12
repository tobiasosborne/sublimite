Implemented edit-bml: paste preserves the buffer’s detected LF/CRLF style.

`make all` and clipboard ASan/UBSan regressions pass. Full checks remain limited by sandbox socket restrictions and unrelated find-panel/journal failures; leak detection was disabled due to ptrace.

[Report with red/green runs and remaining checks](/home/tobias/Projects/sublimite/.wt/edit-bml-s6/docs/worker-reports/edit-bml-s6.md). Stopping after this bead.