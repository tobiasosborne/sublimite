#ifndef EDIT_TEST_H
#define EDIT_TEST_H

#include "edit/edit.h"

/* Only EDIT_TEST_HOOKS builds export these observations. */
size_t edit_test_scratch_high(const edit_buffer *b);
undo_stats edit_test_undo_stats(const edit_buffer *b);

#endif
