# v2-he7.26 — The lane manifest is a per-suite table

Supersedes the manifest format and the `*_EMPTY_WAVE` markers of
`docs/decisions/v2-he7.19.md`. The rest of that decision stands: discovery,
the inventory check before every lane, and the run counts.

## Invariant

**Each suite runs in exactly the lanes its author declared, and that
declaration is one line that belongs to the suite.** A registration can be
lost only by changing or deleting that suite's own line, and both changes show
in the diff under the suite's name.

## Format

`tests/lanes.mk` holds one line per `*_test.c` / `*_fuzz.c`, sorted by path:

    tests/trace_test.c := san tsan
    fuzz/trace_fuzz.c := fuzz

Lanes: `layers san law2 tsan fuzz`. Comment and blank lines are allowed. Each
line is also a make assignment: the variable is named by the suite's path and
its value is the suite's lanes. The Makefile derives the lane lists right after
the include and assigns them nowhere else:

    LANE_TABLE := $(sort $(filter tests/%.c fuzz/%.c,$(.VARIABLES)))
    lane_suites = $(foreach s,$(LANE_TABLE),$(if $(filter $(1),$($(s))),$(s)))
    SAN_SUITES := $(call lane_suites,san)   # ... one per lane

`tools/check_layout.py suites` parses the same text and rejects these cases:

- a line that is not `<tests/…_test.c|fuzz/…_fuzz.c> := <lane>…` starting at
  column 0. This covers any `$`, `+=`, continuation, indentation, trailing
  comment and every other variable, including `TSAN_SUITES := …`. The grammar
  admits only lines whose make meaning equals their table meaning, so make and
  the checker cannot read the file differently;
- a duplicate line (make would silently keep the last one), an unsorted line,
  an unknown lane, or a lane named twice;
- a suite on disk with no line, and a line whose file is missing or sits in the
  wrong inventory (fuzzers run only in `fuzz`);
- an empty lane;
- a lane list derived by make that differs from the table. This guards the
  derivation itself (fixture: a Makefile that filters trace out of TSan);
- the name rules carried over from he7.19: `law2_`/`g1_` suites must be in
  `law2`, plan-listed threaded suites must be in `tsan`, and `layers` must be
  exactly L0–L4.

The include stays `-include`: the dependency-lint fixtures
(`tests/lint/run.py`) build objects in a copy of the Makefile without the
manifest. A missing manifest derives five empty lanes, and `check-suites` (a
prerequisite of every lane) rejects it as `missing manifest`.

The lanes are ordered by path now, not by the order of the `+=` blocks.
Membership is unchanged (see Evidence). A merge of two new suites touches two
different sorted lines. They conflict only when they are adjacent, and then
both lines stay visible in the conflict.

## Declared-empty markers removed

`LAW2_EMPTY_WAVE`, `TSAN_EMPTY_WAVE` and `FUZZ_EMPTY_WAVE` were set nowhere:
all five lanes have had suites since Wave 1. Lanes never lose all their suites
by design, so the marker branch in `check-suites` and the `declared_empty`
recipe branches were dead code. They are deleted (no fossils). Every lane now
requires at least one suite.

## Simplest alternative and its counter-example

The fallback was to keep the `+=` manifest and reject re-assigning a lane
variable after its first definition. It stops the ipc shape, but it does not
remove the cause: one registration is spread across up to four lines in four
variables, each of which can be lost independently. Counter-examples it passes:

1. `TSAN_SUITE += tests/os_endpoint_test.c`: a misspelled lane variable is a
   new variable, assigned once. The suite stays in SAN, so the current check
   sees it registered. It silently never runs under TSan.
2. A merge conflict in `TSAN_SUITES += tests/a_test.c tests/b_test.c` resolved
   by keeping one side's line drops a word without re-assigning anything.
3. Rewriting the first definition itself, e.g. `TSAN_SUITES := tests/os_test.c
   tests/base_test.c` to `TSAN_SUITES := tests/os_test.c tests/ipc_test.c`,
   drops base without any re-assignment. The diff shows a change to a line
   shared by several suites, not to base's registration.

In the table, (1) is an unknown lane or a non-suite line. (2) and (3) cannot
happen to a bystander, because no line is shared: a lost registration is a
deleted line (an unlisted suite) or an edit to that suite's own line. The
checker for the fallback would also need to model make's assignment semantics
(`=`, `:=`, `?=`, `+=`, `override`, `define`, conditionals). The table needs a
one-line regular expression.

Counter-example to the table itself: deleting a lane word from a suite's own
line (`tests/trace_test.c := san`). The name rules catch this only for law2,
threaded and layer suites. In every other case, the loss is a deliberate edit
to the line that names the suite, visible in review, and not an accident of
accumulation order elsewhere in the file. The table can enforce no more than
that without a second, redundant declaration in the suite source, which would
recreate the two-places problem.

## Evidence

Red: `python3 tests/lanes/run.py --build-dir build/he7.26/red --baseline`
against the pre-change tree, `lane fixtures: 8/18 passed`. The exact ipc merge
shape (`TSAN_SUITES := os base ipc`) was in fact caught there, but only because
the threaded-name rule covers `work_test.c`. The same shape that drops only
`trace_test.c` passed (`check-tsan: 4 threaded suites passed`), and so did the
misspelled `TSAN_SUITE +=`. Green after the change: `18/18 passed`.

The sorted lane lists printed by

    make --no-print-directory --eval 'print-lanes: ; @$(foreach v,$(LANE_VARS),echo "$(v): $(sort $($(v)))";)' \
        LANE_VARS='LAYER_SUITES SAN_SUITES LAW2_SUITES TSAN_SUITES FUZZ_SUITES' print-lanes

are byte-identical before and after (`diff` empty).
