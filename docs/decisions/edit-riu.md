# edit-riu: keep live keymap fixtures on their own display

External X clients in the order test use the `DISPLAY` selected by
`xvfb_start()`, captured before the live fixture opens its platform connection.
This matches `plat_init()` and its keyboard worker, which connect through the
environment. The display lock exports its chosen display before executing the
test; the Xvfb helper can subsequently replace it with a private server.

Save the original map with `xkbcomp` as a compiled keymap before changing it.
Replaying only `setxkbmap -query` rules would lose custom mappings. Also save
and restore the root `_XKB_RULES_NAMES` property, including its absence, because
loading the compiled map does not restore that metadata.

Each live-map fixture restores after platform shutdown on success and failure.
An exit hook provides idempotent fallback cleanup before private Xvfb shutdown.
Mark the snapshot dirty before attempting a layout command so partial failures
also restore. Restore failures are test failures. If `setxkbmap` cannot query
or initialize the fixture, print `SKIP`; if the original map cannot be saved,
print `SKIP` before changing it. Synthetic map-dispatch checks still run.

The fixture clears inherited variants and options while selecting US/German;
the snapshot restores the caller's map and metadata afterwards. The existing
worker allocation exemption and display-bench locking remain as supplied.
