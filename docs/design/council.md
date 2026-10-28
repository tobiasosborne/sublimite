# Design council (edit-cxho.23)

The council is a loop that grades screenshots of sublimité against `docs/design/rubric.md`. Its scores are advisory (CLAUDE.md law 1). A design change lands only when:

- the council's mean rises with no criterion regressed for any member, **and**
- Tobias approves the before/after sheet;

and every change carries its perf cost line (rubric "Fixed constraints"). The loop shape is borrowed from the council skill in t3dotgg/tstack. The rubric, the screenshots and the blind A/B design are ours.

## Members

There is one batched call per member per round, covering all scenes and both themes.

| Member | Call | Cost measured 2026-10-27 [AC] |
|---|---|---|
| Codex `gpt-6.1-sol`, xhigh | `codex exec -s read-only -i <png>… --output-schema` | 54,486 tokens (14 images), 88,445 tokens (28 images) (M); the weekly reading did not move from 39 % (M, `quota`, 1 % steps) |
| Muse `muse-wonder-full-1.0`, high | `muse exec --image <png>… --output-schema --prompt-file` | 79 s per call; weekly 6 → 7 % over the two rounds (M, `muse usage`, 1 % steps) |
| Claude Opus | `claude -p --model opus --allowedTools Read --json-schema` | the weekly reading moved 4 → 5 % over the whole session, including the coordinator (M) |

## Run

```sh
sh tools/design_shots.sh build/sublimite /tmp/shots-a          # 8 scenes x dark/light
python3 tools/council_run.py prepare --mode absolute --a /tmp/shots-a --out /tmp/council-1
python3 tools/council_run.py prepare --mode ab --a /tmp/shots-a --b /tmp/shots-b --out /tmp/council-2 --seed N
python3 tools/council_run.py run /tmp/council-2 [--member codex] [--dry-run]
python3 tools/council_run.py aggregate /tmp/council-2            # writes report.md
```

- **What is sent:** six scenes are sent at half size, plus two 1:1 crops of `07-code` for typography.
- **Absolute mode** scores one build against the standard, then ranks its gaps.
- **A/B mode** is blind. The sides are labelled A and B in a seeded random order, and `key.json` stays on disk. The prompt is checked for words that would name a side.
- **Control pair:** one control scene (`03-find`) shows the same build on both sides. A member that prefers one side of the control is counted as noise.
- **The opus member** reads images by absolute path. Round 1 first sent bare names: Opus saw no pixels and said so, and that run was discarded.

Run directories, images and member output stay outside git (scratch). Each round's report is copied to `docs/reviews/design-council-<date>.md`.

## Target images (ideation, not the bar)

Each round may start with generated target images (Codex sol image_gen, through `tools/codex_run.py`; first set `docs/design/targets-2026-10-28/`). They exist to produce ideas and to calibrate the council's eye: the council grades them on the rubric like any build, and its synthesis lists the strongest moves to try. They are never the end state and never the yardstick: a build is graded against the ultimate design the rubric describes, not against its resemblance to a target (Tobias, 2026-10-28; rubric "The standard").

## Tests

- `python3 tests/council_aggregate_test.py` covers the aggregate, the schema validation, the dry-run call count, the blind prompt and the absolute paths.
- `python3 tests/rubric_lint_test.py` checks the rubric's shape and law-4 tags, and that it never names an editor as a reference.
