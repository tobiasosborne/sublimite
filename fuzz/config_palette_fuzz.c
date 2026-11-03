/* edit-b6ai.2: Omarchy colors.toml parse/map. Any input yields a palette:
 * the mapped one, or a built-in table on failure; never a crash. */
#include "config/config.h"
#include "base/base.h"
#include <string.h>
static bool builtin(const config_palette *p)
{
    config_palette b; config_palette_builtin(p->mode, &b);
    return !memcmp(&b, p, sizeof b);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    for (int marker = 0; marker < 2; marker++) {
        config_palette p, q; config_error error, again;
        memset(&p, 0xa5, sizeof p); memset(&q, 0x5a, sizeof q);
        config_theme fallback = size && (data[0] & 1u) ? CONFIG_LIGHT : CONFIG_DARK;
        int rc = config_palette_parse(data, size, marker != 0, fallback, &p, &error);
        EDIT_ASSERT(rc == error.code && error.offset <= size);
        EDIT_ASSERT(p.mode == CONFIG_DARK || p.mode == CONFIG_LIGHT);
        if (rc) EDIT_ASSERT(builtin(&p));
        for (size_t i = 0; i < CONFIG_THEME_TOKENS; i++) EDIT_ASSERT(p.rgb[i] <= 0xffffffu);
        /* Deterministic: no hidden state between calls. */
        EDIT_ASSERT(config_palette_parse(data, size, marker != 0, fallback, &q, &again) == rc);
        EDIT_ASSERT(!memcmp(&p, &q, sizeof p) && again.code == error.code && again.offset == error.offset);
    }
    return 0;
}
