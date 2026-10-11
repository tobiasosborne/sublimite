# edit-d50 — Linux default-key audit

Attribution: src:tobias. This bead remains incomplete: the implemented single-selection fast paths build and have red/green regression coverage, but the text commands identified below remain unsupported.

The authority is the installed `/opt/sublime_text/Packages/Default.sublime-package`, member `Default (Linux).sublime-keymap`, plus its macros, `transform.py`, `transpose.py`, `mark.py`, `kill_ring.py`, and `sort.py`. [Sublime’s key-binding documentation](https://www.sublimetext.com/docs/key_bindings.html) defines the command/argument/context contract. The ordinary editing context is used below; later completion/snippet/HTML/panel overrides are not treated as normal editing defaults. Shift+Tab uses the documented default unindent context, and Ctrl+Enter uses the Add Line macro context.

All argument numbers in the audit table are (G), transcribed from the named installed keymap entry. Names, key labels, dates, code points, line diagnostics and version identifiers are identifiers rather than performance budgets. Counts below are (M)[AC], derived by parsing the initial flags and matching chord labels against the installed JSON keymap; BAT0 was Charging at the initial red run.

Current audit: **218 originally flagged bindings = 75 executable bindings/subsets + 143 retained flags** (M)[AC]. Derivation: the previous audit counted the original flagged rows; count current `KEYS_ARG_UNIMPLEMENTED` rows in `src/keys/keys.c`, then subtract from the original count. The current source rows are recorded in `build/edit-d50-continuation/remaining-flags.json`; BAT0 was Not charging at validation. These are binding counts including aliases, not distinct commands. Occurrence and comment subsets retain the explicit limitations below.

## Continuation: daily-use command gaps

Toggle-comment file-language fast paths, an editable exact go-to-line prompt,
Ctrl+D first-word selection, Ctrl+K Ctrl+C centering and Ctrl+K Ctrl+D sole-
selection occurrence replacement are implemented. `KEYS_ARG_SINGLE_SELECTION`
marks occurrence subsets explicitly; further Ctrl+D multicaret expansion still
refuses unchanged. The new tests were executed red before implementation and
pass in release and ASan/UBSan builds. Full evidence is in the worker report.

Comments follow the installed `comment.py` common-indent/blank-line/variant
behavior and language comment preference tokens. The filename mapping and
local lexer do not replace embedded/custom syntax scopes. Existing view scan
and output limits remain, including document-size refusal for occurrence skip.
Go-to-line input is editor-owned/prepaid, renders in the existing status row,
previews exact index positions and records soft history only on acceptance.
Escape restores selection and viewport. Its seek continues through the
index's bounded foreground API without allocating or guessing a byte offset.
Centering uses exact logical or visual rows and preserves text/selection/history.

Syntax reindent, clipboard-history UI, folding/diff/pane chords, multiple-
selection commands and oversized edit continuations remain unsupported.
Native Sublime/display verification and the comparison drive remain coordinator
work. This continuation does not close the broader bead.

## Implemented semantics and integration

- Duplicate: duplicate selected bytes after the selection, retaining direction; with a caret, duplicate the hard line and move the caret into its copy. An unterminated final line receives the current/preceding line’s LF or CRLF separator.
- Delete line / move line: include the intersected hard lines, exclude a nonempty selection’s endpoint at the next line start, preserve line content and the document’s final-terminator status when swapping with a neighbor. Boundary swaps are no-ops.
- Join: remove the line break and surrounding horizontal whitespace, use a joining space when both sides contain text, put a caret at the first seam, and rebase a nonempty selection. Removed-whitespace endpoints are mapped to that seam.
- Add line before/after: implement the installed macro’s hard-line insertion, preserve leading indentation and LF/CRLF, and collapse the original selection at the inserted line. Syntax-rule indentation remains unavailable.
- Hard BOL/EOL delete: move the cursor to the hard boundary while retaining the original anchor, then delete the resulting range. At an empty boundary, delete the neighboring newline. The Ctrl+K macros instead use the visual row and smart BOL, including soft-end affinity; approximate visual queries are refused, never used to delete guessed bytes.
- Indent/unindent: process intersected lines, exclude the next-line endpoint, use the buffer’s tab preference and configured tab stop, consume mixed space/tab prefixes by visual columns, and map both selection endpoints. An unchanged unindent does not alter revision/history.
- Transpose: match the installed script’s single-caret character swap and disabled single nonempty selection. UTF-8 scalars are preserved, and CRLF is kept together when moving a line separator. The native editor normalizes line endings internally; the coordinator must compare its exported CRLF result.
- Case: transform the selected text, or the Unicode word at a caret, with full Unicode expansions and contextual final sigma. The generator records the host Python Unicode version and uses vendored derived properties restricted to that repertoire. Embedded Sublime Python repertoire parity still needs coordinator verification.
- Sort: expand/shrink-wrap the selected lines, or use the whole buffer when no selection exists; sort stably, default by `lower()` and optionally case-sensitively; retain the final line terminator and select the sorted range, matching `sort.py`.
- Subwords: move at camel/acronym/digit/underscore boundaries with Unicode letter/number classification, preserving the anchor for the extending bindings.
- Brackets: bounded raw paired-bracket navigation and interior selection. No parser/scope state exists here; strings/comments and repeated nested expansion still require golden-master verification.
- Marks: set, cover/select, swap and clear the current selection’s mark; rebase marks after edits. Delete-to-mark captures the removed bytes; a coincident/missing mark follows the script’s ordinary backward deletion.
- Kill/yank: preserve the top kill entry, append/prepend consecutive forward/backward kills at the same point, seal on yank, and replace a selection on yank. History cycling is still a panel command. The top is editor-owned; marks, overwrite, soft history and repeat records are buffer-owned. Marks, soft history and repeat records reset with source replacement; overwrite mode stays with its buffer.
- Overwrite: replace the typed grapheme count up to a hard newline, handle selection replacement through the existing edit path, and keep Enter insertion behavior.
- Soft undo/redo: a prepaid selection/text timeline uses the configured text-history capacity. Selection records do not create text undo groups; text records cooperate with ordinary undo/redo, and typing-burst records coalesce. When text markers are evicted, underlying text redo precedes retained selection redo; new branches invalidate incompatible selection records.
- Redo-or-repeat: redo first; otherwise resolve the previous accepted editing key and its original binding arguments before ingress. Repeat has one physical input sequence, one submitted frame in the regression, and an independent undo group. Successful no-op editing commands remain repeatable.
- Current-line copy/cut: copy the hard line for an empty selection, without moving the caret; cut that range through the clipboard module’s existing grouped replacement path, preserving the original caret in undo.

Each new view preparation function returns one contiguous replacement plus the exact final selection. Preparation never mutates or allocates. The editor checks index/journal admission, opens one explicit undo group, stages delete/insert, repairs both endpoints of new edit plans with `view_restore` (including equal-length edits), follows the cursor, closes the group and records its delta. Trusted undo/soft-history anchors are preserved while following only the cursor, avoiding a distant-anchor normalization continuation. No additional frame is submitted before a command finishes. This is verified internally, not a claim of measured parity with Sublime; the next comparison drive remains mandatory.

## Limits and unfinished text commands

The synchronous preparation path uses the existing `VIEW_WINDOW` and `VIEW_SCAN_BOUND` limits, not a new budget. Oversized source/range/output, a full kill top, or an approximate visual row is refused with preserved text/selection/history and the retained `not implemented` notice/counter. Large-command continuations are unfinished; this is a material golden-master gap.

Still unsupported text/selection behavior: `SELECT_PREVIOUS_LINE`, `WRAP_LINES`, `REINDENT`, `PASTE_AND_INDENT`, `SELECT_SCOPE`, `SELECT_SMART`, `SPLIT_SELECTION_LINES`, `COLUMN_SELECTION`, `FIND_ALL_UNDER`, `CLOSE_TAG`, `XML_TAG_SNIPPET`, `RECORD_MACRO`, `RUN_MACRO`, `REVERT_HUNK`, `REVERT_MODIFICATION`, and commands needing bookmarks/change navigation or search state. These are not relabeled as UI-only or implemented. Their exact native command/arguments are recorded in the audit table. Multi-selection needs a representation beyond the current cursor/anchor undo payload; syntax-dependent edits need syntax preferences/scopes; wrapping needs paragraph/ruler handling. The previous-line selection primitive also remains unimplemented. Comments and occurrence commands now have the limited executors described in the continuation section; embedded syntax, multiple selections and oversized requests remain unavailable.

Panel/window/project/build/completion/diff/folding/spelling/font commands remain consumed and counted with a notice. Extending previous/next-tab commands are consumed as unsupported tab-selection UI, rather than incorrectly changing the active tab. The regression loops over every retained flag and checks unchanged text, selection and history plus a counter increment.

Native-command caret/selection details inferred from the keymap and available sources (especially join, duplicate, brackets, subword behavior, empty-selection clipboard behavior and insert-before indentation) must be checked by the coordinator on the requested safe display. No Sublime process or comparison drive was launched by this worker.

## Original flagged bindings

“Implemented” below means the dispatcher/fast path exists, including aliases already wired before this bead; the limitations above still apply. “Retained” means the missing capability stays flagged, consumed and counted.

| Chord | Action | Sublime command and arguments (G: installed entry) | Final flag |
|---|---|---|---|
| `escape` | `CANCEL` | `cancel` | Retained |
| `insert` | `TOGGLE_OVERWRITE` | `toggle_overwrite` | Implemented |
| `undo` | `UNDO` | `undo` | Implemented |
| `redo` | `REDO_OR_REPEAT` | `redo_or_repeat` | Implemented |
| `context_menu` | `CONTEXT_MENU` | `context_menu` | Retained |
| `find` | `FIND` | `show_panel {"panel":"find","reverse":false}` | Retained |
| `f2` | `NEXT_BOOKMARK` | `next_bookmark` | Retained |
| `f3` | `FIND_NEXT` | `find_next` | Retained |
| `f4` | `NEXT_RESULT` | `next_result` | Retained |
| `f6` | `SPELL_CHECK` | `toggle_setting {"setting":"spell_check"}` | Retained |
| `f7` | `BUILD` | `build` | Retained |
| `f9` | `SORT_LINES` | `sort_lines {"case_sensitive":false}` | Implemented |
| `f11` | `FULL_SCREEN` | `toggle_full_screen` | Retained |
| `f12` | `GOTO_DEFINITION` | `goto_definition` | Retained |
| `close` | `CLOSE` | `close` | Implemented |
| `copy` | `COPY` | `copy` | Implemented |
| `cut` | `CUT` | `cut` | Implemented |
| `open` | `OPEN_FILE` | `prompt_open_file` | Retained |
| `paste` | `PASTE` | `paste` | Implemented |
| `save` | `SAVE` | `save {"async":true}` | Implemented |
| `shift+tab` | `UNINDENT` | `unindent` | Implemented |
| `shift+insert` | `PASTE` | `paste` | Implemented |
| `shift+f2` | `PREVIOUS_BOOKMARK` | `prev_bookmark` | Retained |
| `shift+f3` | `FIND_PREVIOUS` | `find_prev` | Retained |
| `shift+f4` | `PREVIOUS_RESULT` | `prev_result` | Retained |
| `shift+f10` | `CONTEXT_MENU` | `context_menu` | Retained |
| `shift+f11` | `DISTRACTION_FREE` | `toggle_distraction_free` | Retained |
| `shift+f12` | `GOTO_REFERENCE` | `goto_reference` | Retained |
| `shift+delete` | `CUT` | `cut` | Implemented |
| `ctrl+space` | `AUTO_COMPLETE` | `auto_complete` | Retained |
| `ctrl++` | `FONT_SIZE` | `increase_font_size` | Retained |
| `ctrl+,` | `PREVIOUS_MODIFICATION` | `prev_modification` | Retained |
| `ctrl+-` | `FONT_SIZE` | `decrease_font_size` | Retained |
| `ctrl+.` | `NEXT_MODIFICATION` | `next_modification` | Retained |
| `ctrl+/` | `TOGGLE_COMMENT` | `toggle_comment {"block":false}` | File-language subset |
| `ctrl+0` | `FOCUS_SIDEBAR` | `focus_side_bar` | Retained |
| `ctrl+1` | `FOCUS_GROUP` | `focus_group {"group":0}` | Retained |
| `ctrl+2` | `FOCUS_GROUP` | `focus_group {"group":1}` | Retained |
| `ctrl+3` | `FOCUS_GROUP` | `focus_group {"group":2}` | Retained |
| `ctrl+4` | `FOCUS_GROUP` | `focus_group {"group":3}` | Retained |
| `ctrl+5` | `FOCUS_GROUP` | `focus_group {"group":4}` | Retained |
| `ctrl+6` | `FOCUS_GROUP` | `focus_group {"group":5}` | Retained |
| `ctrl+7` | `FOCUS_GROUP` | `focus_group {"group":6}` | Retained |
| `ctrl+8` | `FOCUS_GROUP` | `focus_group {"group":7}` | Retained |
| `ctrl+9` | `FOCUS_GROUP` | `focus_group {"group":8}` | Retained |
| `ctrl+;` | `GOTO_WORD` | `show_overlay {"overlay":"goto","text":"#"}` | Retained |
| `ctrl+=` | `FONT_SIZE` | `increase_font_size` | Retained |
| `ctrl+[` | `UNINDENT` | `unindent` | Implemented |
| `ctrl+]` | `INDENT` | `indent` | Implemented |
| `ctrl+`` | `CONSOLE` | `show_panel {"panel":"console","toggle":true}` | Retained |
| `ctrl+b` | `BUILD` | `build` | Retained |
| `ctrl+c` | `COPY` | `copy` | Implemented |
| `ctrl+d` | `ADD_NEXT_OCCURRENCE` | `find_under_expand` | Single-selection subset |
| `ctrl+e` | `SLURP_FIND` | `slurp_find_string` | Retained |
| `ctrl+f` | `FIND` | `show_panel {"panel":"find","reverse":false}` | Retained |
| `ctrl+g` | `GOTO_LINE` | `show_overlay {"overlay":"goto","text":":"}` | Implemented |
| `ctrl+h` | `REPLACE` | `show_panel {"panel":"replace","reverse":false}` | Retained |
| `ctrl+i` | `INCREMENTAL_FIND` | `show_panel {"panel":"incremental_find","reverse":false}` | Retained |
| `ctrl+m` | `GOTO_BRACKET` | `move_to {"to":"brackets"}` | Implemented |
| `ctrl+n` | `NEW_FILE` | `new_file` | Retained |
| `ctrl+o` | `OPEN_FILE` | `prompt_open_file` | Retained |
| `ctrl+p` | `GOTO_FILE` | `show_overlay {"overlay":"goto","show_files":true}` | Retained |
| `ctrl+q` | `EXIT` | `exit` | Retained |
| `ctrl+r` | `GOTO_SYMBOL` | `show_overlay {"overlay":"goto","text":"@"}` | Retained |
| `ctrl+s` | `SAVE` | `save {"async":true}` | Implemented |
| `ctrl+t` | `TRANSPOSE` | `transpose` | Implemented |
| `ctrl+u` | `SOFT_UNDO` | `soft_undo` | Implemented |
| `ctrl+v` | `PASTE` | `paste` | Implemented |
| `ctrl+w` | `CLOSE` | `close` | Implemented |
| `ctrl+x` | `CUT` | `cut` | Implemented |
| `ctrl+y` | `REDO_OR_REPEAT` | `redo_or_repeat` | Implemented |
| `ctrl+z` | `UNDO` | `undo` | Implemented |
| `ctrl+tab` | `NEXT_VIEW_STACK` | `next_view_in_stack` | Implemented |
| `ctrl+enter` | `INSERT_LINE_AFTER` | `run_macro_file {"file":"res://Packages/Default/Add Line.sublime-macro"}` | Implemented |
| `ctrl+up` | `SCROLL_LINES` | `scroll_lines {"amount":1.0}` | Retained |
| `ctrl+down` | `SCROLL_LINES` | `scroll_lines {"amount":-1.0}` | Retained |
| `ctrl+pageup` | `PREVIOUS_VIEW` | `prev_view` | Implemented |
| `ctrl+pagedown` | `NEXT_VIEW` | `next_view` | Implemented |
| `ctrl+insert` | `COPY` | `copy` | Implemented |
| `ctrl+break` | `CANCEL_BUILD` | `cancel_build` | Retained |
| `ctrl+keypad_enter` | `INSERT_LINE_AFTER` | `run_macro_file {"file":"res://Packages/Default/Add Line.sublime-macro"}` | Implemented |
| `ctrl+f2` | `TOGGLE_BOOKMARK` | `toggle_bookmark {"toggle_line":true}` | Retained |
| `ctrl+f3` | `FIND_UNDER` | `find_under` | Retained |
| `ctrl+f4` | `CLOSE_FILE` | `close_file` | Implemented |
| `ctrl+f6` | `NEXT_MISSPELLING` | `next_misspelling` | Retained |
| `ctrl+f9` | `SORT_LINES` | `sort_lines {"case_sensitive":true}` | Implemented |
| `ctrl+f12` | `GOTO_DEFINITION` | `goto_definition {"side_by_side":true,"clear_to_right":true}` | Retained |
| `ctrl+shift+space` | `SELECT_SCOPE` | `expand_selection {"to":"scope"}` | Retained |
| `ctrl+shift+/` | `TOGGLE_COMMENT` | `toggle_comment {"block":true}` | File-language subset |
| `ctrl+shift+1` | `MOVE_TO_GROUP` | `move_to_group {"group":0}` | Retained |
| `ctrl+shift+2` | `MOVE_TO_GROUP` | `move_to_group {"group":1}` | Retained |
| `ctrl+shift+3` | `MOVE_TO_GROUP` | `move_to_group {"group":2}` | Retained |
| `ctrl+shift+4` | `MOVE_TO_GROUP` | `move_to_group {"group":3}` | Retained |
| `ctrl+shift+5` | `MOVE_TO_GROUP` | `move_to_group {"group":4}` | Retained |
| `ctrl+shift+6` | `MOVE_TO_GROUP` | `move_to_group {"group":5}` | Retained |
| `ctrl+shift+7` | `MOVE_TO_GROUP` | `move_to_group {"group":6}` | Retained |
| `ctrl+shift+8` | `MOVE_TO_GROUP` | `move_to_group {"group":7}` | Retained |
| `ctrl+shift+9` | `MOVE_TO_GROUP` | `move_to_group {"group":8}` | Retained |
| `ctrl+shift+[` | `FOLD` | `fold` | Retained |
| `ctrl+shift+]` | `UNFOLD` | `unfold` | Retained |
| `ctrl+shift+a` | `SELECT_SMART` | `expand_selection {"to":"smart"}` | Retained |
| `ctrl+shift+b` | `BUILD` | `build {"select":true}` | Retained |
| `ctrl+shift+d` | `DUPLICATE_LINE` | `duplicate_line` | Implemented |
| `ctrl+shift+e` | `SLURP_REPLACE` | `slurp_replace_string` | Retained |
| `ctrl+shift+f` | `FIND_IN_FILES` | `show_panel {"panel":"find_in_files"}` | Retained |
| `ctrl+shift+h` | `REPLACE_NEXT` | `replace_next` | Retained |
| `ctrl+shift+i` | `INCREMENTAL_FIND` | `show_panel {"panel":"incremental_find","reverse":true}` | Retained |
| `ctrl+shift+j` | `JOIN_LINES` | `join_lines` | Implemented |
| `ctrl+shift+k` | `DELETE_LINE` | `run_macro_file {"file":"res://Packages/Default/Delete Line.sublime-macro"}` | Implemented |
| `ctrl+shift+l` | `SPLIT_SELECTION_LINES` | `split_selection_into_lines` | Retained |
| `ctrl+shift+m` | `SELECT_BRACKETS` | `expand_selection {"to":"brackets"}` | Implemented |
| `ctrl+shift+n` | `NEW_WINDOW` | `new_window` | Retained |
| `ctrl+shift+p` | `COMMAND_PALETTE` | `show_overlay {"overlay":"command_palette"}` | Retained |
| `ctrl+shift+r` | `GOTO_PROJECT_SYMBOL` | `goto_symbol_in_project` | Retained |
| `ctrl+shift+s` | `SAVE_AS` | `prompt_save_as` | Retained |
| `ctrl+shift+t` | `REOPEN_FILE` | `reopen_last_file {"source":"window"}` | Implemented |
| `ctrl+shift+u` | `SOFT_REDO` | `soft_redo` | Implemented |
| `ctrl+shift+v` | `PASTE_AND_INDENT` | `paste_and_indent` | Retained |
| `ctrl+shift+w` | `CLOSE_WINDOW` | `close_window` | Retained |
| `ctrl+shift+z` | `REDO` | `redo` | Implemented |
| `ctrl+shift+backspace` | `DELETE_TO_BOL` | `run_macro_file {"file":"res://Packages/Default/Delete to Hard BOL.sublime-macro"}` | Implemented |
| `ctrl+shift+tab` | `PREVIOUS_VIEW_STACK` | `prev_view_in_stack` | Implemented |
| `ctrl+shift+enter` | `INSERT_LINE_BEFORE` | `run_macro_file {"file":"res://Packages/Default/Add Line Before.sublime-macro"}` | Implemented |
| `ctrl+shift+up` | `MOVE_LINE_UP` | `swap_line_up` | Implemented |
| `ctrl+shift+down` | `MOVE_LINE_DOWN` | `swap_line_down` | Implemented |
| `ctrl+shift+pageup` | `PREVIOUS_VIEW` | `prev_view {"extend":true}` | Retained |
| `ctrl+shift+pagedown` | `NEXT_VIEW` | `next_view {"extend":true}` | Retained |
| `ctrl+shift+keypad_enter` | `INSERT_LINE_BEFORE` | `run_macro_file {"file":"res://Packages/Default/Add Line Before.sublime-macro"}` | Implemented |
| `ctrl+shift+f2` | `CLEAR_BOOKMARKS` | `clear_bookmarks` | Retained |
| `ctrl+shift+f3` | `FIND_UNDER_PREVIOUS` | `find_under_prev` | Retained |
| `ctrl+shift+f6` | `PREVIOUS_MISSPELLING` | `prev_misspelling` | Retained |
| `ctrl+shift+f12` | `GOTO_REFERENCE` | `goto_reference {"side_by_side":true,"clear_to_right":true}` | Retained |
| `ctrl+shift+delete` | `DELETE_TO_EOL` | `run_macro_file {"file":"res://Packages/Default/Delete to Hard EOL.sublime-macro"}` | Implemented |
| `alt+-` | `JUMP_BACK` | `jump_back` | Retained |
| `alt+.` | `CLOSE_TAG` | `close_tag` | Retained |
| `alt+/` | `AUTO_COMPLETE` | `auto_complete` | Retained |
| `alt+1` | `SELECT_TAB` | `select_by_index {"index":0}` | Implemented |
| `alt+2` | `SELECT_TAB` | `select_by_index {"index":1}` | Implemented |
| `alt+3` | `SELECT_TAB` | `select_by_index {"index":2}` | Implemented |
| `alt+4` | `SELECT_TAB` | `select_by_index {"index":3}` | Implemented |
| `alt+5` | `SELECT_TAB` | `select_by_index {"index":4}` | Implemented |
| `alt+6` | `SELECT_TAB` | `select_by_index {"index":5}` | Implemented |
| `alt+7` | `SELECT_TAB` | `select_by_index {"index":6}` | Implemented |
| `alt+8` | `SELECT_TAB` | `select_by_index {"index":7}` | Implemented |
| `alt+9` | `SELECT_LAST_TAB` | `select_last_tab` | Implemented |
| `alt+l` | `SELECT_PREVIOUS_LINE` | `expand_selection {"to":"line_prev"}` | Retained |
| `alt+o` | `SWITCH_FILE` | `switch_file {"extensions":["cpp","cxx","cc","c","hpp","hxx","hh","h","ipp","inl","m","mm"]}` | Retained |
| `alt+q` | `WRAP_LINES` | `wrap_lines` | Retained |
| `alt+left` | `SUBWORD_LEFT` | `move {"by":"subwords","forward":false}` | Implemented |
| `alt+right` | `SUBWORD_RIGHT` | `move {"by":"subword_ends","forward":true}` | Implemented |
| `alt+keypad_minus` | `JUMP_BACK` | `jump_back` | Retained |
| `alt+f2` | `SELECT_BOOKMARKS` | `select_all_bookmarks` | Retained |
| `alt+f3` | `FIND_ALL_UNDER` | `find_all_under` | Retained |
| `alt+shift+-` | `JUMP_FORWARD` | `jump_forward` | Retained |
| `alt+shift+1` | `SET_LAYOUT` | `set_layout {"cols":[0.0,1.0],"rows":[0.0,1.0],"cells":[[0,0,1,1]]}` | Retained |
| `alt+shift+2` | `SET_LAYOUT` | `set_layout {"cols":[0.0,0.5,1.0],"rows":[0.0,1.0],"cells":[[0,0,1,1],[1,0,2,1]]}` | Retained |
| `alt+shift+3` | `SET_LAYOUT` | `set_layout {"cols":[0.0,0.33,0.66,1.0],"rows":[0.0,1.0],"cells":[[0,0,1,1],[1,0,2,1],[2,0,3,1]]}` | Retained |
| `alt+shift+4` | `SET_LAYOUT` | `set_layout {"cols":[0.0,0.25,0.5,0.75,1.0],"rows":[0.0,1.0],"cells":[[0,0,1,1],[1,0,2,1],[2,0,3,1],[3,0,4,1]]}` | Retained |
| `alt+shift+5` | `SET_LAYOUT` | `set_layout {"cols":[0.0,0.5,1.0],"rows":[0.0,0.5,1.0],"cells":[[0,0,1,1],[1,0,2,1],[0,1,1,2],[1,1,2,2]]}` | Retained |
| `alt+shift+8` | `SET_LAYOUT` | `set_layout {"cols":[0.0,1.0],"rows":[0.0,0.5,1.0],"cells":[[0,0,1,1],[0,1,1,2]]}` | Retained |
| `alt+shift+9` | `SET_LAYOUT` | `set_layout {"cols":[0.0,1.0],"rows":[0.0,0.33,0.66,1.0],"cells":[[0,0,1,1],[0,1,1,2],[0,2,1,3]]}` | Retained |
| `alt+shift+o` | `SWITCH_FILE` | `switch_file {"extensions":["cpp","cxx","cc","c","hpp","hxx","hh","h","ipp","inl","m","mm"],"side_by_side":true}` | Retained |
| `alt+shift+w` | `XML_TAG_SNIPPET` | `insert_snippet {"name":"Packages/XML/Snippets/xml-long-tag.sublime-snippet"}` | Retained |
| `alt+shift+left` | `SUBWORD_LEFT` | `move {"by":"subwords","forward":false,"extend":true}` | Implemented |
| `alt+shift+up` | `COLUMN_SELECTION` | `select_lines {"forward":false}` | Retained |
| `alt+shift+right` | `SUBWORD_RIGHT` | `move {"by":"subword_ends","forward":true,"extend":true}` | Implemented |
| `alt+shift+down` | `COLUMN_SELECTION` | `select_lines {"forward":true}` | Retained |
| `alt+shift+keypad_minus` | `JUMP_FORWARD` | `jump_forward` | Retained |
| `ctrl+alt+p` | `SELECT_WORKSPACE` | `prompt_select_workspace` | Retained |
| `ctrl+alt+q` | `RECORD_MACRO` | `toggle_record_macro` | Retained |
| `ctrl+alt+shift+p` | `SHOW_SCOPE` | `show_scope_name` | Retained |
| `ctrl+alt+shift+q` | `RUN_MACRO` | `run_macro` | Retained |
| `ctrl+k ctrl+space` | `SET_MARK` | `set_mark` | Implemented |
| `ctrl+k ctrl+/` | `INLINE_DIFF` | `toggle_inline_diff` | Retained |
| `ctrl+k ctrl+0` | `UNFOLD_ALL` | `unfold_all` | Retained |
| `ctrl+k ctrl+1` | `FOLD_LEVEL` | `fold_by_level {"level":1}` | Retained |
| `ctrl+k ctrl+2` | `FOLD_LEVEL` | `fold_by_level {"level":2}` | Retained |
| `ctrl+k ctrl+3` | `FOLD_LEVEL` | `fold_by_level {"level":3}` | Retained |
| `ctrl+k ctrl+4` | `FOLD_LEVEL` | `fold_by_level {"level":4}` | Retained |
| `ctrl+k ctrl+5` | `FOLD_LEVEL` | `fold_by_level {"level":5}` | Retained |
| `ctrl+k ctrl+6` | `FOLD_LEVEL` | `fold_by_level {"level":6}` | Retained |
| `ctrl+k ctrl+7` | `FOLD_LEVEL` | `fold_by_level {"level":7}` | Retained |
| `ctrl+k ctrl+8` | `FOLD_LEVEL` | `fold_by_level {"level":8}` | Retained |
| `ctrl+k ctrl+9` | `FOLD_LEVEL` | `fold_by_level {"level":9}` | Retained |
| `ctrl+k ctrl+;` | `INLINE_DIFF` | `toggle_inline_diff {"prefer_hide":true}` | Retained |
| `ctrl+k ctrl+]` | `REINDENT` | `reindent {"single_line":true}` | Retained |
| `ctrl+k ctrl+a` | `SELECT_TO_MARK` | `select_to_mark` | Implemented |
| `ctrl+k ctrl+b` | `TOGGLE_SIDEBAR` | `toggle_side_bar` | Retained |
| `ctrl+k ctrl+c` | `SHOW_AT_CENTER` | `show_at_center` | Implemented |
| `ctrl+k ctrl+d` | `SKIP_NEXT_OCCURRENCE` | `find_under_expand_skip` | Single-selection subset |
| `ctrl+k ctrl+g` | `CLEAR_MARK` | `clear_bookmarks {"name":"mark"}` | Implemented |
| `ctrl+k ctrl+j` | `UNFOLD_ALL` | `unfold_all` | Retained |
| `ctrl+k ctrl+k` | `DELETE_TO_EOL` | `run_macro_file {"file":"res://Packages/Default/Delete to Hard EOL.sublime-macro"}` | Implemented |
| `ctrl+k ctrl+l` | `LOWER_CASE` | `lower_case` | Implemented |
| `ctrl+k ctrl+t` | `FOLD_TAG_ATTRIBUTES` | `fold_tag_attributes` | Retained |
| `ctrl+k ctrl+u` | `UPPER_CASE` | `upper_case` | Implemented |
| `ctrl+k ctrl+v` | `PASTE_HISTORY` | `paste_from_history` | Retained |
| `ctrl+k ctrl+w` | `DELETE_TO_MARK` | `delete_to_mark` | Implemented |
| `ctrl+k ctrl+x` | `SWAP_MARK` | `swap_with_mark` | Implemented |
| `ctrl+k ctrl+y` | `YANK` | `yank` | Implemented |
| `ctrl+k ctrl+z` | `REVERT_HUNK` | `revert_hunk` | Retained |
| `ctrl+k ctrl+backspace` | `DELETE_TO_BOL` | `run_macro_file {"file":"res://Packages/Default/Delete to Hard BOL.sublime-macro"}` | Implemented |
| `ctrl+k ctrl+left` | `FOCUS_NEIGHBOR_GROUP` | `focus_neighboring_group {"forward":false}` | Retained |
| `ctrl+k ctrl+up` | `NEW_PANE` | `new_pane` | Retained |
| `ctrl+k ctrl+right` | `FOCUS_NEIGHBOR_GROUP` | `focus_neighboring_group` | Retained |
| `ctrl+k ctrl+down` | `CLOSE_PANE` | `close_pane` | Retained |
| `ctrl+k ctrl+shift+z` | `REVERT_MODIFICATION` | `revert_modification` | Retained |
| `ctrl+k ctrl+shift+left` | `MOVE_NEIGHBOR_GROUP` | `move_to_neighboring_group {"forward":false}` | Retained |
| `ctrl+k ctrl+shift+up` | `NEW_PANE` | `new_pane {"move":false}` | Retained |
| `ctrl+k ctrl+shift+right` | `MOVE_NEIGHBOR_GROUP` | `move_to_neighboring_group` | Retained |
| `ctrl+j ctrl+j` | `PRIMARY_J_CHANGED` | `primary_j_changed` | Retained |
| `ctrl+j ctrl+left` | `UNSELECT_LEFT` | `unselect_to_left` | Retained |
| `ctrl+j ctrl+up` | `UNSELECT_OTHERS` | `unselect_others` | Retained |
| `ctrl+j ctrl+right` | `UNSELECT_RIGHT` | `unselect_to_right` | Retained |
| `ctrl+j ctrl+pageup` | `FOCUS_LEFT` | `focus_to_left` | Retained |
| `ctrl+j ctrl+pagedown` | `FOCUS_RIGHT` | `focus_to_right` | Retained |
| `ctrl+j ctrl+shift+left` | `SELECT_LEFT` | `select_to_left` | Retained |
| `ctrl+j ctrl+shift+right` | `SELECT_RIGHT` | `select_to_right` | Retained |

## Fix: undo shrinking text above an unwrapped top (edit-d50-fix)

3kk keeps a displayed scroll state (`scroll_display`) separate from the
requested one and, on an edit, only queued a byte seek; the display kept its
pre-edit byte and `editor_scroll_mirror` copied it into the view, so an undo
that removed text at or above the top laid out a stale byte (past EOF:
`layout_begin` refuses it and `editor_step` fails; inside the text: a mid-line
top under the old line label). `editor_scroll_edit` now transforms the
displayed top with each edit: wholly above it is an exact byte and line shift;
an edit that replaces the byte before the top moves it onto the edit and marks
the label unknown, and `editor_scroll_follow` re-certifies it from the view's
normalized top (line start plus label) until the pending seek resolves. An
edit at the top byte leaves it unchanged, so trusted far anchors still take no
view continuation. `editor_full_layout` clamps a top past EOF as a backstop.
Trusted history follow uses main's empty TYPE again; new edit plans keep
`view_restore` for both endpoints. The verifier's attribution to the
`view_restore` change did not hold: with empty TYPE restored alone the case
still failed (both reach the same `normalize`).

The stale-top defect fixed in `editor_scroll_edit` is 3kk's, not d50's:
main's `editor_scroll_edit`/`editor_scroll_mirror` are identical to the
pre-fix code, so any undo, redo or find replace that removes text at or above
an unwrapped top has the same exposure there; d50's toggle-comment undo only
made it reachable from a test.

## Go-to-line drives the 3kk scroll state (edit-d50-fix)

Unwrapped viewports are owned by the scroll state since 3kk: a top written only
into the view is mirrored back to the displayed one, so the prompt previously
moved the caret without scrolling. `goto_continue` now adopts the target's
certified line start with `editor_scroll_place`, or seeks to it with
`editor_scroll_seek` when the scroll policy clips it (a target in the last
page), as `show_at_center` does. The prompt snapshots `scroll_display` when it
opens; Escape and an invalid entry restore that top through the same
place/seek path, not view state alone.
