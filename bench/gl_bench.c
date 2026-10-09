#include "gl/gl_driver.h"
#include "font/font.h"
#include "harness.h"
#include "render_pace.h"
#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#define GL_BENCH_CHECK(c) do { if (!(c)) { fprintf(stderr,"gl_bench:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
typedef struct gl_bench_hooks { uint64_t device_ns, complete_ns, submit_ns, present_ns, fence_ns; uint32_t frame; } gl_bench_hooks;
static void gl_bench_device(void *u,uint32_t id,uint64_t ns)
{ gl_bench_hooks *h=u; h->device_ns=ns; h->frame=id; render_trace_device_done(NULL,id,ns); }
static void gl_bench_complete(void *u,uint32_t id,uint64_t ns)
{ gl_bench_hooks *h=u; h->complete_ns=ns; h->frame=id; render_trace_present_complete(NULL,id,ns); }
static void gl_bench_load(char *load,size_t capacity)
{
    bench__copy(load,capacity,"unknown");
    FILE *f=fopen("/proc/loadavg","r");
    if (f==NULL) return;
    char value[32];
    if (fscanf(f,"%31s",value)==1) bench__copy(load,capacity,value);
    fclose(f);
}
static int gl_bench_report(const char *name,const bench_samples *samples,uint64_t gate50,uint64_t gate99)
{
    if (getenv("EDIT_GL_UPLOAD")==NULL) return bench_report(name,samples,gate50,gate99);
    uint64_t p50=bench_p50(samples),p99=bench_p99(samples),lo=0,hi=0;
    bench_ci95(samples,0.50,&lo,&hi);
    bool pass=samples->n!=0 && samples->dropped==0 && (gate50==0 || p50<=gate50) && (gate99==0 || p99<=gate99);
    char load[32]; gl_bench_load(load,sizeof load);
    printf("BENCH name=%s n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=%llu gate_p99=%llu pass=%d status=TRACK evidence=(M) power=%s load1=%s upload=%s\n",
        name,samples->n,(unsigned long long)p50,(unsigned long long)p99,(unsigned long long)lo,(unsigned long long)hi,
        (unsigned long long)gate50,(unsigned long long)gate99,pass ? 1 : 0,bench_evidence_tag(),load,getenv("EDIT_GL_UPLOAD"));
    return pass ? 0 : 1;
}
static uint32_t gl_bench_random(uint32_t *seed)
{ uint32_t x=*seed; x^=x<<13; x^=x>>17; x^=x<<5; *seed=x; return x; }
static void gl_bench_fill(render_cell *cells,size_t count,uint32_t *seed)
{
    for (size_t i=0;i<count;i++) {
        uint32_t r=gl_bench_random(seed), slot=(r%10u<3u) ? 0u : 1u+(r%94u);
        cells[i]=(render_cell){32u+slot,slot,gl_bench_random(seed)&0xffffffu,
            gl_bench_random(seed)&0xffffffu,0,0};
    }
}
static int gl_bench_snapshot(gl_driver *d,render_grid *g,uint32_t id,bool full,uint64_t *ingress)
{
    edit_malloc_guard_begin();
    *ingress=bench_now_ns();
    const char *upload=getenv("EDIT_GL_UPLOAD");
    bool direct=upload!=NULL && strcmp(upload,"persistent")==0;
    int rc=direct ? gl_cells_acquire(d->backend,g,!full) : RENDER_OK;
    if (rc==RENDER_OK) rc=render_frame_begin(g,id);
    /* Same synthetic layout workload in all experiments, written directly
     * into the CPU grid or mapped lease. Start timing before lease/layout. */
    if (rc==RENDER_OK && upload!=NULL) {
        uint32_t seed=UINT32_C(0x37216e9d)^id;
        if (full) gl_bench_fill(g->cells,(size_t)g->dims.cols*g->dims.rows,&seed);
        else g->cells[(size_t)(g->dims.rows/2)*g->dims.cols].bg=gl_bench_random(&seed)&0xffffffu;
    }
    if (rc==RENDER_OK) rc=full ? render_mark_full(g) : render_mark_rows(g,g->dims.rows/2,1);
    render_strip strips[120]; size_t count=0;
    if (rc==RENDER_OK) rc=render_dirty_strips(g,strips,120,&count);
    if (upload==NULL) *ingress=bench_now_ns(); /* preserve legacy timing */
    if (rc==RENDER_OK) rc=direct ? gl_cells_submit(d->backend,g,strips,count) : render_backend_submit(d->backend,g,strips,count);
    size_t allocations=edit_malloc_guard_end();
    return allocations==0 ? rc : RENDER_ERR_DEVICE;
}
static int gl_bench_frame(gl_driver *d,render_grid *g,gl_bench_hooks *hooks,uint32_t *id,bool full,uint64_t *elapsed)
{
    (*id)++;
    hooks->device_ns=0; hooks->complete_ns=0;
    uint64_t ingress=0;
    int rc=gl_bench_snapshot(d,g,*id,full,&ingress); if (rc!=RENDER_OK) return rc;
    uint64_t submit_done=bench_now_ns();
    rc=gl_driver_finish(d,*id); if (rc!=RENDER_OK) return rc;
    if (hooks->device_ns<ingress || hooks->frame!=*id) return RENDER_ERR_DEVICE;
    *elapsed=hooks->device_ns-ingress;
    hooks->submit_ns=submit_done-ingress;
    hooks->present_ns=d->backend->submitted_ns-submit_done;
    hooks->fence_ns=hooks->device_ns-d->backend->submitted_ns;
    return RENDER_OK;
}
/* Sum only the module object's .text sections; the path follows the Makefile's
 * build/bench -> build/rel/src/gl relation. ELF inspection is startup-only. */
