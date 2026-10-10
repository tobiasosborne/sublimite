Fixed cleanup and load-scaled 10–50 s window polling.  
Red reproduced the 29,484-byte XCB leak.  
All CLI regression probes passed with leaks enabled.  
`make all`: 0; `make check`: 2—unchanged `editor_close_test:42` failure.  
Report: [edit-63v-s8.md](/home/tobias/Projects/sublimite/.wt/edit-63v/docs/worker-reports/edit-63v-s8.md).