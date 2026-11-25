/* check-layers L3: exercise the shipped factory through the UI front door. */
#include "render_fixture.h"

static bool public_null_progress(void)
{
    render_fixture f;
    rf_init(&f);
    ASSERT(base_arena_init(&f.arena, (RF_ROWS + 1u) * os_page_size()) == 0);
    ASSERT(os_wake_init(&f.wake) == OS_OK);
    ASSERT(render_open(&f.arena, &f.setup, &f.r) == RENDER_OK);
    ASSERT(render_bind(f.r, &f.binding, &f.sheet, NULL) == RENDER_OK);

    render_frame_id first, second;
    rf_offer(&f, RENDER_FULL);
    ASSERT(rf_submit(&f, &first, NULL) == RENDER_OK);
    bool progressed = rf_finish_public(&f);
    if (progressed) {
        rf_offer(&f, RENDER_PATCH);
        ASSERT(rf_submit(&f, &second, NULL) == RENDER_OK);
        progressed = second.epoch == first.epoch && second.frame == first.frame + 1u &&
            rf_finish_public(&f);
    }
    ASSERT(render_close(f.r) == RENDER_OK);
    os_wake_close(&f.wake);
    base_arena_free(&f.arena);
    return progressed;
}

int main(void)
{
    return public_null_progress() ? 0 : 1;
}
