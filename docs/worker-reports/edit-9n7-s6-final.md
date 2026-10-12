Fixed edit-9n7 in the fuzz transport and verified compatibility with pending edit-cee changes.

Fuzzer: **63,387 inputs in 61 seconds, no crash (M)[AC]**, from libFuzzer output. `make all` passes. Full `make check` remains incomplete due to sandbox access restrictions and an existing journal fixture failure; display suites need the coordinator’s Xvfb rerun.

Red/green evidence and remaining findings: [worker report](/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/docs/worker-reports/edit-9n7-s6.md).