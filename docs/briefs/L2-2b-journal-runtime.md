SUPERSEDED (advisor, 2026-11-24): L2-2b is split into three beads that land in order, each with its own lanes and
owner read:
- docs/briefs/L2-2b1-journal-writer.md (2b-1: writer, producers, fair service, command group, sync receipt; deletes the
  dead wip);
- docs/briefs/L2-2b2-journal-recovery.md (2b-2: recovery materialisation, Q3, repair and set-aside, Measurement 2);
- docs/briefs/L2-2b3-journal-rotation.md (2b-3: rotation, exceptional recovery, the self-contained size).
None is dispatched before the N2 bead lands and the advisor signs the journal runtime section (artifact 1b).

The successor beads retain the OS interfaces filed under this brief. The writer
and its safety suite use `os_file_pwrite_once` for one write per typing record
and `os_file_pwrite` and `os_file_sync_data` for the positioned-write and sync
seams (dossier §0.3). Recovery repairs a verified torn tail using
`os_file_set_size`. The journal lives under `os_app_dir`.
