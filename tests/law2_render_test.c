#include "render_fixture.h"
#include <stdio.h>

int main(void)
{
    render_fixture f;
    rf_init(&f);
    ASSERT(base_arena_init(&f.arena, (RF_ROWS + 1u) * os_page_size()) == 0);
    ASSERT(os_wake_init(&f.wake) == OS_OK);
    ASSERT(render_open(&f.arena, &f.setup, &f.r) == RENDER_OK);
    ASSERT(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);
    ASSERT(base_malloc_guard_active());

    rf_notify(&f, 0, 63);
    rf_wide(&f, 0, f.tiles[0].image);
    rf_offer(&f, RENDER_FULL);
    f.offer.tile_descriptors = 1; /* one owed, padded, double-cell image */
    f.offer.tile_bytes = RF_TILE;
    f.offer.region = RENDER_REPLACE;
    render_frame_id id;
    base_malloc_guard_begin();
    render_rc rc = rf_submit(&f, &id, NULL);
    size_t allocations = base_malloc_guard_end();
    ASSERT(rc == RENDER_OK && allocations == 0);
    ASSERT(rf_finish_public(&f));

    rf_offer(&f, RENDER_PATCH);
    f.offer.run_count = 1;
    f.runs[0] = (render_strip){0, 1};
    base_malloc_guard_begin();
    rc = rf_submit(&f, &id, NULL);
    allocations = base_malloc_guard_end();
    ASSERT(rc == RENDER_OK && allocations == 0);
    ASSERT(rf_finish_public(&f));

    ASSERT(render_close(f.r) == RENDER_OK);
    os_wake_close(&f.wake);
    base_arena_free(&f.arena);
    puts("render Law 2: live guard, zero allocations for full tile/region and resident row submits");
    return 0;
}
