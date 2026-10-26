/* Internal regression self-checks; no application API. */
#ifndef EDITOR_FILE_TEST_H
#define EDITOR_FILE_TEST_H
#include "file/file.h"
/* Public OS registry checks: bit 0 rejects an invalid view without touching
 * neighbours; bit 1 retires/reuses a slot with a fresh view and clean epoch.
 * Private reader-pin concurrency is covered by the OS conformance suite. */
int file_test_guard_registry(void);
/* Exercise the production OS recovery service on a retained mapping. */
int file_test_recover(const void *address);
/* Per-open, worker-side hook after identity validation, immediately before map. */
int file_test_open_begin(work_pool *, const char *, const file_open_opts *,
                         void (*before_map)(void *), void *, file **);
#endif
