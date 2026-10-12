# edit-1um: external-source prompt placement

Draw `Source changed: R reload / K keep` in the existing bottom status row,
using the current warning foreground, chrome background and baked ASCII
atlas. The row index is the grid row count minus the unit offset for a
zero-based last row (E). Mark that row damaged, leaving the cached first
document row intact while source access is suspended. Resolving the source
already requests full layout and restores ordinary bottom-row composition.
No viewport resizing, extra row reservation or allocation is introduced.

This follows the bead's Sublime placement baseline: source-change notices
belong in status chrome or a dialog. The historical drive report
`docs/reviews/astra-vs-sublime-2.md` also records our old top prompt. The
referenced `build/design/ret-dirty-external-prompt.png` is absent in this
worktree; a fresh visual Sublime comparison needs the coordinator's display.

Precedence remains the established submit order: paint the source notice,
then apply the session save overlay. An active save status therefore takes
the bottom row while saving is active, including the required `saving`
acknowledgement frame described in `docs/decisions/edit-mdv.md`. The source
notice remains eligible on subsequent frames after that overlay retires.
This preserves the displayed saving-frame contract without changing save
submission or acknowledgement. Save failures already defer to stale-source
state; the existing startup recovery overlay order is retained. Go-to-line
already declines to overlay a stale source, and the source branch continues
to bypass unsupported-command and find-panel composition.

The null-backend regression establishes first-line glyphs before setting the
stale-source state, then checks the same glyphs and bottom prompt, a retained
notice frame, and bottom-row restoration. It also guards composition against
allocation. Fixture dimensions derive from the prompt literal and the tab,
fixture-line and status rows (E), with no new timing or iteration budget.
The clean-source submitted-frame probe follows the new status location so
silent reload coverage cannot miss a transient prompt.
