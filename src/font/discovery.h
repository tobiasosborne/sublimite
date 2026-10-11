#ifndef EDIT_FONT_DISCOVERY_H
#define EDIT_FONT_DISCOVERY_H
#include "font/font.h"
/* Worker-only. Opaque fontconfig CPU work runs in an exec'd, cancellable child.
 * False means cancelled; unavailable CLI gracefully gives empty fallbacks. */
int font_fallback_isolated(font_fallback *owner, font_fallback_result *out, work_ctx *ctx);
/* Resolve exactly the missing scalar, rather than a script representative.
 * Coverage is verified against fontconfig's charset before accepting a path. */
int font_fallback_codepoint(font_fallback *owner, work_ctx *ctx, uint32_t cp,
                            char *path, uint32_t *index);
#endif
