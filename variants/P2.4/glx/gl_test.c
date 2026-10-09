#include "base/base.h"
#include "render/render.h"
/* The frozen suite's guard spans present/events. Scope its existing assertion
 * to input->submit (P2.0 law-2 addendum); GL/XCB internals are exempt. */
static bool gl_test_guarding;
static size_t gl_test_allocations;
static void gl_test_guard_begin(void)
{
    gl_test_guarding=true; gl_test_allocations=0; edit_malloc_guard_begin();
}
static int gl_test_begin(render_grid *g, uint32_t id)
{
    if (gl_test_guarding) edit_malloc_guard_begin();
    return render_frame_begin(g,id);
}
static int gl_test_submit(render_backend *b, const render_grid *g,
                          const render_strip *strips, size_t count)
{
    int rc=render_backend_submit(b,g,strips,count);
    if (gl_test_guarding) gl_test_allocations+=edit_malloc_guard_end();
    return rc;
}
static size_t gl_test_guard_end(void)
{
    gl_test_guarding=false;
    printf("gl allocator input->submit: %zu allocations over 10000 typing frames (real_guard=%d)\n",
           gl_test_allocations,edit_malloc_guard_active()?1:0);
    return gl_test_allocations;
}
#define RENDER_TEST_EXTERNAL
#define main gl_conformance_main
#define edit_malloc_guard_begin gl_test_guard_begin
#define edit_malloc_guard_end gl_test_guard_end
#define render_frame_begin gl_test_begin
#define render_backend_submit gl_test_submit
#include "render_test.c"
#undef render_backend_submit
#undef render_frame_begin
#undef edit_malloc_guard_begin
#undef edit_malloc_guard_end
#undef main
#include "gl/gl.h"
#include <dlfcn.h>
static void *gl_test_dlopen(const char *name, int flags)
{
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    flags|=RTLD_NODELETE;
#endif
#endif
    return dlopen(name,flags);
}
#define dlopen gl_test_dlopen
#define GL_READBACK_TEST
#define render_gl_backend gl_readback_factory
#define gl_backend_poll gl_readback_poll
#define gl_backend_query gl_readback_query
#define gl_backend_read_pixels gl_readback_pixels
#define gl_backend_present_complete gl_readback_complete
#include "../src/gl/gl.c"
#undef gl_backend_present_complete
#undef gl_backend_read_pixels
#undef gl_backend_query
#undef gl_backend_poll
#undef render_gl_backend
#undef GL_READBACK_TEST
#undef dlopen
static bool gl_test_readback;

#include "x11/plat.h"
#include <xcb/xcb.h>

