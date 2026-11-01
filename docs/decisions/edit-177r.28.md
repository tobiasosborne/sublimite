# edit-177r.28 — Windows editor WSL degraded mode

2026-11-01, round 2. Implements the settled product decision in
`docs/design/windows-port.md` §2 decision 1: Windows reads WSL files, refuses
saving there, and offers Save As to a Windows destination. No write helper.

- `FILE_WATCH_UNSUPPORTED` is a capability result. Source/reload setup and
  Save As target-watch setup keep the watch invalid and omit native wait-set
  registration; ordinary watch failures remain errors.
- Windows focus/activation and explicit save/find/warm commands compare source
  identity independently of notifications. Source maintenance owns clean reload
  and edited-buffer conflict choices. A Save As COPY checks its new target,
  since it no longer depends on the original share pathname.
- Ctrl+S checks identity first, then refuses WSL before journal preparation.
  Preparing a save that is guaranteed to be unsupported would retain recovery
  ownership and prevent immediate Save As. The file layer's independent refusal
  remains the final boundary for other save callers and WSL Save As targets.
- Identity I/O stays at explicit command/setup/focus boundaries. Document and
  query typing use resident state; no timer or recurring share scan is added.
- Tests extend `tests/win/editor_win_test.c`, using the existing classifier and
  native-provider seams on private NTFS data. A case-sensitive provider alias
  models distinct `a` and `A` files without depending on NTFS privileges. An
  optional real-provider selector reads only this worktree's own source and
  skips when the WSL provider is absent. All journals and Windows scratch use
  the fixture's private TEMP profile; no Windows write targets a WSL share.
