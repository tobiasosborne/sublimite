#include "../tests/support/edit_fixture.h"

#include <string.h>

/* One byte per command and at most two inserted bytes per command.
 * Bound the whole-state oracle to 32 groups, enough for repeated branching
 * and replay, independently of libFuzzer's input length. */
enum { MODEL_STEPS = 32, MODEL_SEED = 4, MODEL_TEXT = MODEL_SEED + 2 * MODEL_STEPS };

typedef struct flat_state {
    uint8_t text[MODEL_TEXT];
    uint64_t source[MODEL_TEXT];
    size_t len;
    edit_selection select;
    uint64_t identity;
} flat_state;

typedef struct flat_group { flat_state before, after; } flat_group;

static void flat_splice(flat_state *s, edit_splice splice, uint64_t *add)
{
    size_t off = (size_t)splice.off, remove = (size_t)splice.remove;
    size_t tail = s->len - off - remove;
    memmove(s->text + off + splice.length, s->text + off + remove, tail);
    memmove(s->source + off + splice.length, s->source + off + remove, tail * sizeof *s->source);
    for (size_t i = 0; i < splice.length; i++) {
        s->text[off + i] = splice.bytes[i];
        s->source[off + i] = PIECE_SPAN_ADD | (*add)++;
    }
    s->len = s->len - remove + splice.length;
}

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size)
{
    const uint8_t seed[] = "seed";
    flat_state flat = {.text = "seed", .len = MODEL_SEED, .identity = 1};
    for (size_t i = 0; i < flat.len; i++) flat.source[i] = i;
    flat_group history[MODEL_STEPS];
    size_t applied = 0, groups = 0;
    uint64_t revision = 0, identity = 1, saved = 1, cutoff = 0, add = 0;
    uint64_t saved_id = 1, cutoff_id = 0;
    uint64_t time = 0, last_time = 0, last_off = 0;
    bool typing = false;
    edit_fixture f;
    /* Worst step: one BEGIN/COMMIT pair and at most two splices, each
     * with a delete and one short literal. */
    journal *j = ej_create(MODEL_STEPS * 6 * (JOURNAL_DELETE_BYTES + 2),
                           MODEL_STEPS * 6);
    ef_open(&f, seed, MODEL_SEED, undo_default_cap_bytes(), j);
    for (size_t i = 0; i < size && i < MODEL_STEPS; i++) {
        uint8_t arg = input[i], byte = (uint8_t)('a' + arg % 26);
        unsigned op = arg >> 4;
        size_t pos = arg % (flat.len + 1);
        bool changed = false, replay = false;
        edit_result r = EDIT_OK;
        if (op <= 6) {
            flat_state next = flat;
            edit_splice plan[2] = {{pos, 0, &byte, 1}, {0, 0, &byte, 1}};
            size_t n = 1;
            bool join = false;
            switch (op) {
            case 0:
            case 1:
            case 2:
                /* The high timestamp step is one nanosecond past v1's
                 * 300 ms threshold. Adjacent keys exercise real joins. */
                time += op == 2 ? UINT64_C(300000001) : 1;
                plan[0].off = flat.select.cursor;
                join = typing && applied == groups && groups &&
                       plan[0].off == last_off + 1 &&
                       time - last_time <= UINT64_C(300000000);
                typing = true;
                last_off = plan[0].off;
                last_time = time;
                r = edit_replace(f.edit, EDIT_TYPE, plan[0], time);
                break;
            case 3:
            case 4:
                /* Use an explicit plan so delete bursts are tested by the
                 * directed cases. Delete one byte when the text is nonempty. */
                if (flat.len) plan[0] = (edit_splice){pos % flat.len, 1, NULL, 0};
                else plan[0] = (edit_splice){0};
                goto apply_plan;
            case 5:
                /* The second splice erases the first insertion. Redo still
                 * has to emit those source coordinates from pinned ADD. */
                plan[1] = (edit_splice){pos, 1, NULL, 0};
                n = 2;
                goto apply_plan;
            case 6:
                if (flat.len && pos < flat.len) plan[0].remove = 1;
                n = 2;
apply_plan:
                typing = false;
                r = edit_apply_plan(f.edit, plan, n, (edit_selection){0});
                break;
            }
            for (size_t k = 0; k < n; k++) {
                changed |= plan[k].remove != 0 || plan[k].length != 0;
                flat_splice(&next, plan[k], &add);
            }
            uint64_t cursor = typing ? plan[0].off + plan[0].length : 0;
            next.select = (edit_selection){cursor, cursor};
            if (changed) {
                next.identity = ++identity;
                if (join) history[applied - 1].after = next;
                else history[applied++] = (flat_group){flat, next};
                groups = applied;
            }
            flat = next;
        } else if (op <= 10) {
            typing = false;
            bool reverse = op <= 8;
            r = edit_replay(f.edit, reverse ? UNDO_REPLAY_UNDO : UNDO_REPLAY_REDO,
                            arg & 1 ? 0 : OS_DEADLINE_NEVER);
            if (reverse && applied) {
                flat = history[--applied].before;
                changed = replay = true;
            } else if (!reverse && applied < groups) {
                flat = history[applied++].after;
                changed = replay = true;
            }
        } else if (op == 11 && !cutoff) {
            typing = false;
            edit_save_boundary(f.edit, EDIT_SAVE_CAPTURED);
            edit_boundary b;
            edit_read(f.edit, &b);
            cutoff = flat.identity;
            cutoff_id = b.content_id;
        } else if ((op == 12 || op == 13) && cutoff) {
            edit_save_boundary(f.edit, op == 12 ? EDIT_SAVE_INSTALLED : EDIT_SAVE_RELEASED);
            if (op == 12) {
                saved = cutoff;
                saved_id = cutoff_id;
            }
            cutoff = cutoff_id = 0;
        } else if (op == 14) {
            edit_save_boundary(f.edit, EDIT_SAVE_FORGOTTEN);
            saved = saved_id = 0;
        } else {
            edit_selection motion = {pos, pos};
            if (flat.select.cursor != pos || flat.select.anchor != pos) typing = false;
            flat.select = motion;
            r = edit_select(f.edit, (edit_selection_state){.active = motion});
        }

        ECHECK(ef_finish(f.edit, r) == EDIT_OK);
        if (changed) {
            ef_submit(f.edit);
            revision++;
        }
        edit_boundary actual;
        ECHECK(edit_read(f.edit, &actual) == EDIT_IDLE);
        ECHECK(actual.revision == revision);
        ECHECK(actual.selection.active.cursor == flat.select.cursor &&
               actual.selection.active.anchor == flat.select.anchor);
        ECHECK(actual.content_id != 0);
        /* A freshly captured cutoff owns the actual identity of this
         * boundary, even when this visit was to an unowned replay ID. */
        if (changed || replay) {
            ECHECK((actual.content_id == saved_id) == (flat.identity == saved));
            ECHECK((actual.content_id == cutoff_id) == (flat.identity == cutoff));
        }
        ef_text(f.edit, flat.text, flat.len);
        uint8_t recovered[MODEL_TEXT];
        ECHECK(ej_read(j, EDIT_TEST_OWNER, seed, MODEL_SEED, recovered, sizeof recovered) == flat.len);
        ECHECK(memcmp(flat.text, recovered, flat.len) == 0 && ej_info(j).committed == revision);
    }
    if (cutoff) edit_save_boundary(f.edit, EDIT_SAVE_RELEASED);
    ef_close(&f);
    return 0;
}