static plat gl_test_platform;
static render_backend *gl_test_current;
static int gl_test_error;
static void gl_test_event(void *ud, const plat_event *ev) { (void)ud; (void)ev; }
static void gl_test_complete(void *ud, uint32_t serial, uint64_t ust, uint64_t msc)
{
    (void)ud;
    int rc = gl_backend_present_complete(gl_test_current, serial, ust, msc);
    if (rc != RENDER_OK && rc != RENDER_ERR_FRAME) gl_test_error = rc;
}
int render_test_prepare(render_backend *b, render_config *cfg)
{
    plat_config pc = {.title="GLX conformance",.width=32,.height=128,.work_eventfd=-1};
    if (!gl_test_platform.conn) {
        CHECK(plat_init(&gl_test_platform, &pc) == PLAT_OK);
        plat_map(&gl_test_platform);
    }
    cfg->platform = &gl_test_platform;
    gl_test_current = b; gl_test_error = 0;
    return gl_test_readback?gl_readback_factory(b):render_gl_backend(b);
}
int render_test_pump(render_backend *b)
{
    plat_callbacks cb = {.on_event=gl_test_event,.on_present_complete=gl_test_complete};
    int rc = gl_backend_poll(b);
    if (rc != RENDER_OK) return rc;
    /* Xvfb cannot produce GLX PIXMAP completions. Diagnostic lifecycle only;
     * never report this synthetic acknowledgement as displayed timing. */
    if (gl_test_readback && b->active && b->device_seen && !b->complete_seen) {
        gl_backend_status status;
        if (gl_backend_query(b,&status)!=RENDER_OK) return RENDER_ERR_DEVICE;
        if (gl_backend_present_complete(b,status.swap_serial,0,0)!=RENDER_ERR_FRAME) {
            fputs("gl completion: FAIL zero MSC was accepted\n",stderr);
            return RENDER_ERR_DEVICE;
        }
        if (gl_backend_present_complete(b,status.swap_serial+1u,0,status.displayed_msc+1u)!=RENDER_ERR_FRAME)
            return RENDER_ERR_DEVICE;
        gl_present_message native={status.swap_serial,0,0,status.displayed_msc+1u};
        work_msg message={.kind=GL_PRESENT_MSG_KIND}; memcpy(message.data,&native,sizeof native);
        render_event complete={RENDER_EVENT_WORK,b->active_frame,0,&message};
        rc=render_backend_event(b,&complete);
        if (rc!=RENDER_OK) return rc;
        if (gl_backend_present_complete(b,native.serial,0,native.msc)!=RENDER_ERR_FRAME)
            return RENDER_ERR_DEVICE;
        return RENDER_OK;
    }
    if (plat_run_for(&gl_test_platform, &cb, 0) != PLAT_OK) return RENDER_ERR_DEVICE;
    return gl_test_error;
}
/* All phases borrow one fixture. Avoid repeated platform initialisation, which
 * has an existing intermittent XKB failure under Xvfb (HANDOFF). */
void render_test_cleanup(void) { gl_test_current=NULL; }

