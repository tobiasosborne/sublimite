/* Internal regression self-checks; no application API. */
#ifndef EDITOR_FILE_TEST_H
#define EDITOR_FILE_TEST_H
/* Bit 0: unpublished reservation preserves foreign pages.
 * Bit 1: retirement/reuse waits for pinned recovery and preserves neighbours. */
int file_test_guard_registry(void);
/* Exercise the production SIGBUS recovery service on a retained mapping. */
int file_test_recover(const void *address);
#endif
