#include "render/render.h"

#include <string.h>

#include "journal/journal.h"

size_t render_smoke(void)
{
    uint8_t payload[sizeof(uint64_t) + sizeof(EDIT_APP_ID)];
    size_t used;
    const char *id = os_app_id();
    const journal_insert insert = {0, (const uint8_t *)id, strlen(id)};
    if (journal_insert_encode(payload, sizeof payload, &used, &insert) != JOURNAL_CODEC_OK)
        return 0;
    return used - sizeof(uint64_t);
}