static void gl_test_t5(void *ud, uint32_t id, uint64_t ns)
{
    done_hook(ud,id,ns); render_trace_device_done(NULL,id,ns);
}
static void gl_test_t6(void *ud, uint32_t id, uint64_t ns)
{
    complete_hook(ud,id,ns); render_trace_present_complete(NULL,id,ns);
}
static int gl_test_trace_frame(uint32_t id)
{
    FILE *file=tmpfile(); CHECK(file!=NULL);
    CHECK(trace_dump(file)==0); rewind(file);
    trace_loaded loaded; CHECK(trace_fmt_load_dump(file,&loaded)==0); fclose(file);
    unsigned counts[3]={0}; uint64_t times[3]={0};
    for (size_t i=0;i<loaded.nrecs;i++) {
        trace_rec rec=loaded.recs[i];
        if (rec.frame_id==id && rec.ev>=TRACE_T4_PRESENT_SUBMITTED && rec.ev<=TRACE_T6_PRESENT_COMPLETE) {
            unsigned at=(unsigned)rec.ev-(unsigned)TRACE_T4_PRESENT_SUBMITTED;
            counts[at]++; times[at]=rec.ns;
        }
    }
    trace_fmt_dump_free(&loaded);
    CHECK(counts[0]==1 && counts[1]==1 && counts[2]==1);
    CHECK(times[0]>0 && times[0]<=times[1] && times[1]<=times[2]);
    puts("gl_trace: PASS original frame ID, exactly one T4/T5/T6, monotonic observation times");
    return 0;
}
static int gl_pixels_test(void)
{
    render_backend b = {0}; delivery d = {0};
    render_config cfg = {.dims={4,3,8,15},.max_width=32,.max_height=45,
        .max_cells=12,.max_glyphs=3,.max_pages=2,.max_atlas_bytes=480,
        .hooks={gl_test_t5,gl_test_t6,&d}};
    CHECK(render_test_prepare(&b, &cfg) == RENDER_OK);
    edit_arena arena; CHECK(edit_arena_init(&arena,b.info.state_size+b.info.state_align)==0);
    void *state=edit_arena_alloc(&arena,b.info.state_size,b.info.state_align);
    CHECK(init_on_worker(&b,&cfg,state,b.info.state_size)==RENDER_OK);
    uint8_t atlas[240];
    for (size_t i=0;i<sizeof atlas;i++) atlas[i]=(uint8_t)((i*37u)%256u);
    render_atlas_page pages[2]={{atlas,sizeof atlas,16,16,15},{atlas,sizeof atlas,16,8,15}};
    render_glyph glyphs[3]={{65,0,0,0,8,15},{66,0,0,0,16,15},{67,1,2,1,4,8}};
    render_cell cells[12]; uint64_t dirty=0; render_grid grid;
    CHECK(render_grid_init(&grid,cfg.dims,cells,12,&dirty,1)==RENDER_OK);
    grid.pages=pages; grid.page_count=2; grid.glyphs=glyphs; grid.glyph_count=3;
    for (size_t i=0;i<12;i++) cells[i]=(render_cell){65,0,0xe19237,0x18395a,0,0};
    cells[0].attrs=RENDER_ATTR_INVERSE;
    cells[4].attrs=RENDER_ATTR_UNDERLINE;
    cells[6]=(render_cell){66,1,0xe19237,0x18395a,RENDER_ATTR_WIDE_LEFT,0};
    cells[7]=(render_cell){0,RENDER_NO_SLOT,0xe19237,0x18395a,RENDER_ATTR_WIDE_RIGHT,0};
    cells[8]=(render_cell){67,2,0xe19237,0x18395a,0,0};
    cells[11]=(render_cell){0,RENDER_NO_SLOT,0xabcdef,0x654321,0,0};
    edit_malloc_guard_begin();
    CHECK(render_frame_begin(&grid,1)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
    render_strip strip={0,3}; CHECK(render_backend_submit(&b,&grid,&strip,1)==RENDER_OK);
    size_t first_allocations=edit_malloc_guard_end();
    if (edit_malloc_guard_active()) CHECK(first_allocations==0);
    printf("gl allocator first input->submit: %zu allocations (real_guard=%d)\n",
           first_allocations,edit_malloc_guard_active()?1:0);
    /* Poison caller metadata immediately: renderer must own its snapshot. */
    render_cell expected_cells[12]; memcpy(expected_cells,cells,sizeof cells);
    for (size_t i=0;i<12;i++) cells[i]=(render_cell){0,RENDER_NO_SLOT,0,0,0,0};
    strip=(render_strip){2,1};
    glyphs[0].glyph_index=99; pages[0].width=1;
    CHECK(finish_frame(&b,1)==0);
    CHECK(gl_test_trace_frame(1)==0);
    uint8_t pixels[32*45*4];
    CHECK(gl_backend_read_pixels(&b,pixels,sizeof pixels)==RENDER_OK);
    for (uint32_t y=0;y<45;y++) for (uint32_t x=0;x<32;x++) {
        size_t ci=(size_t)(y/15)*4+x/8;
        render_cell c=expected_cells[ci]; uint32_t fg=c.fg,bg=c.bg;
        if (c.attrs&RENDER_ATTR_INVERSE) { uint32_t tmp=fg; fg=bg; bg=tmp; }
        uint32_t a=c.atlas_slot==RENDER_NO_SLOT ? 0u : atlas[(size_t)(y%15)*16+x%8];
        if (ci==7) a=atlas[(size_t)(y%15)*16+8+x%8];
        if (ci==8) a=x%8<4 && y%15<8?atlas[(size_t)(y%15+1u)*16+x%8+2u]:0u;
        if ((c.attrs&RENDER_ATTR_UNDERLINE) && y%15==14) a=255;
        size_t offset=((size_t)(44-y)*32+x)*4;
        for (uint32_t channel=0;channel<3;channel++) {
            uint32_t shift=(2-channel)*8;
            uint8_t expected=(uint8_t)((((fg>>shift)&255u)*a+((bg>>shift)&255u)*(255u-a)+127u)/255u);
            CHECK(pixels[offset+channel]==expected);
        }
        CHECK(pixels[offset+3]==255);
    }
    CHECK(d.t5_count==1 && d.t6_count==1);
    /* Also check the actual X drawable: the grid must start at the window's
     * top-left even when its reserved/visible height exceeds the grid. */
    xcb_get_image_reply_t *image=xcb_get_image_reply(gl_test_platform.conn,
        xcb_get_image(gl_test_platform.conn,XCB_IMAGE_FORMAT_Z_PIXMAP,gl_test_platform.win,0,0,32,45,UINT32_MAX),NULL);
    CHECK(image!=NULL && xcb_get_image_data_length(image)>=(int)sizeof pixels);
    const uint8_t *window_pixels=xcb_get_image_data(image);
    for (uint32_t y=0;y<45;y++) for (uint32_t x=0;x<32;x++) {
        uint32_t actual=0; memcpy(&actual,window_pixels+((size_t)y*32+x)*4,4);
        size_t at=((size_t)(44-y)*32+x)*4;
        uint32_t expected=((uint32_t)pixels[at]<<16)|((uint32_t)pixels[at+1u]<<8)|pixels[at+2u];
        if ((actual&UINT32_C(0xffffff))!=expected) {
            fprintf(stderr,"gl window pixel: (%u,%u) actual=%06x expected=%06x\n",x,y,actual&UINT32_C(0xffffff),expected);
            free(image); CHECK(false);
        }
    }
    free(image); puts("gl_window: PASS actual drawable top-left pixels match retained surface");
    gl_backend_status status; CHECK(gl_backend_query(&b,&status)==RENDER_OK && status.present_verified==!gl_test_readback);
    const char *mode=getenv("EDIT_GL_VBO");
    CHECK(status.persistent_requested==(mode && strcmp(mode,"persistent")==0));
    CHECK(!status.persistent_active || status.persistent_requested);
    if (status.persistent_requested && !status.persistent_active)
        CHECK(strcmp(b.info.name,"glx-orphan(fallback)")==0);
    printf("gl_pixels: PASS 4x3 exact coverage/inverse/underline/wide/background/snapshot, VBO=%s MSC=%llu completion_source=%s\n",
        status.persistent_active?"persistent":"orphan",(unsigned long long)status.displayed_msc,
        gl_test_readback?"synthetic_diagnostic":"PresentCompleteNotify");
    /* Repeat partial updates across swapped back buffers; retained rows must
     * be byte-identical. Atlas contents may change after the previous T5. */
    uint8_t before[sizeof pixels]; memcpy(before,pixels,sizeof pixels);
    memcpy(cells,expected_cells,sizeof cells);
    glyphs[0].glyph_index=65; pages[0].width=16;
    for (uint32_t id=2;id<=4;id++) {
        cells[4].bg^=UINT32_C(0x00123456);
        CHECK(render_frame_begin(&grid,id)==RENDER_OK && render_mark_rows(&grid,1,1)==RENDER_OK);
        strip=(render_strip){1,1}; CHECK(render_backend_submit(&b,&grid,&strip,1)==RENDER_OK);
        CHECK(finish_frame(&b,id)==0);
        CHECK(gl_backend_read_pixels(&b,pixels,sizeof pixels)==RENDER_OK);
        CHECK(memcmp(pixels,before,15u*32u*4u)==0);
        CHECK(memcmp(pixels+30u*32u*4u,before+30u*32u*4u,15u*32u*4u)==0);
        CHECK(memcmp(pixels+15u*32u*4u,before+15u*32u*4u,15u*32u*4u)!=0);
        memcpy(before,pixels,sizeof pixels);
    }
    atlas[0]=255u;
    CHECK(render_frame_begin(&grid,5)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
    strip=(render_strip){0,3}; CHECK(render_backend_submit(&b,&grid,&strip,1)==RENDER_OK);
    CHECK(finish_frame(&b,5)==0);
    CHECK(gl_backend_read_pixels(&b,pixels,sizeof pixels)==RENDER_OK);
    size_t top=44u*32u*4u;
    CHECK(pixels[top]==0x18 && pixels[top+1u]==0x39 && pixels[top+2u]==0x5a);
    puts("gl_retained: PASS partial damage preserves both untouched rows across swaps; padded multi-page atlas");
    render_backend_shutdown(&b); edit_arena_free(&arena); render_test_cleanup();
    return 0;
}
int main(int argc, char **argv)
{
    trace_init(); CHECK(trace_thread_register()>=0);
    if (argc==2 && (!strcmp(argv[1],"--init-unsupported") || !strcmp(argv[1],"--readback-init-unsupported"))) {
        gl_test_readback=!strcmp(argv[1],"--readback-init-unsupported");
        render_backend b={0};
        render_config cfg={.dims={4,3,8,15},.max_width=32,.max_height=45,.max_cells=12};
        CHECK(render_test_prepare(&b,&cfg)==RENDER_OK);
        edit_arena arena; CHECK(edit_arena_init(&arena,b.info.state_size+b.info.state_align)==0);
        void *state=edit_arena_alloc(&arena,b.info.state_size,b.info.state_align);
        CHECK(init_on_worker(&b,&cfg,state,b.info.state_size)==RENDER_ERR_UNSUPPORTED);
        CHECK(!b.initialized && b.state==NULL);
        render_backend_shutdown(&b); edit_arena_free(&arena); render_test_cleanup();
        plat_shutdown(&gl_test_platform);
        puts("gl_init: PASS unsupported context cleans up for CPU fallback"); return 0;
    }
    if (getenv("DISPLAY")==NULL) {
        render_backend b={0}; CHECK(render_gl_backend(&b)==RENDER_OK);
        render_config cfg={.dims={1,1,8,15},.max_width=8,.max_height=15,.max_cells=1};
        edit_arena arena; CHECK(edit_arena_init(&arena,b.info.state_size+b.info.state_align)==0);
        void *state=edit_arena_alloc(&arena,b.info.state_size,b.info.state_align);
        CHECK(init_on_worker(&b,&cfg,state,b.info.state_size)==RENDER_ERR_UNSUPPORTED);
        edit_arena_free(&arena); puts("gl_test: SKIP no DISPLAY (clean init failure checked)"); return 0;
    }
    /* Probe the production factory first; missing presentation support must
     * still cleanly reject init, even though the renderer can be read back. */
    render_backend probe={0};
    render_config probe_cfg={.dims={4,3,8,15},.max_width=32,.max_height=45,.max_cells=12};
    CHECK(render_test_prepare(&probe,&probe_cfg)==RENDER_OK);
    edit_arena probe_arena; CHECK(edit_arena_init(&probe_arena,probe.info.state_size+probe.info.state_align)==0);
    void *probe_state=edit_arena_alloc(&probe_arena,probe.info.state_size,probe.info.state_align);
    int probe_rc=init_on_worker(&probe,&probe_cfg,probe_state,probe.info.state_size);
    CHECK(probe_rc==RENDER_OK || probe_rc==RENDER_ERR_UNSUPPORTED);
    if (probe_rc==RENDER_ERR_UNSUPPORTED) {
        CHECK(!probe.initialized && probe.state==NULL);
        gl_test_readback=true;
        puts("gl_present: SKIP production GLX presentation unsupported; readback diagnostics with synthetic T6 only");
    }
    render_backend_shutdown(&probe); edit_arena_free(&probe_arena); render_test_cleanup();
    CHECK(gl_pixels_test()==0);
    if (argc==2 && strcmp(argv[1],"--pixels-only")==0) { plat_shutdown(&gl_test_platform); return 0; }
    CHECK(gl_conformance_main()==0);
    plat_shutdown(&gl_test_platform);
    puts("gl_test: PASS (frozen render checks; diagnostic T6 on unsupported Xvfb)"); return 0;
}
