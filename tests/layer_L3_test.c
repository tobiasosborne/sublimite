/* check-layers L3: linked --whole-archive against lower layers only. */
#include "render_fixture.h"

int main(void)
{
    render_fixture f;
    rf_init(&f);
    if (base_arena_init(&f.arena, (RF_ROWS + 1u) * os_page_size()) != 0) return 1;
    if (os_wake_init(&f.wake) != OS_OK) return 1;
    if (render_open(&f.arena, &f.setup, &f.r) != RENDER_OK ||
        render_bind(f.r, &f.binding, &f.sheet, NULL) != RENDER_OK ||
        render_close(f.r) != RENDER_OK) return 1;
    os_wake_close(&f.wake);
    base_arena_free(&f.arena);
    return 0;
}
