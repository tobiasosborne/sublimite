#include "config/private.h"
#include <stdio.h>
#include <string.h>
#define T(x) do { if (!(x)) { fprintf(stderr, "config_worker_test: FAIL %s\n", #x); return 1; } } while (0)
typedef struct cancel_probe { work_slot *slot; size_t calls; } cancel_probe;
static int cancel(void *ctx, const char *path, const config_value *v)
{
    cancel_probe *p = ctx; (void)path; (void)v; p->calls++;
    atomic_fetch_add(&p->slot->epoch, 1u); return CONFIG_OK;
}
int main(void)
{
    /* (P) cancellation is an epoch mismatch. A source gap of VIEW_SCAN_BOUND
     * bytes exercises the parser's inherited byte-work polling interval. */
    work_slot slot = {0}; atomic_init(&slot.epoch, 1u);
    work_ctx ctx = {.slot = &slot, .epoch = 0};
    config_settings settings, before; config_defaults(&settings); before = settings;
    const char text[] = "theme='light'\n"; config_error error;
    T(config_parse_worker((const uint8_t *)text, sizeof text - 1u, &settings, &error, &ctx) == CONFIG_ERR_BUSY);
    T(!memcmp(&settings, &before, sizeof settings));
    T(config_parse_worker(NULL, 0, &settings, &error, &ctx) == CONFIG_ERR_BUSY);
    T(!memcmp(&settings, &before, sizeof settings));
    char source[sizeof("first=true\n") + VIEW_SCAN_BOUND + sizeof("next=true\n")];
    size_t head = sizeof("first=true\n") - 1u; memcpy(source, "first=true\n", head);
    memset(source + head, ' ', VIEW_SCAN_BOUND);
    memcpy(source + head + VIEW_SCAN_BOUND, "next=true\n", sizeof("next=true\n") - 1u);
    size_t length = head + VIEW_SCAN_BOUND + sizeof("next=true\n") - 1u;
    cancel_probe p = {.slot = &slot}; ctx.epoch = atomic_load(&slot.epoch);
    T(config_toml_parse_worker((const uint8_t *)source, length, cancel, &p, &error, &ctx) == CONFIG_ERR_BUSY);
    T(p.calls == 1u); /* (P) cancellation from first callback prevents next */
    puts("config_worker_test: cancelled parse preserves settings and polls inside untrusted text passed");
    return 0;
}
