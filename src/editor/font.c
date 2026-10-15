#include "editor/private.h"
#include <stdio.h>
#include <string.h>

int editor_font_request_chain(editor *e, const char *family, const char *font_file,
                              const char *fallbacks)
{
    if (!e) return EDITOR_ERR_ARG;
    editor_font_size *f = &e->font;
    if (f->requested_generation != f->applied_generation) return EDITOR_MORE;
    if (f->requested_generation == UINT32_MAX) return EDITOR_ERR_CAPACITY;
    if (fallbacks && strlen(fallbacks) >= sizeof f->fallbacks) return EDITOR_ERR_ARG;
    if (font_selection_parse(&f->selection, family, font_file)) return EDITOR_ERR_ARG;
    if (fallbacks) memcpy(f->fallbacks, fallbacks, strlen(fallbacks) + 1u);
    f->requested_generation++;
    return editor_font_poll(e);
}
int editor_font_request(editor *e, const char *family, const char *font_file)
{
    return editor_font_request_chain(e, family, font_file, NULL);
}

/* Invalid environment syntax is rejected during setup, but its diagnostic
 * still belongs to a worker. The atomic flag survives pool shutdown. */
static void bad_config_job(work_ctx *ctx)
{
    _Atomic bool *logged = ctx->arg;
    if (!work_should_stop(ctx) && !atomic_exchange(logged, true))
        fprintf(stderr, "sublimite: invalid font selection; using embedded DejaVu Sans Mono\n");
}
void editor_font_bad_config(editor *e)
{
    (void)work_submit(&e->pool, (work_job){bad_config_job, &e->font_logged, 0, WORK_BULK});
}
