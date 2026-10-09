#include "gl/gl_driver.h"
#include <stdio.h>
#define GL_CHECK(c) do { if (!(c)) { fprintf(stderr,"gl_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
/* Private unit access: compile the renderer with renamed exports. This tests
 * the real snapshot code with a native dispatch that rejects context binding;
 * it does not pretend to supply hardware fence/display completions. */
#define render_gl_backend gl_test_factory
#define gl_present_complete gl_test_present_complete
#define gl_buffer_mode gl_test_buffer_mode
#define gl_device_name gl_test_device_name
#define gl_displayed_msc gl_test_displayed_msc
#define gl_read_pixels gl_test_read_pixels
#define gl_cells_acquire gl_test_cells_acquire
#define gl_cells_submit gl_test_cells_submit
#include "../src/gl/gl.c"
#undef render_gl_backend
#undef gl_present_complete
#undef gl_buffer_mode
#undef gl_device_name
#undef gl_displayed_msc
#undef gl_read_pixels
#undef gl_cells_acquire
#undef gl_cells_submit
static EGLBoolean gl_reject_bind(EGLDisplay display, EGLSurface draw,
                                 EGLSurface read, EGLContext context)
{ (void)display; (void)draw; (void)read; (void)context; return EGL_FALSE; }
static int gl_snapshot_test(void)
{
    render_cell cells[12]; uint64_t dirty[1]; render_grid grid;
    gl_instance instances[12], mapped[12]; gl_strip strips[2]; uint8_t seen[1];
    uint8_t cache[32]={0}, atlas_dirty[8]={0}, pixels[24]; size_t offsets[1];
    render_atlas_page page={pixels,24,6,4,4}; render_glyph glyph={65,0,1,0,3,4};
    gl_state state={.instances=instances,.strips=strips,.glyph_seen=seen,
        .mapped=mapped,.eMakeCurrent=gl_reject_bind,.atlas_cache=cache,
        .atlas_dirty=atlas_dirty,.atlas_width=4,.atlas_height=8,.page_offsets=offsets};
    render_backend b={.state=&state};
    for (size_t i=0;i<12;i++) cells[i]=(render_cell){0,RENDER_NO_SLOT,0xffffff,0x123456,0,0};
    GL_CHECK(render_grid_init(&grid,(render_dims){4,3,4,4},cells,12,dirty,1)==RENDER_OK);
    grid.pages=&page; grid.page_count=1; grid.glyphs=&glyph; grid.glyph_count=1;
    cells[4]=(render_cell){65,0,0xffffff,0x123456,0,0};
    uint32_t id=0;
    for (uint32_t mode=0;mode<2;mode++) for (uint32_t frame=0;frame<10000;frame++) {
        state.persistent=mode!=0;
        edit_malloc_guard_begin();
        memset(pixels,(int)(frame%255u+1u),sizeof pixels);
        int rc=render_frame_begin(&grid,++id);
        if (rc==RENDER_OK) rc=render_mark_rows(&grid,1,1);
        render_strip row={1,1};
        if (rc==RENDER_OK) rc=render_grid_validate(&grid);
        if (rc==RENDER_OK) rc=gl_submit(&b,&grid,&row,1);
        size_t allocations=edit_malloc_guard_end();
        GL_CHECK(rc==RENDER_OK);
        GL_CHECK(allocations==0 && state.instance_count==4);
        GL_CHECK(instances[0].pos[1]==4 && instances[0].color[1]==0x123456);
        GL_CHECK(cache[1]==pixels[1] && cache[15]==pixels[21] && atlas_dirty[0] && atlas_dirty[3]);
        if (mode!=0) GL_CHECK(memcmp(instances,mapped,4*sizeof(gl_instance))==0);
    }
    puts("gl_test: snapshot input->submit: 10000 frames per VBO mode incl. changing atlas, 0 allocations, no native calls");
    return 0;
}
/* Native dispatch encodes timeout/signalled/failed in the fake fence handle;
 * no globals, no timing assumptions and no claims of GPU completion. */
