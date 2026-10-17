# edit-ywd: command palette

The palette shares the bottom-grid panel geometry and ASCII atlas used by find.
Its query occupies the first row; actions occupy the remaining rows of the
existing replace/find-in-files panel, clipped by the existing geometry rule that
preserves a document row. No independent panel height or visible-result limit.

Labels title-case underscore-separated names from `config/actions.inc`.
Spelling overrides expand Document Home/End, Beginning/End Of Line, and XML.
The config action-name capacity also fits these expanded labels. Query storage
uses that capacity: a longer query cannot match any label, and excess input is
refused without modifying the query. Backspace removes its final grapheme.

Matching folds ASCII case and accepts subsequences. Tightness is the minimum
matching span minus the query length: the count of intervening unmatched bytes
(P). Every possible first match is considered, with greedy following matches
giving its earliest end. Equal tightness retains action-table order. Empty
queries retain the table order. Up/Down clamp at the ends; scrolling keeps the
selected action visible. Enter with no matches closes without dispatching.

Storage is prepaid from the editor's base arena at initialization. The action
enum's non-routing range supplies a compile-time capacity, statically checked
against the actual action table count. Rows keep immutable default command
pointers; only result indices move. Labels, queries and ranking never allocate.

Only commands with a default argument form appear. Numeric payloads and variant
flags are excluded, including index-zero tab/group actions and insert/scroll/
layout/fold-level/directional-group/column actions. Font Size accepts its
dispatcher-defined reset default. Unimplemented no-argument intents keep the
existing dispatcher notice. Existing capability flags remain on commands.

Chord display resolves the live override/default map using its actual lookup,
including prefix reachability for multi-stroke chords. It requires the same
action, flags and value as the default command. Normal keyboard chords are
preferred over device keys, then standalone strokes, then table order. Unbound
commands remain callable and show an empty chord. Chords sit at the right grid
edge; labels clip before them. The keys module formats both prefix and final
stroke without allocation.

The key handler's post-lookup action ladder is factored into
`editor_dispatch_action`. Palette Enter closes the panel and passes the selected
immutable command plus its synthetic stroke into that same function. This
preserves command arguments, field dispatch, repeat/history and Save As prompt
behavior. Palette keys are captured ahead of find/global bindings. Opening it
hides the find panel and clears pending find selection; active replacement
refuses opening. Escape, mutation and focus loss close it. Shared mutation
bookkeeping closes immediately; owner/revision/source checks also cover buffer
replacement and asynchronous source changes.
Mutation bookkeeping closes after transforming the displayed scroll state:
closing earlier resizes a scroll configuration that the edit then restores
from its prior displayed state, losing the restored document geometry.

The find panel has no row-click dispatch to copy, so mouse selection is omitted
as authorized by the brief. Menu bar, file palette and new actions stay outside
this bead.
