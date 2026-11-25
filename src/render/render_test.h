#ifndef RENDER_TEST_H
#define RENDER_TEST_H

/* Implemented only by the render test-hooks objects. Inspect between
 * owner's calls. Samples are computed on request from the real scene. */
#include "render/render_driver.h"

typedef struct render_test_owner {
    render_driver driver;
    void *state;
    render_driver_port *port;
} render_test_owner;

typedef struct render_test_view {
    render_scene_view scene;
    render_binding binding;
    size_t scan_cells, drawn_rows;
    uint64_t scan_ns;
} render_test_view;

render_test_owner render_test_owner_get(render *);
render_test_view render_test_inspect(render *);
render_driver render_test_manual_null(void);
/* Test-only gate. Set while idle, before the next command is published. */
void render_test_null_on_take(void *null_state, void (*fn)(void *), void *ctx);
const uint8_t *render_test_atlas(void *null_state);

typedef struct render_sample {
    render_cell cell;
    const uint8_t *coverage;
    uint32_t width, height, x_offset;
} render_sample;

static inline render_sample render_test_sample(render_test_view view, const uint8_t *atlas, size_t index)
{
    render_sample sample = {.cell = view.scene.cells[index]};
    size_t slot;
    if (render_scene_ink(&view.scene, &view.binding, index, &slot, &sample.x_offset)) {
        size_t stride = (size_t)view.binding.cell_width * view.binding.cell_height * RENDER_TILE_MAX_SPAN;
        sample.coverage = atlas + slot * stride;
        sample.width = view.scene.installed[slot].width;
        sample.height = view.scene.installed[slot].height;
    }
    return sample;
}
#endif
