/* Pure snapshot test; no context, display, or fabricated GPU completion. */
#include "gl/gl.h"
#include <stdio.h>
#define render_gl_backend scroll_test_factory
#define gl_present_complete scroll_test_present_complete
#define gl_completion_status scroll_test_completion_status
#define gl_buffer_mode scroll_test_buffer_mode
#define gl_device_name scroll_test_device_name
#define gl_displayed_msc scroll_test_displayed_msc
#define gl_read_pixels scroll_test_read_pixels
#define gl_cells_acquire scroll_test_cells_acquire
#define gl_cells_submit scroll_test_cells_submit
#include "../src/gl/gl.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"gl_scroll_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
static GLint region_destination[4];
static void APIENTRY record_bind(GLenum target, GLuint buffer)
{ (void)target; (void)buffer; }
static void APIENTRY record_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
    GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
    (void)sx0; (void)sy0; (void)sx1; (void)sy1; (void)mask; (void)filter;
    region_destination[0] = dx0; region_destination[1] = dy0;
    region_destination[2] = dx1; region_destination[3] = dy1;
}
int main(void)
{
    /* Same minimal surface/overscan geometry as raster_scroll_test. */
    render_cell cells[6]; gl_instance instances[6]; gl_strip strips[1];
    uint8_t seen[1] = {0};
    gl_state state = {.instances = instances, .strips = strips, .glyph_seen = seen};
    render_backend backend = {.state = &state};
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++)
        cells[i] = (render_cell){.atlas_slot = RENDER_NO_SLOT, .bg = (uint32_t)i};
    uint64_t dirty = 0; render_grid grid;
    CHECK(render_grid_init(&grid, (render_dims){2,2,1,2}, cells, 6, &dirty, 1) == 0);
    grid.scroll = (render_scroll){-128,1,1,1};
    CHECK(render_frame_begin(&grid,1) == 0 && render_mark_full(&grid) == 0 && render_grid_validate(&grid) == 0);
    render_strip full = {0,2};
    edit_malloc_guard_begin();
    int rc = gl_submit(&backend, &grid, &full, 1);
    size_t allocations = edit_malloc_guard_end();
    CHECK(rc == 0 && (!edit_malloc_guard_active() || allocations == 0));
    CHECK(state.scroll.origin_y_q8 == -128 && state.scroll.first_row == 1 && state.scroll.cols == 1);
    CHECK(state.instance_count == sizeof cells / sizeof cells[0] && state.strips[0].rows.row_count == grid.dims.rows + 1u);
    memset(cells, 0, sizeof cells); grid.scroll = (render_scroll){0};
    CHECK(instances[4].color[1] == 4 && state.scroll.origin_y_q8 == -128);
    uint32_t ink[] = {0x112233, 0x445566};
    state.region = (render_pixels){.pixels = ink, .capacity = 2, .stride = 1,
        .x = 1, .y = 2, .width = 1, .height = 2};
    state.gBindFramebuffer = record_bind; state.gBlitFramebuffer = record_blit;
    gl_blit_region(&state, 4);
    CHECK(region_destination[0] == 1 && region_destination[1] == 0 &&
        region_destination[2] == 2 && region_destination[3] == 2);
    CHECK(state.scroll.origin_y_q8 == -128 && state.region.y == 2);
    puts("gl_scroll_test: immutable Q8 plan/overscan snapshot, fixed surface-space pixel region PASS");
    return 0;
}
