#include "base/base.h"
#include <string.h>

int main(void) {
    const char *v = edit_version();
    EDIT_ASSERT(v != NULL);
    EDIT_ASSERT(strlen(v) > 0);
    EDIT_ASSERT(EDIT_LIKELY(1 + 1 == 2));
    puts(v);
    return 0;
}
