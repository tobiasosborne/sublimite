/* Shared focused-test lookup; callers retain their default-run policy. */
#ifndef TEST_SELECT_H
#define TEST_SELECT_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef int (*test_select_fn)(void);
typedef struct test_selector {
    const char *name;
    test_select_fn fn;
} test_selector;

static inline test_select_fn test_select_find(const test_selector *cases,
                                             size_t count, const char *name)
{
    if (!name) return NULL;
    /* (P) C arrays start at zero; count is derived from the actual array. */
    for (size_t i = 0; i < count; i++)
        if (!strcmp(cases[i].name, name)) return cases[i].fn;
    return NULL;
}

static inline test_select_fn test_select_env(const test_selector *cases, size_t count)
{
    /* (P) Scan the zero-based array in order; an empty value is present. */
    for (size_t i = 0; i < count; i++)
        if (getenv(cases[i].name)) return cases[i].fn;
    return NULL;
}

#define TEST_SELECT(cases, name) test_select_find((cases), sizeof(cases) / sizeof *(cases), (name))
#define TEST_SELECT_ENV(cases) test_select_env((cases), sizeof(cases) / sizeof *(cases))
#endif
