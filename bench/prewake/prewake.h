#ifndef PREWAKE_H
#define PREWAKE_H
#include "x11/plat.h"
#include <stdint.h>
typedef struct prewake_ops {
    void *ctx;
    int (*warm)(void *);
    int (*spin)(void *, uint64_t);
} prewake_ops;
typedef struct prewake_state { uint64_t last_activity_ns; } prewake_state;
int prewake_hint(prewake_state *s, const plat_event *event, uint64_t now,
                 const prewake_ops *ops);
void prewake_activity(prewake_state *s, uint64_t now);
#endif
