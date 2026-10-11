#include "config/config.h"
#include "base/base.h"
#include <string.h>
static int visit(void *ctx, const char *path, const config_value *v)
{
    (void)ctx; EDIT_ASSERT(strlen(path) < CONFIG_NAME_CAP);
    if (v->type == CONFIG_STRING) EDIT_ASSERT(strlen(v->string) < CONFIG_TEXT_CAP);
    if (v->type == CONFIG_STRINGS) {
        EDIT_ASSERT(v->count <= CONFIG_BINDING_CAP);
        for (size_t i = 0; i < v->count; i++) EDIT_ASSERT(strlen(v->strings[i]) < CONFIG_TEXT_CAP);
    }
    return CONFIG_OK;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    config_settings s, before; memset(&s, 0xa5, sizeof s); before = s; config_error error;
    int rc = config_parse(data, size, &s, &error);
    EDIT_ASSERT(rc == error.code && error.offset <= size);
    if (rc) EDIT_ASSERT(!memcmp(&s, &before, sizeof s));
    else {
        EDIT_ASSERT(s.binding_count <= CONFIG_BINDING_CAP);
        keys_state state = {0}; const keys_binding *binding;
        (void)config_keys_lookup(&s, &state, size ? data[0] : 0, 0, &binding);
    }
    rc = config_toml_parse(data, size, visit, NULL, &error);
    EDIT_ASSERT(rc == error.code && error.offset <= size);
    return 0;
}
