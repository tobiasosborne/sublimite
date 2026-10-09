/* libFuzzer: EOL / BOM prefix scanner vs a byte-at-a-time model. */
#include "file/file.h"
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    file_prefix_info i;
    uint64_t lf = 0, crlf = 0, cr = 0;
    file_prefix_scan(data, size, &i);
    for (size_t k = 0; k < size; k++) {
        if (data[k] == '\n') { if (k > 0 && data[k - 1] == '\r') crlf++; else lf++; }
        else if (data[k] == '\r' && (k + 1 == size || data[k + 1] != '\n')) cr++;
    }
    if (i.lf != lf || i.crlf != crlf || i.cr != cr) __builtin_trap();
    file_eol want = (lf + crlf) == 0 ? FILE_EOL_NONE : crlf == 0 ? FILE_EOL_LF : lf == 0 ? FILE_EOL_CRLF : FILE_EOL_MIXED;
    if (i.kind != want) __builtin_trap();
    if (i.dominant != (crlf > lf ? FILE_EOL_CRLF : FILE_EOL_LF)) __builtin_trap();
    if (i.has_bom != (size >= 3 && memcmp(data, "\xEF\xBB\xBF", 3) == 0)) __builtin_trap();
    return 0;
}