static GLenum gl_ring_script_wait(GLsync fence,GLbitfield flags,GLuint64 timeout)
{
    if (flags!=0 || timeout!=0) return GL_WAIT_FAILED;
    if ((uintptr_t)fence==1) return GL_TIMEOUT_EXPIRED;
    return (uintptr_t)fence==2 ? GL_ALREADY_SIGNALED : GL_WAIT_FAILED;
}
static void gl_ring_script_delete(GLsync fence) { (void)fence; }
static int gl_ring_unit_test(void)
{
    render_cell slots[GL_CELL_RING][12], cpu[12]; uint64_t dirty[1]; render_grid g;
    gl_strip strips[2]; uint8_t seen[1], cache[1], atlas_dirty[1]; size_t offsets[1];
    gl_state s={.upload=GL_UPLOAD_PERSISTENT,.cell_bytes=sizeof cpu,.dims={4,3,4,4},
        .strips=strips,.glyph_seen=seen,.atlas_cache=cache,.atlas_dirty=atlas_dirty,
        .atlas_width=1,.atlas_height=1,.page_offsets=offsets,
        .gClientWaitSync=gl_ring_script_wait,.gDeleteSync=gl_ring_script_delete};
    for (uint32_t i=0;i<GL_CELL_RING;i++) {
        s.ring[i].cells=slots[i];
        for (size_t j=0;j<12;j++) slots[i][j]=(render_cell){0,RENDER_NO_SLOT,0,0,0,0};
    }
    render_backend b={0}; GL_CHECK(gl_test_factory(&b)==RENDER_OK);
    b.state=&s; b.initialized=true;
    b.config=(render_config){.dims=s.dims,.max_width=16,.max_height=12,.max_cells=12};
    GL_CHECK(render_grid_init(&g,s.dims,cpu,12,dirty,1)==RENDER_OK);
    s.ring[0].fence=(GLsync)(uintptr_t)1;
    GL_CHECK(gl_test_cells_acquire(&b,&g,false)==RENDER_ERR_BUSY && g.cells==cpu && !s.leased);
    GL_CHECK(gl_ring_poll(&s)==RENDER_OK && s.ring[0].fence!=NULL);
    s.ring[0].fence=(GLsync)(uintptr_t)3;
    GL_CHECK(gl_ring_poll(&s)==RENDER_ERR_DEVICE && s.ring[0].fence!=NULL);
    s.ring[0].fence=(GLsync)(uintptr_t)2;
    GL_CHECK(gl_ring_poll(&s)==RENDER_OK && s.ring[0].fence==NULL);
    for (uint32_t id=1;id<=10;id++) {
        edit_malloc_guard_begin();
        int rc=gl_test_cells_acquire(&b,&g,id!=1);
        if (rc==RENDER_OK) rc=render_frame_begin(&g,id);
        if (rc==RENDER_OK) {
            if (id!=1 && g.cells[0].bg!=id-1) rc=RENDER_ERR_DEVICE;
            g.cells[0].bg=id;
            rc=render_mark_full(&g);
        }
        render_strip full={0,3};
        if (rc==RENDER_OK) rc=gl_test_cells_submit(&b,&g,&full,1);
        size_t allocations=edit_malloc_guard_end();
        GL_CHECK(rc==RENDER_OK && allocations==0 && g.cells==NULL);
        GL_CHECK(s.ring_current==(id-1)%GL_CELL_RING && slots[s.ring_current][0].bg==id);
        GL_CHECK(gl_test_cells_acquire(&b,&g,true)==RENDER_ERR_BUSY && g.cells==NULL);
        s.ring[s.ring_current].fence=(GLsync)(uintptr_t)2;
        GL_CHECK(gl_ring_poll(&s)==RENDER_OK);
        /* Adapter completions alone are scripted; the ring unit is not a
         * native lifecycle test. */
        b.active=false;
    }
    GL_CHECK(gl_test_cells_acquire(&b,&g,true)==RENDER_OK);
    render_cell *lease=g.cells;
    GL_CHECK(gl_test_cells_acquire(&b,&g,true)==RENDER_ERR_BUSY && g.cells==lease);
    GL_CHECK(render_frame_begin(&g,11)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
    g.cells[0].reserved=1; render_strip full={0,3};
    GL_CHECK(gl_test_cells_submit(&b,&g,&full,1)==RENDER_ERR_CELL && g.cells==lease && s.leased);
    g.cells[0].reserved=0;
    GL_CHECK(gl_test_cells_submit(&b,&g,&full,1)==RENDER_OK && g.cells==NULL);
    b.active=false;
    /* Ordinary grids retain the frozen copy-on-submit guarantee. */
    for (size_t i=0;i<12;i++) cpu[i]=(render_cell){0,RENDER_NO_SLOT,0,0x123456,0,0};
    g.cells=cpu;
    GL_CHECK(render_frame_begin(&g,12)==RENDER_OK && render_mark_full(&g)==RENDER_OK);
    GL_CHECK(render_backend_submit(&b,&g,&full,1)==RENDER_OK);
    cpu[0].bg=0;
    GL_CHECK(slots[s.ring_current][0].bg==0x123456);
    puts("gl_test: cell ring PASS (triple wrap, timeout/failed fences, lease revocation, retained cells, retry, copy fallback, 0 allocations)");
    return 0;
}
/* Exercise the merged pacing callback itself. Only presentation/pumping are
 * scripted: this checks CPU scroll/lease ownership, not native T5/T6 or MSC. */
static int gl_pace_test_pump(gl_driver *d);
#define main gl_test_bench_main
#define gl_cells_acquire gl_test_cells_acquire
#define gl_cells_submit gl_test_cells_submit
#define gl_displayed_msc gl_test_displayed_msc
#define gl_driver_pump gl_pace_test_pump
#include "../bench/gl_bench.c"
#undef main
#undef gl_cells_acquire
#undef gl_cells_submit
#undef gl_displayed_msc
#undef gl_driver_pump
static int gl_pace_test_present(render_backend *b,uint32_t id)
{
    gl_state *s=b->state;
    if (id!=b->active_frame) return RENDER_ERR_FRAME;
    s->ring[s->ring_current].fence=(GLsync)(uintptr_t)2;
    return RENDER_OK;
}
static int gl_pace_test_pump(gl_driver *d)
{
    render_backend *b=d->backend; gl_state *s=b->state;
    int rc=gl_ring_poll(s);
    if (rc==RENDER_OK) rc=render_backend_signal(b,RENDER_EVENT_DEVICE_DONE,b->active_frame,bench_now_ns());
    if (rc==RENDER_OK) rc=render_backend_signal(b,RENDER_EVENT_PRESENT_COMPLETE,b->active_frame,bench_now_ns());
    s->msc++;
    return rc;
}
static int gl_pace_lease_test(void)
{
    const char *selected=getenv("EDIT_GL_UPLOAD"); char saved[16];
    bool restore=selected!=NULL;
    if (restore) { GL_CHECK(strlen(selected)<sizeof saved); strcpy(saved,selected); }
    GL_CHECK(setenv("EDIT_GL_UPLOAD","persistent",1)==0);
    render_cell slots[GL_CELL_RING][12],cpu[12],previous[12]; uint64_t dirty[1]; render_grid g;
    gl_strip strips[2]; uint8_t seen[95],cache[16],atlas_dirty[4],pixels[16]={0}; size_t offsets[1];
    uint32_t images[95][4]; render_glyph glyphs[95];
    render_atlas_page page={pixels,sizeof pixels,4,4,4};
    gl_state s={.upload=GL_UPLOAD_PERSISTENT,.cell_bytes=sizeof cpu,.dims={4,3,4,4},
        .strips=strips,.glyph_seen=seen,.atlas_cache=cache,.atlas_dirty=atlas_dirty,
        .atlas_width=4,.atlas_height=4,.page_offsets=offsets,.images=images,
        .gClientWaitSync=gl_ring_script_wait,.gDeleteSync=gl_ring_script_delete};
    for (uint32_t i=0;i<GL_CELL_RING;i++) s.ring[i].cells=slots[i];
    for (uint32_t i=0;i<95;i++) glyphs[i]=(render_glyph){32u+i,0,0,0,4,4};
    render_backend b={0}; GL_CHECK(gl_test_factory(&b)==RENDER_OK);
    b.state=&s; b.initialized=true; b.ops.present=gl_pace_test_present;
    gl_bench_hooks hooks={0};
    b.config=(render_config){.dims=s.dims,.max_width=16,.max_height=12,.max_cells=12,
        .max_glyphs=95,.max_pages=1,.max_atlas_bytes=16,
        .hooks={gl_bench_device,gl_bench_complete,&hooks}};
    GL_CHECK(render_grid_init(&g,s.dims,cpu,12,dirty,1)==RENDER_OK);
    g.pages=&page; g.page_count=1; g.glyphs=glyphs; g.glyph_count=95;
    uint32_t id=0,seed=UINT32_C(0x37216e9d);
    gl_bench_fill(cpu,12,&seed);
    gl_driver driver={.backend=&b}; gl_pace_rig rig={&driver,&g,&hooks,&id,&seed};
    for (uint32_t frame=0;frame<10;frame++) {
        render_pace_frame out={0};
        GL_CHECK(gl_pace_frame(&rig,frame!=0,&out)==0);
        GL_CHECK(g.cells==NULL && !b.active && !s.leased);
        GL_CHECK(s.ring_current==frame%GL_CELL_RING && out.msc==frame+1u);
        if (frame!=0) GL_CHECK(memcmp(slots[s.ring_current],previous+4,8*sizeof(render_cell))==0);
        memcpy(previous,slots[s.ring_current],sizeof previous);
    }
    GL_CHECK(restore ? setenv("EDIT_GL_UPLOAD",saved,1)==0 : unsetenv("EDIT_GL_UPLOAD")==0);
    puts("gl_test: paced persistent scroll PASS (merged callback, lease reacquire/revoke, retained rows, ring wrap, 0 allocations; scripted completion)");
    return 0;
}
/* The frozen suite's old guard also encloses present/event. Session 5 scopes
 * law 2 to input->submit: keep counting begin/fill/damage/submit and suspend
 * only around presentation/event dispatch. Production guard is unchanged. */
static bool gl_scope_on;
static size_t gl_scope_allocations;
static void gl_scope_begin(void)
{ gl_scope_on=true; gl_scope_allocations=0; edit_malloc_guard_begin(); }
static size_t gl_scope_end(void)
{ gl_scope_on=false; return gl_scope_allocations+edit_malloc_guard_end(); }
static void gl_scope_pause(void)
{ if (gl_scope_on) gl_scope_allocations+=edit_malloc_guard_end(); }
static void gl_scope_resume(void)
{ if (gl_scope_on) edit_malloc_guard_begin(); }
static int gl_scope_present(render_backend *b,uint32_t id)
{
    gl_scope_pause(); int rc=render_backend_present(b,id); gl_scope_resume(); return rc;
}
/* Include the frozen file unchanged; redirect only main and the test's guard
 * boundary around native present/event calls, as required by the settled law. */
#define RENDER_TEST_EXTERNAL
#define main gl_frozen_main
#define edit_malloc_guard_begin gl_scope_begin
#define edit_malloc_guard_end gl_scope_end
#define render_backend_present gl_scope_present
#include "render_test.c"
#undef main
#undef edit_malloc_guard_begin
#undef edit_malloc_guard_end
#undef render_backend_present
static gl_driver gl_conformance_driver;
int render_test_prepare(render_backend *b, render_config *cfg)
{ return gl_driver_prepare(&gl_conformance_driver, b, cfg); }
int render_test_pump(render_backend *b)
{
    (void)b; gl_scope_pause(); int rc=gl_driver_pump(&gl_conformance_driver);
    gl_scope_resume(); return rc;
}
void render_test_cleanup(void)
{ gl_driver_cleanup(&gl_conformance_driver); }
static void gl_ignore_hook(void *u, uint32_t id, uint64_t ns) { (void)u; (void)id; (void)ns; }
static uint8_t gl_expected_channel(uint32_t fg, uint32_t bg, unsigned shift, uint8_t a)
{ return (uint8_t)((((fg >> shift) & 255u) * a + ((bg >> shift) & 255u) * (255u-a) + 127u)/255u); }
static GLenum gl_signalled_fence(GLsync fence,GLbitfield flags,GLuint64 timeout)
{ (void)fence; return flags==0 && timeout==0 ? GL_ALREADY_SIGNALED : GL_WAIT_FAILED; }
static void gl_delete_test_fence(GLsync fence) { (void)fence; }
static int gl_trace_unit_test(void)
{
    trace_reset();
    gl_state s={.fence=(GLsync)(uintptr_t)1,.gClientWaitSync=gl_signalled_fence,
        .gDeleteSync=gl_delete_test_fence,.pending_serial=17,.msc=123,.present_verified=true};
    render_backend b={0}; GL_CHECK(gl_test_factory(&b)==RENDER_OK);
    b.state=&s; b.initialized=true; b.active=true; b.presented=true;
    b.active_frame=9001; b.submitted_ns=trace_now_ns();
    b.config.hooks=(render_hooks){render_trace_device_done,render_trace_present_complete,NULL};
    trace_record_at(b.submitted_ns,TRACE_T4_PRESENT_SUBMITTED,b.active_frame);
    GL_CHECK(gl_test_present_complete(&b,16,UINT64_MAX,123)==RENDER_ERR_FRAME);
    GL_CHECK(gl_test_present_complete(&b,17,UINT64_MAX,123)==RENDER_OK);
    GL_CHECK(!b.active && b.t5_sent && b.t6_sent && s.fence==NULL);
    GL_CHECK(gl_test_present_complete(&b,17,UINT64_MAX,123)==RENDER_ERR_FRAME);
    FILE *f=tmpfile(); GL_CHECK(f!=NULL && trace_dump(f)==0); rewind(f);
    trace_loaded loaded; GL_CHECK(trace_fmt_load_dump(f,&loaded)==0); fclose(f);
    size_t t5=0,t6=0; uint64_t device=0,complete=0;
    for (size_t i=0;i<loaded.nrecs;i++) {
        const trace_rec *r=&loaded.recs[i];
        GL_CHECK(r->frame_id==9001 && r->ns>=b.submitted_ns);
        if (r->ev==TRACE_T5_DEVICE_DONE) { t5++; device=r->ns; }
        if (r->ev==TRACE_T6_PRESENT_COMPLETE) { t6++; complete=r->ns; }
    }
    GL_CHECK(t5==1 && t6==1 && complete>=device);
    trace_fmt_dump_free(&loaded); trace_reset();
    puts("gl_test: trace dispatch unit PASS (bounded fence poll, original frame T5/T6, stale/duplicate rejection; scripted native dispatch)");
    return 0;
}
/* Readback unit context uses the production context/resource setup on the
 * worker, but does not select/probe Present or claim displayed completions.
 * The actual backend lifecycle below still requires real PIXMAP notifications. */
static int gl_readback_init(render_backend *b,const render_config *cfg)
{
    gl_state *s=b->state; memset(s,0,sizeof *s);
    s->platform=cfg->platform; s->dims=cfg->dims;
    s->max_width=cfg->max_width; s->max_height=cfg->max_height;
    if (gl_select_upload(s)!=RENDER_OK) return RENDER_ERR_ARG;
    const char *mode=getenv("EDIT_GL_VBO");
    s->requested_persistent=s->upload==GL_UPLOAD_LEGACY && mode!=NULL && strcmp(mode,"persistent")==0;
    int rc=gl_context_init(s,cfg,0);
    if (rc==RENDER_OK) {
        if (!s->eMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) rc=RENDER_ERR_INIT;
        else { s->bound=false; if (!s->eReleaseThread()) rc=RENDER_ERR_INIT; }
    }
    if (rc!=RENDER_OK) gl_release(s);
    return rc;
}
static int gl_readback_unit_test(void)
{
    render_backend b={0}; gl_driver d;
    render_config cfg={.dims={4,3,16,16},.max_width=64,.max_height=48,.max_cells=12,
        .max_glyphs=2,.max_pages=2,.max_atlas_bytes=768};
    GL_CHECK(gl_driver_prepare(&d,&b,&cfg)==RENDER_OK);
    b.ops.init=gl_readback_init;
    b.info.capabilities=RENDER_CAP_GPU; /* no synthetic display/fence timing */
    int rc=gl_driver_init(&d);
    if (rc==RENDER_ERR_UNSUPPORTED) {
        gl_driver_cleanup(&d); puts("gl_test: readback unit SKIP: EGL context unavailable"); return 0;
    }
    GL_CHECK(rc==RENDER_OK);
    const char *upload=getenv("EDIT_GL_UPLOAD");
    if (upload!=NULL) GL_CHECK(strcmp(gl_buffer_mode(&b),upload)==0);
    uint8_t coverage[256],wide[512];
    for (size_t i=0;i<256;i++) coverage[i]=(uint8_t)i;
    memset(wide,255,sizeof wide);
    render_atlas_page pages[2]={{coverage,256,16,16,16},{wide,512,32,32,16}};
    render_glyph glyphs[2]={{65,0,0,0,16,16},{66,1,0,0,32,16}};
    render_cell cells[12],expected[12]; uint64_t dirty[1]; render_grid grid;
    for (size_t i=0;i<12;i++) cells[i]=(render_cell){65,0,0xe17123,0x173b91,0,0};
    cells[1].attrs=RENDER_ATTR_INVERSE; cells[2].attrs=RENDER_ATTR_UNDERLINE;
    cells[4]=(render_cell){66,1,0xabcdef,0x123456,RENDER_ATTR_WIDE_LEFT,0};
    cells[5]=(render_cell){0,RENDER_NO_SLOT,0xabcdef,0x123456,RENDER_ATTR_WIDE_RIGHT,0};
    cells[11]=(render_cell){0,RENDER_NO_SLOT,0xffffff,0x0a1b2c,0,0};
    memcpy(expected,cells,sizeof cells);
    GL_CHECK(render_grid_init(&grid,cfg.dims,cells,12,dirty,1)==RENDER_OK);
    grid.pages=pages; grid.page_count=2; grid.glyphs=glyphs; grid.glyph_count=2;
    bool direct=upload!=NULL && strcmp(upload,"persistent")==0;
    if (direct) {
        GL_CHECK(gl_cells_acquire(&b,&grid,false)==RENDER_OK);
        memcpy(grid.cells,expected,sizeof expected);
    }
    edit_malloc_guard_begin();
    rc=render_frame_begin(&grid,1);
    if (rc==RENDER_OK) rc=render_mark_full(&grid);
    render_strip full={0,3};
    if (rc==RENDER_OK) rc=direct ? gl_cells_submit(&b,&grid,&full,1) : render_backend_submit(&b,&grid,&full,1);
    size_t allocations=edit_malloc_guard_end();
    GL_CHECK(rc==RENDER_OK && allocations==0 && (!direct || grid.cells==NULL));
    memset(cells,0,sizeof cells); memset(glyphs,0,sizeof glyphs); memset(pages,0,sizeof pages);
    gl_state *s=b.state; GL_CHECK(gl_bind(s)); gl_upload_pending(s); gl_draw(s);
    uint8_t rgba[64*48*4]; GL_CHECK(gl_test_read_pixels(&b,rgba,sizeof rgba)==RENDER_OK);
    for (uint32_t y=0;y<48;y++) for (uint32_t x=0;x<64;x++) {
        render_cell c=expected[(y/16)*4+x/16]; uint32_t fg=c.fg,bg=c.bg;
        if (c.attrs & RENDER_ATTR_INVERSE) { uint32_t tmp=fg; fg=bg; bg=tmp; }
        uint8_t a=c.atlas_slot==RENDER_NO_SLOT ? 0 : coverage[(y%16)*16+x%16];
        if (y/16==1 && x<32) a=255;
        if ((c.attrs & RENDER_ATTR_UNDERLINE) && y%16==15) a=255;
        size_t off=((size_t)y*64+x)*4;
        GL_CHECK(rgba[off]==gl_expected_channel(fg,bg,16,a));
        GL_CHECK(rgba[off+1]==gl_expected_channel(fg,bg,8,a));
        GL_CHECK(rgba[off+2]==gl_expected_channel(fg,bg,0,a) && rgba[off+3]==255);
    }
    /* White-box retained-damage checks exercise the same GL drawing code.
     * No display or T6 completion is fabricated to release the adapter slot. */
    memcpy(cells,expected,sizeof cells); grid.cells=cells; grid.page_count=0; grid.glyph_count=0;
    for (size_t i=0;i<12;i++) {
        cells[i].glyph_index=0; cells[i].atlas_slot=RENDER_NO_SLOT; cells[i].attrs=0;
        if (i>=4 && i<8) cells[i].bg=0x345678;
    }
    render_strip middle={1,1};
    GL_CHECK(render_grid_validate(&grid)==RENDER_OK);
    GL_CHECK(gl_submit(&b,&grid,&middle,1)==RENDER_OK);
    gl_upload_pending(s); gl_draw(s);
    uint8_t updated[sizeof rgba]; GL_CHECK(gl_test_read_pixels(&b,updated,sizeof updated)==RENDER_OK);
    GL_CHECK(memcmp(rgba,updated,64*16*4)==0 && memcmp(rgba+64*32*4,updated+64*32*4,64*16*4)==0);
    for (size_t off=64*16*4;off<64*32*4;off+=4)
        GL_CHECK(updated[off]==0x34 && updated[off+1]==0x56 && updated[off+2]==0x78 && updated[off+3]==255);
    GL_CHECK(gl_submit(&b,&grid,NULL,0)==RENDER_OK); gl_upload_pending(s); gl_draw(s);
    GL_CHECK(gl_test_read_pixels(&b,rgba,sizeof rgba)==RENDER_OK && memcmp(rgba,updated,sizeof rgba)==0);
    if (direct) {
        /* Actual mapped layout/readback across more than one ring revolution,
         * with strip-only writes and retained-row preservation. */
        b.active=false;
        for (uint32_t id=2;id<11;id++) {
            GL_CHECK(gl_cells_acquire(&b,&grid,true)==RENDER_OK);
            GL_CHECK(render_frame_begin(&grid,id)==RENDER_OK);
            for (size_t i=4;i<8;i++) grid.cells[i].bg=0x100000u+id;
            GL_CHECK(render_mark_rows(&grid,1,1)==RENDER_OK);
            GL_CHECK(gl_cells_submit(&b,&grid,&middle,1)==RENDER_OK && grid.cells==NULL);
            gl_upload_pending(s); gl_draw(s);
            GL_CHECK(gl_test_read_pixels(&b,rgba,sizeof rgba)==RENDER_OK);
            GL_CHECK(memcmp(rgba,updated,64*16*4)==0 && memcmp(rgba+64*32*4,updated+64*32*4,64*16*4)==0);
            GL_CHECK(rgba[64*16*4]==0x10 && rgba[64*16*4+1]==0 && rgba[64*16*4+2]==id);
            b.active=false;
        }
        puts("gl_test: coherent mapped cells readback PASS (direct writes, triple ring wrap, retained strip)");
    }
    printf("gl_test: readback unit PASS (4x3, all 256 alpha values, inverse, underline, wide, snapshot), VBO=%s renderer=%s\n",
        gl_buffer_mode(&b),gl_device_name(&b));
    gl_driver_cleanup(&d); return 0;
}
static int gl_failure_test(void)
{
    const char *old=getenv("EDIT_GL_EGL_LIBRARY"); char saved[4096];
    bool restore=old!=NULL;
    if (restore) { GL_CHECK(strlen(old)<sizeof saved); memcpy(saved,old,strlen(old)+1); }
    const char *libraries[]={"/nonexistent/editor-libEGL.so", "libc.so.6"};
    for (size_t i=0;i<2;i++) {
        render_backend b={0}; gl_driver d;
        render_config cfg={.dims={4,3,4,4},.max_width=16,.max_height=12,.max_cells=12,
            .max_glyphs=1,.max_pages=1,.max_atlas_bytes=16};
        GL_CHECK(gl_driver_prepare(&d,&b,&cfg)==RENDER_OK);
        GL_CHECK(setenv("EDIT_GL_EGL_LIBRARY",libraries[i],1)==0);
        GL_CHECK(gl_driver_init(&d)==RENDER_ERR_UNSUPPORTED);
        GL_CHECK(!b.initialized && b.state==NULL);
        GL_CHECK(render_null_backend(&b)==RENDER_OK); /* failed init permits fallback */
        gl_driver_cleanup(&d);
    }
    GL_CHECK(restore ? setenv("EDIT_GL_EGL_LIBRARY",saved,1)==0 : unsetenv("EDIT_GL_EGL_LIBRARY")==0);
    render_backend b={0}; gl_driver d; edit_arena tiny;
    GL_CHECK(edit_arena_init(&tiny,16)==0);
    render_config cfg={.dims={4,3,4,4},.max_width=16,.max_height=12,.max_cells=12,
        .max_glyphs=1,.max_pages=1,.max_atlas_bytes=16,.arena=&tiny};
    GL_CHECK(gl_driver_prepare(&d,&b,&cfg)==RENDER_OK);
    GL_CHECK(gl_driver_init(&d)==RENDER_ERR_INIT);
    GL_CHECK(!b.initialized && b.state==NULL && tiny.used==0);
    gl_driver_cleanup(&d); edit_arena_free(&tiny);
    puts("gl_test: clean failure/fallback (missing library, missing symbols, exhausted arena)");
    return 0;
}
static int gl_pixels_test(void)
{
    render_backend b = {0}; gl_driver d;
    render_config cfg = {.dims={4,3,4,4}, .max_width=16, .max_height=12,
        .max_cells=12, .max_glyphs=2, .max_pages=2, .max_atlas_bytes=48,
        .hooks={gl_ignore_hook,gl_ignore_hook,NULL}};
    GL_CHECK(gl_driver_prepare(&d,&b,&cfg) == RENDER_OK);
    int init_result=gl_driver_init(&d);
    if (init_result==RENDER_ERR_UNSUPPORTED) {
        GL_CHECK(!b.initialized && b.state==NULL);
        gl_driver_cleanup(&d);
        puts("gl_test: native lifecycle SKIP: EGL swap does not supply matching PIXMAP Present MSC");
        return 2;
    }
    GL_CHECK(init_result==RENDER_OK);
    printf("gl_test: VBO=%s\n",gl_buffer_mode(&b));
    uint8_t coverage[16], wide[32];
    for (size_t i=0;i<16;i++) coverage[i]=(uint8_t)((i%4)*85);
    memset(wide,255,sizeof wide);
    render_atlas_page pages[2]={{coverage,16,4,4,4},{wide,32,8,8,4}};
    render_glyph glyphs[2]={{65,0,0,0,4,4},{66,1,0,0,8,4}};
    render_cell cells[12]; uint64_t dirty[1]; render_grid grid;
    for (size_t i=0;i<12;i++) cells[i]=(render_cell){65,0,0xe17123,0x173b91,0,0};
    cells[1].attrs=RENDER_ATTR_INVERSE; cells[2].attrs=RENDER_ATTR_UNDERLINE;
    cells[4]=(render_cell){66,1,0xabcdef,0x123456,RENDER_ATTR_WIDE_LEFT,0};
    cells[5]=(render_cell){0,RENDER_NO_SLOT,0xabcdef,0x123456,RENDER_ATTR_WIDE_RIGHT,0};
    cells[11]=(render_cell){0,RENDER_NO_SLOT,0xffffff,0x0a1b2c,0,0};
    render_cell expected[12]; memcpy(expected,cells,sizeof cells);
    GL_CHECK(render_grid_init(&grid,cfg.dims,cells,12,dirty,1)==RENDER_OK);
    grid.pages=pages; grid.page_count=2; grid.glyphs=glyphs; grid.glyph_count=2;
    GL_CHECK(render_frame_begin(&grid,1)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
    render_strip full={0,3}; edit_malloc_guard_begin();
    int first_submit_result=render_backend_submit(&b,&grid,&full,1);
    size_t first_submit_allocations=edit_malloc_guard_end();
    GL_CHECK(first_submit_result==RENDER_OK);
    printf("gl allocation probe: first_submit=%zu guard=%s\n",first_submit_allocations,edit_malloc_guard_active() ? "release" : "ASan inactive");
    memset(cells,0,sizeof cells); /* successful submit must consume metadata/cells */
    memset(glyphs,0,sizeof glyphs); memset(pages,0,sizeof pages);
    GL_CHECK(gl_driver_finish(&d,1)==RENDER_OK);
    uint8_t rgba[16*12*4]; GL_CHECK(gl_read_pixels(&b,rgba,sizeof rgba)==RENDER_OK);
    for (uint32_t y=0;y<12;y++) for (uint32_t x=0;x<16;x++) {
        render_cell c=expected[(y/4)*4+x/4]; uint32_t fg=c.fg,bg=c.bg;
        if (c.attrs & RENDER_ATTR_INVERSE) { uint32_t tmp=fg; fg=bg; bg=tmp; }
        uint8_t a=c.atlas_slot==RENDER_NO_SLOT ? 0 : coverage[(y%4)*4+x%4];
        if (y/4==1 && x<8) a=255; /* one wide image extends over continuation */
        if ((c.attrs & RENDER_ATTR_UNDERLINE) && y%4==3) a=255;
        size_t off=((size_t)y*16+x)*4;
        GL_CHECK(rgba[off]==gl_expected_channel(fg,bg,16,a));
        GL_CHECK(rgba[off+1]==gl_expected_channel(fg,bg,8,a));
        GL_CHECK(rgba[off+2]==gl_expected_channel(fg,bg,0,a)); GL_CHECK(rgba[off+3]==255);
    }
    /* Strip updates preserve pixels across alternating native swap buffers. */
    memcpy(cells,expected,sizeof cells);
    grid.page_count=0; grid.glyph_count=0;
    for (size_t i=4;i<8;i++) cells[i]=(render_cell){0,RENDER_NO_SLOT,0xffffff,0x345678,0,0};
    for (size_t i=0;i<12;i++) { cells[i].glyph_index=0; cells[i].atlas_slot=RENDER_NO_SLOT; cells[i].attrs=0; }
    for (uint32_t id=2;id<5;id++) {
        GL_CHECK(render_frame_begin(&grid,id)==RENDER_OK && render_mark_rows(&grid,1,1)==RENDER_OK);
        render_strip strip={1,1}; GL_CHECK(render_backend_submit(&b,&grid,&strip,1)==RENDER_OK);
        GL_CHECK(gl_driver_finish(&d,id)==RENDER_OK);
    }
    uint8_t updated[sizeof rgba]; GL_CHECK(gl_read_pixels(&b,updated,sizeof updated)==RENDER_OK);
    GL_CHECK(memcmp(updated,rgba,16*4*4)==0 && memcmp(updated+16*8*4,rgba+16*8*4,16*4*4)==0);
    GL_CHECK(updated[16*4*4]==0x34 && updated[16*4*4+1]==0x56 && updated[16*4*4+2]==0x78);
    size_t submit_allocations=0, present_allocations=0, event_allocations=0;
    for (uint32_t id=5;id<105;id++) {
        GL_CHECK(render_frame_begin(&grid,id)==RENDER_OK && render_mark_rows(&grid,1,1)==RENDER_OK);
        render_strip row={1,1};
        edit_malloc_guard_begin();
        int rc=render_backend_submit(&b,&grid,&row,1);
        submit_allocations+=edit_malloc_guard_end(); GL_CHECK(rc==RENDER_OK);
        edit_malloc_guard_begin(); rc=render_backend_present(&b,id);
        present_allocations+=edit_malloc_guard_end(); GL_CHECK(rc==RENDER_OK);
        uint64_t deadline=trace_now_ns()+UINT64_C(3000000000);
        edit_malloc_guard_begin();
        while (b.active && trace_now_ns()<deadline) {
            rc=gl_driver_pump(&d); if (rc!=RENDER_OK) break;
        }
        event_allocations+=edit_malloc_guard_end(); GL_CHECK(rc==RENDER_OK && !b.active);
    }
    printf("gl allocation probe: frames=100 submit=%zu present=%zu event=%zu guard=%s\n",
        submit_allocations,present_allocations,event_allocations,edit_malloc_guard_active() ? "release" : "ASan inactive");
    /* Reuse already reserved textures with changing referenced coverage. The
     * first sixteen iterations exercise every R8 alpha, including inverse. */
    pages[0]=(render_atlas_page){coverage,16,4,4,4}; pages[1]=(render_atlas_page){wide,32,8,8,4};
    glyphs[0]=(render_glyph){65,0,0,0,4,4}; glyphs[1]=(render_glyph){66,1,0,0,8,4};
    grid.page_count=2; grid.glyph_count=2;
    for (size_t i=0;i<4;i++) cells[i]=(render_cell){65,0,0xe17123,0x173b91,i==1 ? RENDER_ATTR_INVERSE : 0,0};
    size_t atlas_allocations=0;
    for (uint32_t id=105;id<205;id++) {
        for (size_t i=0;i<16;i++) coverage[i]=(uint8_t)(((id-105)*16+(uint32_t)i)&255u);
        GL_CHECK(render_frame_begin(&grid,id)==RENDER_OK && render_mark_rows(&grid,0,1)==RENDER_OK);
        render_strip row={0,1}; edit_malloc_guard_begin();
        int rc=render_backend_submit(&b,&grid,&row,1); atlas_allocations+=edit_malloc_guard_end();
        GL_CHECK(rc==RENDER_OK && gl_driver_finish(&d,id)==RENDER_OK);
        GL_CHECK(gl_read_pixels(&b,updated,sizeof updated)==RENDER_OK);
        for (uint32_t y=0;y<4;y++) for (uint32_t x=0;x<16;x++) {
            render_cell c=cells[x/4]; uint32_t fg=c.fg,bg=c.bg;
            if (c.attrs & RENDER_ATTR_INVERSE) { uint32_t tmp=fg; fg=bg; bg=tmp; }
            uint8_t a=coverage[y*4+x%4]; size_t off=((size_t)y*16+x)*4;
            GL_CHECK(updated[off]==gl_expected_channel(fg,bg,16,a));
            GL_CHECK(updated[off+1]==gl_expected_channel(fg,bg,8,a));
            GL_CHECK(updated[off+2]==gl_expected_channel(fg,bg,0,a));
        }
    }
    printf("gl allocation probe: atlas_update_frames=100 submit=%zu guard=%s\n",atlas_allocations,edit_malloc_guard_active() ? "release" : "ASan inactive");
    grid.page_count=0; grid.glyph_count=0;
    for (size_t i=0;i<4;i++) { cells[i].glyph_index=0; cells[i].atlas_slot=RENDER_NO_SLOT; cells[i].attrs=0; }
    for (size_t i=0;i<4;i++) cells[i].bg=0x13579b;
    for (size_t i=8;i<12;i++) cells[i].bg=0x2468ac;
    GL_CHECK(render_frame_begin(&grid,205)==RENDER_OK && render_mark_rows(&grid,0,1)==RENDER_OK && render_mark_rows(&grid,2,1)==RENDER_OK);
    render_strip separated[2]={{0,1},{2,1}};
    GL_CHECK(render_backend_submit(&b,&grid,separated,2)==RENDER_OK && gl_driver_finish(&d,205)==RENDER_OK);
    GL_CHECK(gl_read_pixels(&b,updated,sizeof updated)==RENDER_OK);
    GL_CHECK(updated[0]==0x13 && updated[1]==0x57 && updated[2]==0x9b);
    GL_CHECK(updated[16*4*4]==0x34 && updated[16*4*4+1]==0x56 && updated[16*4*4+2]==0x78);
    GL_CHECK(updated[16*8*4]==0x24 && updated[16*8*4+1]==0x68 && updated[16*8*4+2]==0xac);
    GL_CHECK(render_frame_begin(&grid,206)==RENDER_OK && render_backend_submit(&b,&grid,NULL,0)==RENDER_OK);
    GL_CHECK(gl_driver_finish(&d,206)==RENDER_OK && gl_read_pixels(&b,rgba,sizeof rgba)==RENDER_OK);
    GL_CHECK(memcmp(rgba,updated,sizeof rgba)==0);
    GL_CHECK(render_gl_backend(&b)==RENDER_ERR_STATE);
    GL_CHECK(render_frame_begin(&grid,207)==RENDER_OK && render_mark_full(&grid)==RENDER_OK);
    GL_CHECK(render_backend_submit(&b,&grid,&full,1)==RENDER_OK && render_backend_present(&b,207)==RENDER_OK);
    gl_driver_cleanup(&d); /* quiescent UI shutdown drains a pending real fence */
    return 0;
}
int main(int argc, char **argv)
{
    bool pixels_only=argc==2 && strcmp(argv[1],"--pixels-only")==0;
    GL_CHECK(argc==1 || pixels_only);
    trace_init(); GL_CHECK(trace_thread_register()>=0);
    GL_CHECK(gl_snapshot_test()==0);
    GL_CHECK(gl_trace_unit_test()==0);
    GL_CHECK(gl_ring_unit_test()==0);
    GL_CHECK(gl_pace_lease_test()==0);
    /* The trace registry has a finite lifetime thread budget. Run its frozen
     * conformance before the extra per-variant native init workers. */
    GL_CHECK(strips_test()==0 && arguments_test()==0 && cells_test()==0);
    GL_CHECK(async_test()==0 && trace_test()==0);
    render_backend b={0}; GL_CHECK(render_gl_backend(&b)==RENDER_OK);
    if (getenv("DISPLAY")==NULL || getenv("DISPLAY")[0]=='\0') {
        render_config cfg={.dims={4,3,4,4},.max_width=16,.max_height=12,.max_cells=12,
            .max_pages=1,.max_glyphs=1,.max_atlas_bytes=16};
        edit_arena a; GL_CHECK(edit_arena_init(&a,b.info.state_size+b.info.state_align)==0);
        void *state=edit_arena_alloc(&a,b.info.state_size,b.info.state_align);
        GL_CHECK(init_on_worker(&b,&cfg,state,b.info.state_size)==RENDER_ERR_UNSUPPORTED);
        GL_CHECK(!b.initialized); edit_arena_free(&a);
        puts("gl_test: SKIP no DISPLAY; clean unsupported init verified"); return 0;
    }
    GL_CHECK(gl_failure_test()==0);
    const char *selected=getenv("EDIT_GL_UPLOAD");
    if (selected!=NULL) GL_CHECK(gl_readback_unit_test()==0);
    else {
        const char *modes[]={"subdata","orphan","persistent"};
        GL_CHECK(gl_readback_unit_test()==0); /* legacy default stays covered */
        for (size_t i=0;i<3;i++) {
            GL_CHECK(setenv("EDIT_GL_UPLOAD",modes[i],1)==0);
            GL_CHECK(gl_readback_unit_test()==0);
        }
        GL_CHECK(unsetenv("EDIT_GL_UPLOAD")==0);
    }
    int pixels_result=gl_pixels_test();
    GL_CHECK(pixels_result==0 || pixels_result==2);
    if (pixels_result==2) {
        puts("gl_test: PASS (snapshot law 2, failure cleanup, frozen grid/adapter checks; native lifecycle SKIP)");
        return 0;
    }
    puts("gl_test: frozen allocator check scoped to input->submit; present/event excluded");
    if (!pixels_only) GL_CHECK(gl_frozen_main()==0);
    puts(pixels_only ? "gl_test: PASS (4x3 readback, exact blend, inverse, underline, wide, retained damage; conformance not run)" : "gl_test: PASS (4x3 readback, exact blend, inverse, underline, wide, retained damage, frozen conformance)");
    return 0;
}
