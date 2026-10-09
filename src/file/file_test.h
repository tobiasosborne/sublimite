/* Internal regression self-checks; no application API. */
#ifndef EDITOR_FILE_TEST_H
#define EDITOR_FILE_TEST_H
/* Bit 0: unpublished reservation preserves foreign pages.
 * Bit 1: retirement/reuse waits for pinned recovery and preserves neighbours. */
int file_test_guard_registry(void);
#endif
