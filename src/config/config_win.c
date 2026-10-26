#if defined(_WIN32)
#include "config/private.h"
#include <malloc.h>
#include <string.h>
int config_path(char *out, size_t cap)
{
    if (!out || !cap) return CONFIG_ERR_ARG;
    char root[OS_PATH_MAX];
    os_err rc = os_app_dir(OS_APP_CONFIG, root, sizeof root);
    if (rc) return rc == OS_E_RANGE || rc == OS_E_NAMETOOLONG ? CONFIG_ERR_LIMIT : CONFIG_ERR_ARG;
    const char *suffix = "/config.toml";
    size_t n = strlen(root);
    if (n >= cap || strlen(suffix) >= cap - n) return CONFIG_ERR_LIMIT;
    memcpy(out, root, n); strcpy(out + n, suffix); return CONFIG_OK;
}
void *config_runtime_alloc(size_t alignment, size_t size) { return _aligned_malloc(size, alignment); }
void config_runtime_free(void *p) { _aligned_free(p); }
#endif
