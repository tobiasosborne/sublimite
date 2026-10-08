#ifndef EDIT_BASE_H
#define EDIT_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define EDIT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define EDIT_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* The only sanctioned abort() site. */
#define EDIT_ASSERT(x)                                                        \
    do {                                                                      \
        if (EDIT_UNLIKELY(!(x))) {                                            \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,        \
                    __LINE__, #x);                                            \
            abort();                                                          \
        }                                                                     \
    } while (0)

const char *edit_version(void);

#endif