static uint64_t gl_bench_text_bytes(const char *exe)
{
    char path[4096]; size_t len=strlen(exe);
    if (len>=sizeof path) return 0;
    memcpy(path,exe,len+1); char *slash=strrchr(path,'/'); if (slash==NULL) return 0;
    *slash='\0'; slash=strrchr(path,'/'); if (slash==NULL) return 0;
    *slash='\0';
    size_t base=strlen(path); const char suffix[]="/rel/src/gl/gl.o";
    if (base+sizeof suffix>sizeof path) return 0;
    memcpy(path+base,suffix,sizeof suffix);
    FILE *f=fopen(path,"rb"); if (f==NULL) return 0;
    Elf64_Ehdr eh; Elf64_Shdr sh[128]; char names[4096]; uint64_t bytes=0;
    if (fread(&eh,sizeof eh,1,f)!=1 || memcmp(eh.e_ident,ELFMAG,SELFMAG)!=0 || eh.e_shnum>128 ||
        eh.e_shstrndx>=eh.e_shnum || eh.e_shoff>LONG_MAX || eh.e_shentsize!=sizeof(Elf64_Shdr)) goto done;
    if (fseek(f,(long)eh.e_shoff,SEEK_SET)!=0 || fread(sh,sizeof(Elf64_Shdr),eh.e_shnum,f)!=eh.e_shnum) goto done;
    Elf64_Shdr table=sh[eh.e_shstrndx];
    if (table.sh_size>sizeof names || table.sh_offset>LONG_MAX ||
        fseek(f,(long)table.sh_offset,SEEK_SET)!=0 || fread(names,1,(size_t)table.sh_size,f)!=table.sh_size) goto done;
    for (uint16_t i=0;i<eh.e_shnum;i++)
        if (sh[i].sh_name+5u<table.sh_size && memcmp(names+sh[i].sh_name,".text",5)==0) bytes+=sh[i].sh_size;
done:
    fclose(f); return bytes;
}
typedef struct gl_pace_rig {
    gl_driver *driver; render_grid *grid; gl_bench_hooks *hooks;
    uint32_t *id, *seed;
} gl_pace_rig;
static int gl_pace_frame(void *user, bool scrolling, render_pace_frame *out)
{
    gl_pace_rig *r = user;
    render_grid *g = r->grid; render_backend *b = r->driver->backend;
    r->hooks->device_ns = 0; r->hooks->complete_ns = 0;
    uint32_t id = ++*r->id;
    uint64_t start = bench_now_ns();
    edit_malloc_guard_begin();
    const char *upload = getenv("EDIT_GL_UPLOAD");
    bool direct = upload != NULL && strcmp(upload,"persistent") == 0;
    /* Submit revokes a mapped lease. Acquire before reading retained cells or
     * mutating the next scroll; baseline initializes a fresh region in full. */
    int rc = direct ? gl_cells_acquire(b,g,scrolling) : RENDER_OK;
    if (rc == RENDER_OK && upload != NULL && !scrolling) {
        *r->seed = UINT32_C(0x37216e9d);
        gl_bench_fill(g->cells,(size_t)g->dims.cols * g->dims.rows,r->seed);
    }
    if (rc == RENDER_OK && scrolling) {
        size_t n = (size_t)g->dims.cols * g->dims.rows;
        memmove(g->cells,g->cells + g->dims.cols,(n - g->dims.cols) * sizeof *g->cells);
        gl_bench_fill(g->cells + n - g->dims.cols,g->dims.cols,r->seed);
    }
    if (rc == RENDER_OK) rc = render_frame_begin(g,id);
    if (rc == RENDER_OK) rc = render_mark_full(g);
    render_strip strips[120]; size_t count = 0;
    if (rc == RENDER_OK) rc = render_dirty_strips(g,strips,120,&count);
    uint64_t submit_start = bench_now_ns();
    if (rc == RENDER_OK) rc = direct ? gl_cells_submit(b,g,strips,count) : render_backend_submit(b,g,strips,count);
    uint64_t submit_end = bench_now_ns();
    size_t allocations = edit_malloc_guard_end();
    if (rc != RENDER_OK || allocations) return -1;
    uint64_t present_start = bench_now_ns();
    rc = render_backend_present(b,id);
    uint64_t present_end = bench_now_ns();
    if (rc != RENDER_OK) return -1;
    uint64_t deadline = present_end + UINT64_C(3000000000);
    while (b->active) {
        if (gl_driver_pump(r->driver) != RENDER_OK || bench_now_ns() > deadline) return -1;
    }
    if (r->hooks->frame != id || r->hooks->device_ns < present_start ||
        r->hooks->complete_ns < r->hooks->device_ns) return -1;
    out->msc = gl_displayed_msc(b); out->msc_available = out->msc != 0;
    out->complete_ns = r->hooks->complete_ns;
    out->stage[0] = submit_start - start;
    out->stage[1] = submit_end - submit_start;
    out->stage[2] = present_end - present_start;
    out->stage[3] = r->hooks->device_ns > present_end ? r->hooks->device_ns - present_end : 0;
    out->stage[4] = r->hooks->complete_ns - r->hooks->device_ns;
    out->stage[5] = r->hooks->device_ns - start;
    return 0;
}
static void gl_scroll_track(gl_driver *driver, render_grid *grid, gl_bench_hooks *hooks,
                            uint32_t *id, uint32_t *seed, const char *name)
{
    /* Caller initialized this context with EGL swap interval 1. */
    const char *names[] = {"grid_mutation_damage", "submit", "present_swap",
        "swap_return_to_T5", "T5_to_completion", "ingress_to_T5"};
    gl_pace_rig r = {driver,grid,hooks,id,seed};
    render_pace_run(stdout,name,names,sizeof names / sizeof names[0],gl_pace_frame,&r);
}
static int gl_bench_run(uint32_t px,bool quick,bool scroll_only,bool upload_only)
{
    char track_name[80]; snprintf(track_name,sizeof track_name,"A_egl_scroll_600_%upx",px);
    const char *skip_reason = render_pace_skip_reason(getenv("DISPLAY"));
    if (skip_reason && !upload_only) {
        render_pace_skip(stdout,track_name,skip_reason);
        if (scroll_only) return 0;
    }
    const font_ascii_atlas *atlas=font_ascii_atlas_for_px(px); GL_BENCH_CHECK(atlas!=NULL);
    render_dims dims={2880u/atlas->cell.cell_w,1800u/atlas->cell.cell_h,atlas->cell.cell_w,atlas->cell.cell_h};
    size_t ncells=(size_t)dims.cols*dims.rows;
    edit_arena arena; GL_BENCH_CHECK(edit_arena_init(&arena,ncells*sizeof(render_cell)+4096)==0);
    render_cell *cells=edit_arena_alloc(&arena,ncells*sizeof(render_cell),_Alignof(render_cell));
    uint64_t dirty[2]={0}; render_grid grid;
    GL_BENCH_CHECK(render_grid_init(&grid,dims,cells,ncells,dirty,2)==RENDER_OK);
    render_glyph glyphs[95];
    for (uint32_t i=0;i<95;i++) glyphs[i]=(render_glyph){32u+i,0,i*dims.cell_w,0,dims.cell_w,dims.cell_h};
    render_atlas_page page={atlas->pixels,atlas->pixels_len,(size_t)dims.cell_w*95,dims.cell_w*95,dims.cell_h};
    grid.pages=&page; grid.page_count=1; grid.glyphs=glyphs; grid.glyph_count=95;
    uint32_t seed=0x37216e9du; gl_bench_fill(cells,ncells,&seed);
    render_backend backend={0}; gl_driver driver; gl_bench_hooks hooks={0};
    render_config cfg={.dims=dims,.max_width=2880,.max_height=1800,.max_cells=ncells,
        .max_glyphs=95,.max_pages=1,.max_atlas_bytes=atlas->pixels_len,
        .hooks={gl_bench_device,gl_bench_complete,&hooks}};
    int rc=gl_driver_prepare(&driver,&backend,&cfg);
    if (rc!=RENDER_OK) {
        if (!skip_reason && !upload_only) render_pace_skip(stdout,track_name,"X11_unavailable");
        printf("BENCH name=egl_init_%upx status=SKIP reason=X11_unavailable result=%d power=%s\n",px,rc,bench_evidence_tag());
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return scroll_only ? 0 : 2;
    }
    rc=gl_driver_init(&driver);
    if (rc!=RENDER_OK) {
        if (!skip_reason && !upload_only) render_pace_skip(stdout,track_name,"EGL_or_matching_Present_unsupported");
        printf("BENCH name=egl_init_%upx status=SKIP reason=EGL_or_matching_Present_unsupported result=%d power=%s\n",px,rc,bench_evidence_tag());
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return scroll_only ? 0 : 2;
    }
    uint32_t id=0; uint64_t elapsed=0;
    if (scroll_only) {
        gl_scroll_track(&driver,&grid,&hooks,&id,&seed,track_name);
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return 0;
    }
    if (getenv("EDIT_GL_UPLOAD")!=NULL) puts("egl_bench: layout=synthetic_cell_fill G3=layout_to_T5 G1=layout_to_T4; editor ingress/mutation outside this microbench");
    printf("egl_bench: px=%u VBO=%s surface=2880x1800 cells=%zu timing=T5_after_SwapBuffers minimap=absent allowance_ns=180000(E) indicative=concurrent_workers\n",
           px,gl_buffer_mode(&backend),ncells);
    printf("egl_bench: renderer=%s native_window=%ux%u swap_interval=%s T6=Present_PIXMAP_Complete\n",gl_device_name(&backend),driver.platform.width,driver.platform.height,getenv("EDIT_GL_SWAP_INTERVAL"));
    puts("egl_bench: every frame asserts 0 counted allocations from begin/damage through submit; present/event outside law 2");
    uint64_t sample_buf[10000]; bench_samples samples;
    bench_samples_init(&samples,sample_buf,2000); (void)bench_add(&samples,driver.init_ns);
    char name[80]; int failed=0;
    if (px==15) failed=gl_bench_report("init_cost",&samples,0,0);
    else printf("egl_bench: init_cost_30px_ns=%llu TRACK (M)%s\n",(unsigned long long)driver.init_ns,bench_evidence_tag());
    size_t warm=quick ? 20 : 200, n=quick ? 100 : 2000;
    for (size_t i=0;i<warm;i++) GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
    bench_samples_init(&samples,sample_buf,2000);
    uint64_t submit_total=0,present_total=0,fence_total=0;
    for (size_t i=0;i<n;i++) {
        GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
        submit_total+=hooks.submit_ns; present_total+=hooks.present_ns; fence_total+=hooks.fence_ns;
        (void)bench_add(&samples,elapsed);
    }
    (void)snprintf(name,sizeof name,"full_frame_warm_%upx",px);
    bool experiment=getenv("EDIT_GL_UPLOAD")!=NULL;
    bool indicative=experiment || strcmp(bench_evidence_tag(),"[AC]")!=0;
    printf("egl_bench: %s reference_gate_p50_ns=5000000 reference_gate_p99_ns=5560000(G) verdict=%s\n",
        name,experiment ? "TRACK_shared_box" : (indicative ? "TRACK_battery" : "gate_pending_quiet_box"));
    failed|=gl_bench_report(name,&samples,indicative ? 0 : 5000000,indicative ? 0 : 5560000);
    printf("egl_bench: mean_submit_ns=%llu mean_present_ns=%llu mean_fence_poll_ns=%llu (M)%s\n",
        (unsigned long long)(submit_total/n),(unsigned long long)(present_total/n),(unsigned long long)(fence_total/n),bench_evidence_tag());
    if (px==15) {
        bench_samples_init(&samples,sample_buf,2000);
        for (size_t i=0;i<n;i++) {
            if (!experiment) cells[(size_t)(dims.rows/2)*dims.cols].bg=gl_bench_random(&seed)&0xffffffu;
            GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,false,&elapsed)==RENDER_OK);
            /* G1 ends at T4. Full frame G3 continues to device fence T5. */
            if (experiment) elapsed=hooks.submit_ns+hooks.present_ns;
            (void)bench_add(&samples,elapsed);
        }
        if (experiment) puts("egl_bench: typing_row reference_gate_p50_ns=1000000 reference_gate_p99_ns=2000000(G) timing=layout_to_T4 verdict=TRACK_shared_box");
        failed|=gl_bench_report("typing_row",&samples,0,0);
        if (upload_only) { gl_driver_cleanup(&driver); edit_arena_free(&arena); return failed; }
        if (skip_reason == NULL) {
            const char *old_interval=getenv("EDIT_GL_SWAP_INTERVAL");
            char saved_interval[8]; bench__copy(saved_interval,sizeof saved_interval,old_interval);
            gl_driver_cleanup(&driver);
            GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL","1",1)==0);
            backend=(render_backend){0};
            GL_BENCH_CHECK(gl_driver_prepare(&driver,&backend,&cfg)==RENDER_OK && gl_driver_init(&driver)==RENDER_OK);
            GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
            uint64_t previous=gl_displayed_msc(&backend), misses=0, duplicates=0;
            size_t scroll_n=quick ? 100 : 10000;
            bench_samples_init(&samples,sample_buf,10000);
            for (size_t i=0;i<scroll_n;i++) {
                memmove(cells,cells+dims.cols,(ncells-dims.cols)*sizeof(render_cell));
                gl_bench_fill(cells+ncells-dims.cols,dims.cols,&seed);
                GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
                uint64_t msc=gl_displayed_msc(&backend);
                uint64_t gap=0;
                if (msc<=previous) { duplicates++; gap=1; }
                if (msc>previous+1) { gap=msc-previous-1; misses+=gap; }
                (void)bench_add(&samples,gap);
                previous=msc;
            }
            printf("egl_bench: scroll_frames=%zu displayed_msc_misses=%llu (M)%s hard_max=0(G)\n",scroll_n,(unsigned long long)misses,bench_evidence_tag());
            /* Per-frame gap percentiles use the shared harness. The aggregate
             * zero-miss hard maximum also rejects rare gaps below p99 rank. */
            uint64_t lo=0,hi=0; bench_ci95(&samples,0.50,&lo,&hi);
            printf("BENCH name=scroll_10k n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=0 gate_p99=0 misses=%llu duplicate_msc=%llu gate_max=0 max_gap=%llu status=%s pass=%d power=%s\n",
                samples.n,(unsigned long long)bench_p50(&samples),(unsigned long long)bench_p99(&samples),
                (unsigned long long)lo,(unsigned long long)hi,(unsigned long long)misses,
                (unsigned long long)duplicates,(unsigned long long)bench_p(&samples,1.0),indicative ? "TRACK" : "GATE",
                indicative || (misses==0 && duplicates==0 && samples.n==scroll_n && samples.dropped==0) ? 1 : 0,bench_evidence_tag());
            if (!indicative && (misses!=0 || duplicates!=0)) failed=1;
            gl_driver_cleanup(&driver);
            GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL",saved_interval,1)==0);
            backend=(render_backend){0};
            GL_BENCH_CHECK(gl_driver_prepare(&driver,&backend,&cfg)==RENDER_OK && gl_driver_init(&driver)==RENDER_OK);
            GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
        } else puts("BENCH name=scroll_10k status=SKIP reason=Xvfb_:99_has_no_real_vblank gate=G3z verdict=unmeasured");
        if (!quick) {
            bench_samples_init(&samples,sample_buf,2000);
            for (size_t i=0;i<5;i++) {
                struct timespec idle={15,0};
                while (nanosleep(&idle,&idle)!=0) { }
                GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
                (void)bench_add(&samples,elapsed);
            }
            failed|=gl_bench_report("first_frame_after_idle",&samples,0,0);
        } else puts("BENCH name=first_frame_after_idle status=SKIP reason=quick_mode");
    }
    if (skip_reason == NULL) {
        const char *old_interval = getenv("EDIT_GL_SWAP_INTERVAL");
        char saved_interval[8]; bench__copy(saved_interval,sizeof saved_interval,old_interval);
        gl_driver_cleanup(&driver);
        GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL","1",1)==0);
        backend = (render_backend){0};
        rc = gl_driver_prepare(&driver,&backend,&cfg);
        if (rc == RENDER_OK) rc = gl_driver_init(&driver);
        if (rc == RENDER_OK) gl_scroll_track(&driver,&grid,&hooks,&id,&seed,track_name);
        else render_pace_skip(stdout,track_name,"vsync_context_unavailable");
        GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL",saved_interval,1)==0);
    }
    gl_driver_cleanup(&driver); edit_arena_free(&arena);
    return failed;
}
int main(int argc,char **argv)
{
    (void)setvbuf(stdout,NULL,_IOLBF,0);
    if (argc == 2 && !strcmp(argv[1],"--pace-self-check")) return render_pace_self_check();
    const char *upload=getenv("EDIT_GL_UPLOAD");
    bool quick=false, focus=false, valid=true;
    bool scroll_only = argc == 2 && strcmp(argv[1],"--scroll-track")==0;
    if (!scroll_only) for (int i=1;i<argc;i++) {
        if (!quick && strcmp(argv[i],"--quick")==0) quick=true;
        else if (!focus && strcmp(argv[i],"--upload-only")==0) focus=true;
        else valid=false;
    }
    if (!valid) {
        fprintf(stderr,"usage: %s [--quick [--upload-only]|--upload-only [--quick]|--scroll-track|--pace-self-check]; EDIT_GL_VBO=orphan|persistent; EDIT_GL_UPLOAD=subdata|orphan|persistent\n",argv[0]);
        return 1;
    }
    if (upload!=NULL && strcmp(upload,"subdata")!=0 && strcmp(upload,"orphan")!=0 && strcmp(upload,"persistent")!=0) {
        fprintf(stderr,"gl_bench: invalid EDIT_GL_UPLOAD=%s\n",upload); return 1;
    }
    /* A selected experiment defaults to its focused comparison. An explicit
     * scroll request still runs both paced sizes, using the selected upload. */
    bool upload_only=!scroll_only && (focus || upload!=NULL);
    if (scroll_only) GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL","1",1)==0);
    if (getenv("EDIT_GL_SWAP_INTERVAL")==NULL) GL_BENCH_CHECK(setenv("EDIT_GL_SWAP_INTERVAL","0",0)==0);
    trace_init(); GL_BENCH_CHECK(trace_thread_register()>=0);
    char power[32]; bench_battery_status(power,sizeof power);
    char load[32]; gl_bench_load(load,sizeof load);
    printf("POWER status=%s evidence=(M)%s load1=%s upload=%s verdict=TRACK\n",power,bench_evidence_tag(),load,upload!=NULL ? upload : "legacy");
    if (scroll_only) {
        int first = gl_bench_run(15,false,true,false);
        int second = gl_bench_run(30,false,true,false);
        return first | second;
    }
    uint64_t text_bytes=gl_bench_text_bytes(argv[0]);
    printf("BENCH name=binary_size n=1 p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=0 gate_p99=0 text_bytes=%llu units=bytes status=TRACK pass=1 evidence=(M) power=%s load1=%s upload=%s\n",
        (unsigned long long)text_bytes,(unsigned long long)text_bytes,(unsigned long long)text_bytes,
        (unsigned long long)text_bytes,(unsigned long long)text_bytes,bench_evidence_tag(),load,upload!=NULL ? upload : "legacy");
    int a=gl_bench_run(15,quick,false,upload_only); if (a==2 || upload_only) return a;
    int b=gl_bench_run(30,quick,false,upload_only); if (b==2) return 2;
    return a|b;
}
