/* Test-only entry points exercise the OS fault service, never install one. */
#include "file/file_test.h"
#include <stdlib.h>
#include <string.h>

int file_test_recover(const void *address)
{
    /* A logical single-byte subview identifies the byte to fault. Recovery
     * uses the registered full view, retained by the test's reader. */
    os_map byte = {.addr = address, .len = 1};
    return os_fault_test_trigger(&byte, 0) == OS_OK;
}

int file_test_guard_registry(void)
{
    char path[OS_PATH_MAX];
    os_dir dir = OS_DIR_INIT;
    os_file source = OS_FILE_INIT;
    os_map first = {0}, neighbour = {0};
    int slot = -1, other = -1, result = 0;
    size_t page = os_page_size(), done = 0;
    uint8_t *bytes = malloc(page);
    if (!bytes) return 0;
    memset(bytes, 'Q', page);
    if (os_temp_dir(path, sizeof path) != OS_OK || os_dir_open(&dir, path) != OS_OK ||
        os_file_open_anon(&source, &dir) != OS_OK ||
        os_file_write(&source, bytes, page, &done) != OS_OK || done != page ||
        os_map_file(&first, &source, 0, page) != OS_OK ||
        os_map_file(&neighbour, &source, 0, page) != OS_OK) goto end;
    os_map invalid = {0}; int unused = -1;
    if (os_fault_register(&invalid, &unused) == OS_E_INVAL &&
        ((const uint8_t *)first.addr)[0] == 'Q' && ((const uint8_t *)neighbour.addr)[0] == 'Q') result |= 1;
    if (os_fault_register(&first, &slot) != OS_OK || os_fault_register(&neighbour, &other) != OS_OK) goto end;
    if (os_fault_test_trigger(&first, 0) != OS_OK) goto end;
    os_fault_retire(slot); slot = -1;
    os_unmap(&first);
    if (os_map_file(&first, &source, 0, page) != OS_OK) goto end;
    if (os_fault_register(&first, &slot) == OS_OK && os_fault_epoch(slot) == 0 &&
        os_fault_epoch(other) == 0 && ((const uint8_t *)neighbour.addr)[0] == 'Q') result |= 2;
end:
    os_fault_retire(slot); os_fault_retire(other);
    if (first.addr) os_unmap(&first);
    if (neighbour.addr) os_unmap(&neighbour);
    os_file_close(&source); os_dir_close(&dir); free(bytes);
    return result;
}
