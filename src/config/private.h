#ifndef EDIT_CONFIG_PRIVATE_H
#define EDIT_CONFIG_PRIVATE_H
#include "config/config.h"
#include "work/work.h"
/* Same parser, with cooperative cancellation for worker-owned source text. */
int config_toml_parse_worker(const uint8_t *, size_t, config_visit_fn, void *,
                             config_error *, const work_ctx *);
int config_parse_worker(const uint8_t *, size_t, config_settings *, config_error *, const work_ctx *);
#endif
