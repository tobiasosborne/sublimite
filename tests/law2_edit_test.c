#include "base/base.h"
#include "support/edit_fixture.h"

#include <string.h>

int main(void)
{
    /* Each row uses the full production tree/log/edit path. The fixture
     * has two initial bytes, two replacement splices and one submit slot. */
    edit_fixture f;
    const uint8_t seed[] = "ab";
    journal *j = ej_create(JOURNAL_PAGE_BYTES, JOURNAL_PAGE_BYTES / JOURNAL_HEADER_BYTES);
    ef_open(&f, seed, sizeof seed - 1, undo_default_cap_bytes(), j);
    ECHECK(base_malloc_guard_active());
    const uint8_t key = 'x';
    const edit_splice plan[] = {{0, 1, &key, 1}, {1, 0, &key, 1}};
    base_malloc_guard_begin();
    ECHECK(edit_select(f.edit, (edit_selection_state){.active = {0, 0}}) == EDIT_OK);
    ECHECK(ef_finish(f.edit, edit_replace(f.edit, EDIT_TYPE, (edit_splice){0, 0, &key, 1}, 0)) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ef_finish(f.edit, edit_replace(f.edit, EDIT_DELETE_FORWARD, (edit_splice){0, 1, NULL, 0}, 0)) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ef_finish(f.edit, edit_replace(f.edit, EDIT_BACKSPACE, (edit_splice){0, 1, NULL, 0}, 0)) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ef_finish(f.edit, edit_apply_plan(f.edit, plan, sizeof plan / sizeof *plan,
                                            (edit_selection){2, 2})) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ef_finish(f.edit, edit_replay(f.edit, UNDO_REPLAY_UNDO, 0)) == EDIT_OK);
    ef_submit(f.edit);
    ECHECK(ef_finish(f.edit, edit_replay(f.edit, UNDO_REPLAY_REDO, 0)) == EDIT_OK);
    ef_submit(f.edit);
    size_t count = base_malloc_guard_end();
    printf("law2_edit_test: type forward-delete backspace compound undo redo active=%d count=%zu\n",
           (int)base_malloc_guard_active(), count);
    ef_text(f.edit, (const uint8_t *)"xx", 2);
    ef_close(&f);
    return base_malloc_guard_active() && count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
