# edit-1wc: preserve the reload-then-save outcome

The existing durability hook holds save-worker phases; the editor hooks
observe ingress, frames and IO without a seam that holds a clean source
reload until typing. Accept the valid reload-then-save outcome with exact
content checks, retaining the original competing-save variant.

The accepted outcomes are (P, derived from inserting the named contribution
at the beginning of the literal base):

- A saves `Abase`; B retains dirty `Bbase` with a save error or stale notice.
- B saves `Bbase`; A retains `Abase` with a stale notice.
- B reloads A's `Abase` while clean, types `B`, and saves exactly `BAbase`.
  Both disk and B's captured buffer must contain that exact text; B must be
  clean, without a save error or stale notice. Lengths come from `strlen` of
  the expected literals (P), so a prefix or merely finding both letters
  cannot satisfy the assertion.

An ordered variant uses the existing go/report pipes: B publishes readiness
after opening `base`, A saves, then B explicitly calls the existing
`file_check` stat probe and pumps the editor until it has reloaded `Abase`
before typing. The suite can fork after the process-wide inotify descriptor
is installed; the inherited descriptor lets the parent's polling consume
the child's notice. The ordered variant avoids that notification dependency.
This runs alongside the original race in the default
suite and alone with `--two-process-reload`, making the regression repeatable
for red and green. It uses the existing editor watchdog without adding a
duration. Session creation and creator-held leases remain as in edit-cxo.
