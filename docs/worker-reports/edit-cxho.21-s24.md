# edit-cxho.21 report (s24)

Files: tools/design_shots.sh, tools/test_design_shots.sh, tools/design_fixtures/{readme.md,hello.c,util.py,notes.txt}, docs/decisions/edit-cxho.21.md. No editor code touched. (Brief paths used instead of the bead's tests/ paths.)

Scenes (each x dark, light = 16 PNGs, 3296x2060, Xvfb -dpi 192): 01-empty, 02-tabs (hello.c util.py notes.txt readme.md, README active), 03-find (ctrl+f "minimap", match selected 1/1), 04-replace (ctrl+h "minimap"), 05-goto (ctrl+g "12"), 06-palette (ctrl+shift+p "wrap"), 07-code (hello.c, syntax colours), 08-large (20000-line generated Markdown, ctrl+g 10000, minimap with viewport box). Light exists as config `theme = "light"`; nothing added.

Red: test run with tools/design_shots.sh absent: `sh: 0: cannot open tools/design_shots.sh: No such file` (the test aborts under set -e; the shell prints that, exit status of the pipeline was masked by tail).
Green: `test_design_shots: PASS`, exit 0 (manifest 16 entries, every PNG >1 colour, two runs byte-identical).

Determinism (two runs + a third run in build/design_shots; sha256 identical, sample): 01-empty-dark 635697471e27..., 02-tabs-dark ba654b4cfeccc..., 03-find-dark a074ab8a9b89..., 07-code-light a4acbb0dc4ee..., 08-large-light c0bd617836cae... (all 16 hashes in manifest.json).
Findings on the way: (1) the caret blinks (500 ms) in plain-document scenes until the 10 s idle stop, so the settle rule is "screen unchanged 1.2 s", which lands on the caret-visible state; (2) PNG bytes differed only through date tEXt chunks (pixels identical): fixed with -strip; (3) xwd headers differ between grabs (colormap) so comparisons skip 3179 bytes.

Runtime (M): full script 84 s on battery ("Discharging"), load average 6.7 (other lanes busy); test (2 runs) 181 s. Not added to make check (existing tools/test_*.sh run in seconds); manual: `sh tools/test_design_shots.sh [binary]`.

Contact sheet: /home/tobias/Projects/sublimite/.wt/edit-cxho.21-s24/build/design_shots/contact-sheet.png (outputs and manifest.json in the same dir; build/ is untracked).

Outside scope: none needing action. Note: the editor window defaults to 1977x1442 under Xvfb without a WM; the harness resizes it, which is not how Tobias's panel opens it (fullscreen/maximised by his WM may differ in chrome).
Verification: make -j4 all exit 0 (built at start); sh -n tools/design_shots.sh ok; Xvfb :91/:92 leftover check: none.
