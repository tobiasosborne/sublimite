/* edit-cxho.24-r3: cell geometry is the design specification (P: the brief's
 * two-cell insets, one-cell right margin and one chrome spacer row). */
#include "editor/private.h"
#include "editor/theme.h"
#include "trace/trace.h"
#include "x11/private.h"
#include "raster/raster.h"
#include <xcb/xcb.h>
#include <stdlib.h>
#include <unistd.h>
#include "../vendor/xkbcommon/xkbcommon-keysyms.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;
#define T(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
static void settle(editor *e)
{
    uint64_t deadline = trace_now_ns() + EDITOR_IDLE_NS; /* existing hot-exit suite watchdog */
    do {
        T(editor_step(e, 0) >= 0);
        editor_stats s = editor_get_stats(e);
        if (!s.pending && !s.render_active) return;
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    } while (trace_now_ns() < deadline);
    T(false);
}
static void stroke(editor *e, uint32_t sym, uint16_t mods)
{
    plat_event ev = {.kind = PLAT_EV_KEY, .keysym = sym, .mods = mods, .press = true};
    T(editor_inject(e, &ev) == 0); settle(e);
}
static bool text_at(editor *e, uint32_t r, uint32_t c, const char *s)
{
    for (; *s; s++, c++) if (c >= e->grid.dims.cols || e->grid.cells[(size_t)r * e->grid.dims.cols + c].glyph_index != (uint8_t)*s) return false;
    return true;
}
static bool row_has(editor *e, uint32_t r, const char *s)
{
    for (uint32_t c = 0; c < e->grid.dims.cols; c++) if (text_at(e, r, c, s)) return true;
    return false;
}
static void static_design(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    const char initial[] = "alpha beta alpha\nsecond";
    editor_config cfg = {.initial = (const uint8_t *)initial, .initial_len = sizeof initial - 1u,
        .cols = 80, .rows = 12, .wrap_mode = -1}; /* (P) scene dimensions, ample space for the full panel */
    editor *e = NULL; T(editor_open(&e, &cfg, &backend) == 0); if (!e) return; settle(e);
    T(e->lay.gw == 5u); /* two inset + one digit + two gap */
    T(e->tab_rows == 2u);
    T(e->text_grid.dims.cols + e->map_cols + 1u == e->grid.dims.cols);
    T(text_at(e, e->tab_rows, 2, "1"));
    T(text_at(e, e->tab_rows, e->lay.gw, "alpha"));
    const tabs_tab *tab = tabs_active(&e->tabs);
    T(text_at(e, e->menu_rows, e->lay.gw, tab->title));
    for (uint32_t c = 0; c < e->grid.dims.cols; c++) {
        render_cell v = e->grid.cells[(size_t)(e->tab_rows - 1u) * e->grid.dims.cols + c];
        T(v.bg == e->layout_cfg.bg && (v.atlas_slot == RENDER_NO_SLOT || v.glyph_index == ' '));
    }
    T(editor_set_cursor(e, sizeof "alpha beta alpha\n" - 1u) == 0); settle(e);
    view_selection before = editor_view(e).selection;
    plat_event click = {.kind = PLAT_EV_BUTTON, .press = true, .code = 1,
        .x = (int32_t)(e->lay.gw * e->grid.dims.cell_w), .y = (int32_t)((e->tab_rows - 1u) * e->grid.dims.cell_h)};
    T(editor_inject(e, &click) == 0); settle(e);
    T(editor_view(e).selection.cursor == before.cursor && editor_view(e).selection.anchor == before.anchor);
    stroke(e, 'f', PLAT_MOD_CTRL);
    uint32_t first = e->grid.dims.rows - e->panel_rows;
    T(text_at(e, first, e->lay.gw, "Find")); T(!row_has(e, first, "Find:"));
    T(e->grid.cells[(size_t)first * e->grid.dims.cols + e->lay.gw].fg == e->layout_cfg.gutter_fg);
    T(row_has(e, first, "Aa ab .*"));
    for (uint32_t c = 0; c < e->grid.dims.cols; c++) T(e->grid.cells[(size_t)first * e->grid.dims.cols + c].bg != e->layout_cfg.bg);
    stroke(e, XKB_KEY_Escape, 0); stroke(e, 'p', PLAT_MOD_CTRL | PLAT_MOD_SHIFT);
    first = e->grid.dims.rows - e->panel_rows;
    T(text_at(e, first, e->lay.gw, "Command")); T(!row_has(e, first, "Command:"));
    stroke(e, XKB_KEY_Escape, 0); stroke(e, 'g', PLAT_MOD_CTRL);
    T(text_at(e, e->grid.dims.rows - 1u, e->lay.gw, "Line number"));
    T(!row_has(e, e->grid.dims.rows - 1u, "Go to line:"));
    editor_close(e);
}
static void motion(editor *e, uint32_t row, uint32_t col)
{
    plat_event ev = {.kind=PLAT_EV_MOTION, .x=(int32_t)(col * e->grid.dims.cell_w),
        .y=(int32_t)(row * e->grid.dims.cell_h)};
    T(editor_inject(e, &ev) == 0); settle(e);
}
typedef struct design_frames { editor *e; uint64_t rows; bool pixels; } design_frames;
static void submitted(void *ctx, const editor_frame *frame)
{
    design_frames *seen=ctx; (void)frame; if (!seen->e) return;
    seen->rows=0; seen->pixels=seen->e->grid.region.dirty;
    for (uint32_t r=0;r<seen->e->grid.dims.rows;r++) if (render_row_dirty(&seen->e->grid,r)) seen->rows|=UINT64_C(1)<<r;
}
static void moving_states(void)
{
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    design_frames seen={0};
    const char text[] = "alpha beta\nsecond\nthird";
    editor_config cfg = {.initial=(const uint8_t *)text, .initial_len=sizeof text-1u,
        .cols=80, .rows=12, .wrap_mode=-1, .hook_ctx=&seen, .on_submit=submitted}; /* same scene as the static checks */
    editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); if (!e) return; seen.e=e; settle(e);
    uint64_t id; T(editor_add_buffer(e,NULL,NULL,0,&id)==0); settle(e);
    T(editor_select_tab(e,0)==0); settle(e);
    uint32_t label=e->strip.first_col + e->strip.tab_cols + 1u;
    edit_malloc_guard_begin();
    motion(e,e->menu_rows,label);
    size_t allocations=edit_malloc_guard_end(); T(!edit_malloc_guard_active() || allocations==0);
    T(seen.rows==(UINT64_C(1)<<e->menu_rows) && !seen.pixels);
    T(e->grid.cells[(size_t)e->menu_rows*e->grid.dims.cols+label].fg==e->colors.tab_hover);
    uint64_t frames=backend.stats.submitted_frames;
    motion(e,e->menu_rows,label+1u); T(backend.stats.submitted_frames==frames);
    motion(e,e->tab_rows,e->lay.gw); T(e->pointer_shape==PLAT_CURSOR_TEXT);
    T(e->grid.cells[(size_t)e->menu_rows*e->grid.dims.cols+label].fg==e->colors.chrome_fg);
    stroke(e,'f',PLAT_MOD_CTRL);
    findui_bounds bounds=editor_panel_bounds(e);
    findui_geometry g=findui_get_geometry(&e->find,bounds);
    uint32_t first=e->grid.dims.rows-e->panel_rows;
    motion(e,first,g.toggles);
    T(e->pointer_shape==PLAT_CURSOR_DEFAULT);
    T(seen.rows==(UINT64_C(1)<<first) && !seen.pixels);
    T(e->grid.cells[(size_t)first*e->grid.dims.cols+g.toggles].bg==e->colors.hover);
    motion(e,first,bounds.first); T(e->pointer_shape==PLAT_CURSOR_TEXT);
    motion(e,first,g.toggles);
    plat_event leave={.kind=PLAT_EV_LEAVE}; T(editor_inject(e,&leave)==0); settle(e);
    T(e->hover.kind==EDITOR_HOVER_NONE && e->pointer_shape==PLAT_CURSOR_DEFAULT);
    T(e->grid.cells[(size_t)first*e->grid.dims.cols+g.toggles].bg==e->colors.panel);
    stroke(e,XKB_KEY_Escape,0); stroke(e,'p',PLAT_MOD_CTRL|PLAT_MOD_SHIFT);
    first=e->grid.dims.rows-e->panel_rows;
    motion(e,first+2u,e->lay.gw);
    T(e->grid.cells[(size_t)(first+2u)*e->grid.dims.cols+e->lay.gw].bg==e->colors.hover);
    T(e->grid.cells[(size_t)(first+1u)*e->grid.dims.cols+e->lay.gw].bg==e->layout_cfg.sel_bg);
    T(seen.rows==(UINT64_C(1)<<(first+2u)) && !seen.pixels);
    motion(e,first+1u,e->lay.gw);
    T(e->grid.cells[(size_t)(first+1u)*e->grid.dims.cols+e->lay.gw].bg==e->layout_cfg.sel_bg);
    stroke(e,XKB_KEY_Escape,0);
    /* Minimap hover changes only its retained pixel region. */
    uint64_t deadline=trace_now_ns()+EDITOR_IDLE_NS;
    while ((e->buffer->map_snapshot || editor_map_due(e)) && trace_now_ns()<deadline) settle(e);
    size_t sample=2u*e->grid.region.width+1u; /* interior wash, excluding the one-pixel perimeter */ uint32_t before=e->map_pixels[sample];
    motion(e,e->tab_rows,e->text_grid.dims.cols);
    T(seen.rows==0 && seen.pixels && e->map_pixels[sample]!=before);
    frames=backend.stats.submitted_frames;
    motion(e,e->tab_rows+1u,e->text_grid.dims.cols+1u); T(backend.stats.submitted_frames==frames);
    T(editor_inject(e,&leave)==0); settle(e); T(e->map_pixels[sample]==before);
    /* Unselected menu rows wash; selected rows retain their tint. */
    stroke(e,'f',PLAT_MOD_ALT);
    T(e->menu.open==0);
    motion(e,e->menu_rows+1u,e->lay.gw);
    T(e->grid.cells[(size_t)(e->menu_rows+1u)*e->grid.dims.cols+e->lay.gw].bg==e->colors.hover);
    T(seen.rows==(UINT64_C(1)<<(e->menu_rows+1u)) && !seen.pixels);
    editor_close(e);
}
static void native_hover(void)
{
    for (unsigned light=0;light<2;light++) {
        char path[]="/tmp/r3-native-hover-XXXXXX"; int fd=mkstemp(path); T(fd>=0);
        const char *pref=light ? "theme='light'\n" : "theme='dark'\n";
        T(write(fd,pref,strlen(pref))==(ssize_t)strlen(pref)); close(fd);
        render_backend backend={0}; T(render_cpu_backend(&backend)==0);
        editor_config cfg={.cols=80,.rows=12,.font_dpi=192,.wrap_mode=-1,.preferences_path=path};
        editor *e=NULL; T(editor_open(&e,&cfg,&backend)==0); if (!e) { unlink(path); continue; } settle(e);
        uint64_t id; T(editor_add_buffer(e,NULL,NULL,0,&id)==0); settle(e);
        T(editor_select_tab(e,0)==0); settle(e);
        /* One page surface through active/inactive tabs and the unused tail,
         * in each theme. Theme token ladders remain an independent contract. */
        bool page_strip=true;
        for (uint32_t c=0;c<e->grid.dims.cols;c++)
            if (e->grid.cells[(size_t)e->menu_rows*e->grid.dims.cols+c].bg!=e->layout_cfg.bg) page_strip=false;
        T(page_strip);
        x11_plat *x=x11_private(&e->platform); T(x);
        if (x) {
            uint32_t col=e->strip.first_col+e->strip.tab_cols+1u;
            xcb_warp_pointer(x->conn,XCB_NONE,x->win,0,0,0,0,(int16_t)(e->lay.gw*e->grid.dims.cell_w),
                (int16_t)((e->tab_rows+1u)*e->grid.dims.cell_h)); xcb_flush(x->conn);
            uint64_t deadline=trace_now_ns()+EDITOR_IDLE_NS;
            while (e->pointer_shape!=PLAT_CURSOR_TEXT && trace_now_ns()<deadline) T(editor_step(e,1)>=0);
            T(e->pointer_shape==PLAT_CURSOR_TEXT); settle(e);
            xcb_warp_pointer(x->conn,XCB_NONE,x->win,0,0,0,0,(int16_t)(col*e->grid.dims.cell_w),
                (int16_t)(e->menu_rows*e->grid.dims.cell_h+e->grid.dims.cell_h/2u)); xcb_flush(x->conn);
            deadline=trace_now_ns()+EDITOR_IDLE_NS;
            while (e->hover.kind!=EDITOR_HOVER_TAB && trace_now_ns()<deadline) T(editor_step(e,1)>=0);
            settle(e);
            if (e->hover.kind!=EDITOR_HOVER_TAB || e->hover.index!=1u)
                fprintf(stderr,"native hover light=%u kind=%u index=%zu drag=%llu/%u/%u pointer=%u\n",light,(unsigned)e->hover.kind,e->hover.index,(unsigned long long)e->drag_id,e->map_drag,e->text_drag,(unsigned)e->pointer_shape);
            T(e->hover.kind==EDITOR_HOVER_TAB && e->hover.index==1u);
            T(e->grid.cells[(size_t)e->menu_rows*e->grid.dims.cols+col].fg==e->colors.tab_hover);
            stroke(e,'f',PLAT_MOD_CTRL);
            findui_geometry g=findui_get_geometry(&e->find,editor_panel_bounds(e));
            uint32_t row=e->grid.dims.rows-e->panel_rows;
            xcb_warp_pointer(x->conn,XCB_NONE,x->win,0,0,0,0,(int16_t)(g.toggles*e->grid.dims.cell_w+e->grid.dims.cell_w/2u),
                (int16_t)(row*e->grid.dims.cell_h+e->grid.dims.cell_h/2u)); xcb_flush(x->conn);
            deadline=trace_now_ns()+EDITOR_IDLE_NS;
            while (e->hover.kind!=EDITOR_HOVER_FIND && trace_now_ns()<deadline) T(editor_step(e,1)>=0);
            settle(e);
            T(e->hover.kind==EDITOR_HOVER_FIND && e->hover.index==FINDUI_HIT_CASE);
            T(e->grid.cells[(size_t)row*e->grid.dims.cols+g.toggles].bg==e->colors.hover);
        }
        editor_close(e); unlink(path);
    }
}
int main(int argc, char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--native-hover")) { native_hover(); return failures ? 1 : 0; }
    static_design(); moving_states(); native_hover();
    if (failures) return 1;
    puts("editor_design_test: margins, spacer, panel surfaces and placeholders passed"); return 0;
}
