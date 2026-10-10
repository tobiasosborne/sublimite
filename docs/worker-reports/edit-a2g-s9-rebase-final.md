Rebase completed as `de15011`, preserving both intents. Keep now restarts background work after durable reconciliation; eviction also waits for the bead’s retiring find lease before destruction.

- GCC 13 `make all`: exit 0.
- Clang 18 ASan/UBSan `make check`: exit 0; 63 binaries and shell checks passed. Leaks were off; display `:99` only.

[Report written](/home/tobias/Projects/sublimite/.wt/edit-a2g/docs/worker-reports/edit-a2g-s9-rebase.md), including log tails. No intent remained unreconciled.