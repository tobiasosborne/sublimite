#include "gl/gl_driver.h"
#include "gl_gate.h"
#include "font/font.h"
#include "harness.h"
#include "render_pace.h"
#include <elf.h>
#include <limits.h>
#include <stdatomic.h>
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
/* Gated row: the verdict comes from gl_gate_judge, which enforces the limits
 * under [AC], [bat] and [unknown] power alike (the tag is evidence, not a
 * switch), refuses a verdict below the required sample count, and labels
 * partial-operation rows. Experiments (EDIT_GL_UPLOAD) stay TRACK. */
static int gl_bench_gate_row(const char *name,const char *scenario,bench_samples *samples,size_t required,
                             const gl_gate_limit *lim,bool partial)
{
    if (getenv("EDIT_GL_UPLOAD")!=NULL) return gl_bench_report(name,samples,0,0);
    uint64_t p50=bench_p50(samples),p99=bench_p99(samples),lo=0,hi=0;
    bench_ci95(samples,0.50,&lo,&hi);
    const char *tag=bench_evidence_tag();
    gl_gate_verdict v=gl_gate_judge(tag,samples->n,samples->dropped,required,p50,p99,lim,partial);
    char load[32]; gl_bench_load(load,sizeof load);
    printf("BENCH name=%s scenario=%s n=%zu required_n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=%llu gate_p99=%llu/%llu "
           "status=%s pass=%d g_claim=%s evidence=(M) power=%s load1=%s\n",
        name,scenario,samples->n,required,(unsigned long long)p50,(unsigned long long)p99,(unsigned long long)lo,(unsigned long long)hi,
        (unsigned long long)lim->p50_ns,(unsigned long long)lim->p99_num,(unsigned long long)lim->p99_den,
        gl_gate_name(v),v==GL_GATE_PASS||v==GL_GATE_PASS_PARTIAL ? 1 : 0,
        v==GL_GATE_PASS ? "yes" : "no",tag,load);
    return gl_gate_exit(v);
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
    *ingress=bench_now_ns(); /* ingress: before lease, mutation, damage and strips (review gl-1 MAJOR 10) */
    const char *upload=getenv("EDIT_GL_UPLOAD");
    bool direct=upload!=NULL && strcmp(upload,"persistent")==0;
    int rc=direct ? gl_cells_acquire(d->backend,g,!full) : RENDER_OK;
    if (rc==RENDER_OK) rc=render_frame_begin(g,id);
    /* The mutation is part of the timed operation in every mode: a full
     * frame is a one-row scroll (shift + new bottom row) or, in the upload
     * experiments, a synthetic full fill; a typing frame touches one cell. */
    if (rc==RENDER_OK) {
        uint32_t seed=UINT32_C(0x37216e9d)^id;
        size_t n=(size_t)g->dims.cols*g->dims.rows;
        if (full && upload!=NULL) gl_bench_fill(g->cells,n,&seed);
        else if (full) {
            memmove(g->cells,g->cells+g->dims.cols,(n-g->dims.cols)*sizeof *g->cells);
            gl_bench_fill(g->cells+n-g->dims.cols,g->dims.cols,&seed);
        } else g->cells[(size_t)(g->dims.rows/2)*g->dims.cols].bg=gl_bench_random(&seed)&0xffffffu;
    }
    if (rc==RENDER_OK) rc=full ? render_mark_full(g) : render_mark_rows(g,g->dims.rows/2,1);
    render_strip strips[120]; size_t count=0;
    if (rc==RENDER_OK) rc=render_dirty_strips(g,strips,120,&count);
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
 * build/bench -> build/rel/src/gl relation. ELF inspection is startup-only and
 * bounds-checked by gl_gate_elf_text_bytes (unit-tested). */
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
    uint64_t bytes=0;
    if (!gl_gate_elf_text_bytes(f,&bytes)) bytes=0;
    fclose(f); return bytes;
}
/* Bulk-worker load (perf §0.2: G1/G3 are verified with index, find and save
 * queued and one active). The work pool admits one bulk worker, so a running
 * job holds it for the whole window (it loops in 256 KiB chunks, polling
 * work_should_stop each chunk, far inside the 5 ms rule) and the other jobs
 * stay queued behind it. Overlap is verified, not assumed. */
#define GL_BULK_BYTES (32u<<20)
#define GL_BULK_CHUNK (256u<<10)
typedef struct gl_bulk_job { uint8_t *buf; int kind; _Atomic uint64_t chunks; } gl_bulk_job;
typedef struct gl_bulk {
    work_pool pool; bool live; uint8_t *mem; gl_bulk_job jobs[3]; work_handle h[3]; size_t njobs;
    uint64_t chunks_at_start;
} gl_bulk;
static volatile uint64_t gl_bulk_sink;
static void gl_bulk_run(work_ctx *c)
{
    gl_bulk_job *j=c->arg; size_t pos=0; uint64_t h=1469598103934665603ull;
    while (!work_should_stop(c)) {
        uint8_t *p=j->buf+pos;
        if (j->kind==0) { for (size_t i=0;i<GL_BULK_CHUNK;i+=8) { uint64_t w; memcpy(&w,p+i,8); h=(h^w)*1099511628211ull; } } /* index: hash scan */
        else if (j->kind==1) { const void *f=memchr(p,0xff,GL_BULK_CHUNK); h+=f!=NULL; }                                   /* find: byte scan */
        else { memcpy(j->buf+((pos+(GL_BULK_BYTES-GL_BULK_CHUNK)/2)%(GL_BULK_BYTES-GL_BULK_CHUNK)),p,GL_BULK_CHUNK/2); h+=p[0]; }                          /* save: copy-out */
        pos=(pos+GL_BULK_CHUNK)%(GL_BULK_BYTES-GL_BULK_CHUNK);
        atomic_fetch_add_explicit(&j->chunks,1,memory_order_relaxed);
    }
    gl_bulk_sink=h;
}
static uint64_t gl_bulk_chunks(const gl_bulk *b)
{ uint64_t t=0; for (size_t i=0;i<b->njobs;i++) t+=atomic_load_explicit(&b->jobs[i].chunks,memory_order_relaxed); return t; }
static int gl_bulk_start(gl_bulk *b,size_t njobs)
{
    memset(b,0,sizeof *b);
    b->mem=malloc(GL_BULK_BYTES);
    if (b->mem==NULL) return -1;
    memset(b->mem,0x41,GL_BULK_BYTES);
    if (work_pool_init(&b->pool,1,0)!=0) { free(b->mem); b->mem=NULL; return -1; }
    b->live=true; b->njobs=njobs;
    for (size_t i=0;i<njobs;i++) {
        b->jobs[i].buf=b->mem; b->jobs[i].kind=(int)i; atomic_init(&b->jobs[i].chunks,0);
        b->h[i]=work_submit(&b->pool,(work_job){gl_bulk_run,&b->jobs[i],1,WORK_BULK});
        if (b->h[i].epoch==0) return -1;
    }
    /* Wait until the active job demonstrably runs. */
    uint64_t deadline=bench_now_ns()+UINT64_C(2000000000);
    while (gl_bulk_chunks(b)==0) { if (bench_now_ns()>deadline) return -1; struct timespec ts={0,1000000}; nanosleep(&ts,NULL); }
    b->chunks_at_start=gl_bulk_chunks(b);
    return 0;
}
/* True when the background work progressed during the window, exactly one
 * job ran, and every other job is still queued (not finished). */
static bool gl_bulk_overlapped(const gl_bulk *b)
{
    if (gl_bulk_chunks(b)<=b->chunks_at_start+1) return false;
    for (size_t i=0;i<b->njobs;i++) {
        bool ran=atomic_load_explicit(&b->jobs[i].chunks,memory_order_relaxed)!=0;
        if (work_handle_finished(&b->pool,b->h[i])) return false;
        if (i!=0 && ran) return false;
    }
    return true;
}
static void gl_bulk_stop(gl_bulk *b)
{
    if (b->live) { for (size_t i=0;i<b->njobs;i++) work_cancel(&b->pool,b->h[i]); work_pool_shutdown(&b->pool); }
    free(b->mem); memset(b,0,sizeof *b);
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
/* One scenario: `n` frames of `full` (scroll-one-row full frame, G3) or typing
 * (one cell, G1), collecting T5 (device fence) and T4 (submit+present return). */
static int gl_bench_measure(gl_driver *d,render_grid *g,gl_bench_hooks *hooks,uint32_t *id,bool full,size_t n,
                            bench_samples *t5,bench_samples *t4)
{
    uint64_t elapsed=0;
    for (size_t i=0;i<n;i++) {
        GL_BENCH_CHECK(gl_bench_frame(d,g,hooks,id,full,&elapsed)==RENDER_OK);
        (void)bench_add(t5,elapsed);
        (void)bench_add(t4,hooks->submit_ns+hooks->present_ns);
    }
    return 0;
}
/* Rows for the three operations of a scenario. G3 rows compare the renderer-only
 * time with the budget minus the minimap allowance and never claim G3. */
static int gl_bench_rows(const char *scenario,const char *suffix,bench_samples *t5,bench_samples *t4,bool full,size_t required)
{
    gl_gate_limit g3={GL_GATE_G3_P50_NS,GL_GATE_G3_P99_NUM,GL_GATE_G3_P99_DEN};
    gl_gate_limit g3r=gl_gate_reduce_for_minimap(&g3);
    gl_gate_limit g1={1000000u,2000000u,1u};
    char name[96]; int failed=0;
    if (full) {
        (void)snprintf(name,sizeof name,"full_frame_T5_%s",suffix);
        failed|=gl_bench_gate_row(name,scenario,t5,required,&g3r,true);
    } else {
        (void)snprintf(name,sizeof name,"typing_T4_%s",suffix);
        failed|=gl_bench_gate_row(name,scenario,t4,required,&g1,true);
        (void)snprintf(name,sizeof name,"typing_T5_%s",suffix);
        failed|=gl_bench_report(name,t5,0,0); /* T5 is tracked, not a G1 endpoint */
    }
    return failed;
}
static int gl_bench_run(uint32_t px,bool quick,bool scroll_only,bool upload_only,bool bulk_only)
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
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return 2;
    }
    rc=gl_driver_init(&driver);
    if (rc!=RENDER_OK) {
        if (!skip_reason && !upload_only) render_pace_skip(stdout,track_name,"EGL_or_matching_Present_unsupported");
        printf("BENCH name=egl_init_%upx status=SKIP reason=EGL_or_matching_Present_unsupported result=%d power=%s\n",px,rc,bench_evidence_tag());
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return 2;
    }
    uint32_t id=0; uint64_t elapsed=0;
    if (scroll_only) {
        gl_scroll_track(&driver,&grid,&hooks,&id,&seed,track_name);
        gl_driver_cleanup(&driver); edit_arena_free(&arena); return 0;
    }
    if (getenv("EDIT_GL_UPLOAD")!=NULL) puts("egl_bench: layout=synthetic_cell_fill G3=layout_to_T5 G1=layout_to_T4; editor ingress/mutation outside this microbench");
    printf("egl_bench: px=%u VBO=%s surface=2880x1800 cells=%zu timing=T5_after_SwapBuffers minimap=absent allowance_ns=180000(E)_deducted_from_G3_budget rows=partial_operation_no_G3_claim\n",
           px,gl_buffer_mode(&backend),ncells);
    printf("egl_bench: renderer=%s native_window=%ux%u swap_interval=%s T6=Present_PIXMAP_Complete\n",gl_device_name(&backend),driver.platform.width,driver.platform.height,getenv("EDIT_GL_SWAP_INTERVAL"));
    puts("egl_bench: every frame asserts 0 counted allocations from begin/damage through submit; present/event outside law 2");
    static uint64_t sample_buf[10000], sample_buf4[10000]; bench_samples samples, samples4;
    bench_samples_init(&samples,sample_buf,2000); (void)bench_add(&samples,driver.init_ns);
    int failed=0;
    bool experiment=getenv("EDIT_GL_UPLOAD")!=NULL;
    if (px==15) failed=gl_bench_report("init_cost",&samples,0,0);
    else printf("egl_bench: init_cost_30px_ns=%llu TRACK (M)%s\n",(unsigned long long)driver.init_ns,bench_evidence_tag());
    /* Required sample count (perf §4): 10 000 per interaction scenario. Quick
     * mode takes fewer, so its gate rows are REFUSED, never PASS. */
    const size_t required=GL_GATE_SAMPLES_INTERACTION;
    size_t warm=quick ? 20 : 200, n=quick ? 100 : required;
    char name[96];
    if (!bulk_only) {
        for (size_t i=0;i<warm;i++) GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
        bench_samples_init(&samples,sample_buf,required); bench_samples_init(&samples4,sample_buf4,required);
        GL_BENCH_CHECK(gl_bench_measure(&driver,&grid,&hooks,&id,true,n,&samples,&samples4)==0);
        (void)snprintf(name,sizeof name,"full_frame_warm_%upx",px);
        if (experiment) { puts("egl_bench: experiment rows are TRACK_shared_box"); failed|=gl_bench_report(name,&samples,0,0); }
        else {
            gl_gate_limit g3={GL_GATE_G3_P50_NS,GL_GATE_G3_P99_NUM,GL_GATE_G3_P99_DEN};
            gl_gate_limit g3r=gl_gate_reduce_for_minimap(&g3);
            printf("egl_bench: %s G3 p50<=%llu p99<=%llu/%llu ns exact; renderer-only budget (minimap allowance deducted) p50<=%llu p99<=%llu/%llu\n",
                name,(unsigned long long)g3.p50_ns,(unsigned long long)g3.p99_num,(unsigned long long)g3.p99_den,
                (unsigned long long)g3r.p50_ns,(unsigned long long)g3r.p99_num,(unsigned long long)g3r.p99_den);
            failed|=gl_bench_gate_row(name,"warm_scroll_one_row",&samples,required,&g3r,true);
        }
        printf("egl_bench: %s T4_p50_ns=%llu T4_p99_ns=%llu (M)%s\n",name,
            (unsigned long long)bench_p50(&samples4),(unsigned long long)bench_p99(&samples4),bench_evidence_tag());
    }
    if (px==15) {
        if (!bulk_only) {
            bench_samples_init(&samples,sample_buf,required); bench_samples_init(&samples4,sample_buf4,required);
            GL_BENCH_CHECK(gl_bench_measure(&driver,&grid,&hooks,&id,false,n,&samples,&samples4)==0);
            if (experiment) { puts("egl_bench: typing_row reference_gate_p50_ns=1000000 reference_gate_p99_ns=2000000(G) timing=layout_to_T4 verdict=TRACK_shared_box");
                bench_samples_init(&samples,sample_buf,required);
                for (size_t i=0;i<samples4.n;i++) (void)bench_add(&samples,samples4.v[i]);
                failed|=gl_bench_report("typing_row",&samples,0,0); }
            else failed|=gl_bench_rows("typing","warm",&samples,&samples4,false,required);
        }
        if (!experiment) {
            /* Bulk-worker scenarios: one active job, the rest queued behind it. */
            static const struct { const char *name; size_t jobs; } bulks[]={{"bulk_active",1},{"bulk_queued3",3}};
            for (size_t k=0;k<2;k++) {
                gl_bulk bulk;
                if (gl_bulk_start(&bulk,bulks[k].jobs)!=0) {
                    printf("BENCH name=%s status=REFUSED reason=bulk_worker_did_not_start pass=0\n",bulks[k].name);
                    gl_bulk_stop(&bulk); failed=1; continue;
                }
                for (int full=1;full>=0;full--) {
                    bench_samples_init(&samples,sample_buf,required); bench_samples_init(&samples4,sample_buf4,required);
                    uint64_t c0=gl_bulk_chunks(&bulk); bulk.chunks_at_start=c0;
                    GL_BENCH_CHECK(gl_bench_measure(&driver,&grid,&hooks,&id,full!=0,n,&samples,&samples4)==0);
                    if (!gl_bulk_overlapped(&bulk)) {
                        printf("BENCH name=%s_%s status=REFUSED reason=bulk_overlap_not_verified pass=0\n",bulks[k].name,full ? "full" : "typing");
                        failed=1; continue;
                    }
                    printf("egl_bench: %s bulk_chunks_during_window=%llu jobs=%zu active=1 queued=%zu\n",bulks[k].name,
                        (unsigned long long)(gl_bulk_chunks(&bulk)-c0),bulks[k].jobs,bulks[k].jobs-1);
                    failed|=gl_bench_rows(bulks[k].name,bulks[k].name,&samples,&samples4,full!=0,required);
                }
                gl_bulk_stop(&bulk);
            }
        }
        if (bulk_only) { gl_driver_cleanup(&driver); edit_arena_free(&arena); return failed; }
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
                /* scroll mutation happens inside gl_bench_frame's timed region */
                GL_BENCH_CHECK(gl_bench_frame(&driver,&grid,&hooks,&id,true,&elapsed)==RENDER_OK);
                uint64_t msc=gl_displayed_msc(&backend);
                uint64_t gap=0;
                if (msc<=previous) { duplicates++; gap=1; }
                if (msc>previous+1) { gap=msc-previous-1; misses+=gap; }
                (void)bench_add(&samples,gap);
                previous=msc;
            }
            printf("egl_bench: scroll_frames=%zu displayed_msc_misses=%llu (M)%s hard_max=0(G)\n",scroll_n,(unsigned long long)misses,bench_evidence_tag());
            uint64_t lo=0,hi=0; bench_ci95(&samples,0.50,&lo,&hi);
            gl_gate_verdict cv=gl_gate_cadence(bench_evidence_tag(),samples.n,GL_GATE_SAMPLES_INTERACTION,misses,duplicates,samples.dropped);
            if (experiment && cv!=GL_GATE_MISS) cv=GL_GATE_UNKNOWN; /* experiments never qualify */
            printf("BENCH name=scroll_10k n=%zu required_n=%u p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=0 gate_p99=0 misses=%llu duplicate_msc=%llu gate_max=0 max_gap=%llu status=%s pass=%d power=%s\n",
                samples.n,GL_GATE_SAMPLES_INTERACTION,(unsigned long long)bench_p50(&samples),(unsigned long long)bench_p99(&samples),
                (unsigned long long)lo,(unsigned long long)hi,(unsigned long long)misses,
                (unsigned long long)duplicates,(unsigned long long)bench_p(&samples,1.0),gl_gate_name(cv),
                cv==GL_GATE_PASS ? 1 : 0,bench_evidence_tag());
            failed|=gl_gate_exit(cv);
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
            /* G3i: T = 1e9/90 ns p99, 8 ms p50 (provisional GPU wake). */
            gl_gate_limit gi={8000000u,1000000000u,90u};
            gl_gate_limit gir=gl_gate_reduce_for_minimap(&gi);
            failed|=gl_bench_gate_row("first_frame_after_idle","after_idle_15s",&samples,5,&gir,true);
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
/* GL-free check of the bulk-load scenarios: for 1 and 3 jobs the active job
 * makes progress for 200 ms while the others stay queued. */
static int gl_bulk_self_check(void)
{
    static const size_t counts[]={1,3};
    for (size_t k=0;k<2;k++) {
        gl_bulk b;
        GL_BENCH_CHECK(gl_bulk_start(&b,counts[k])==0);
        uint64_t c0=gl_bulk_chunks(&b); b.chunks_at_start=c0;
        struct timespec ts={0,200000000}; nanosleep(&ts,NULL);
        bool ok=gl_bulk_overlapped(&b);
        printf("gl_bench: bulk_self_check jobs=%zu chunks_in_200ms=%llu overlapped=%d\n",counts[k],
            (unsigned long long)(gl_bulk_chunks(&b)-c0),ok ? 1 : 0);
        GL_BENCH_CHECK(ok);
        gl_bulk_stop(&b);
    }
    return 0;
}
int main(int argc,char **argv)
{
    (void)setvbuf(stdout,NULL,_IOLBF,0);
    if (argc == 2 && !strcmp(argv[1],"--pace-self-check")) return render_pace_self_check();
    if (argc == 2 && !strcmp(argv[1],"--bulk-self-check")) return gl_bulk_self_check();
    const char *upload=getenv("EDIT_GL_UPLOAD");
    bool quick=false, focus=false, valid=true, bulk_only=false, help=false;
    bool scroll_only = argc == 2 && strcmp(argv[1],"--scroll-track")==0;
    if (!scroll_only) for (int i=1;i<argc;i++) {
        if (!quick && strcmp(argv[i],"--quick")==0) quick=true;
        else if (!focus && strcmp(argv[i],"--upload-only")==0) focus=true;
        else if (!bulk_only && strcmp(argv[i],"--bulk-only")==0) bulk_only=true;
        else if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) help=true;
        else valid=false;
    }
    if (help || !valid) {
        fprintf(help ? stdout : stderr,
            "usage: %s [--quick] [--upload-only] [--bulk-only] [--scroll-track] [--pace-self-check] [--bulk-self-check] [--help]\n"
            "  (default)        warm full frame, typing (G1 T4 + T5), bulk_active, bulk_queued3, G3z scroll_10k, G3i, paced scroll\n"
            "  --quick          100-frame smoke run: gate rows print status=REFUSED (needs %u samples), never PASS\n"
            "  --bulk-only      only bulk_active (1 background job) and bulk_queued3 (1 active + 2 queued) rows, 15px; the\n"
            "                   background overlap is verified and a row without overlap is REFUSED\n"
            "  --upload-only    EDIT_GL_UPLOAD experiments only (TRACK, never qualifying)\n"
            "  --scroll-track / --pace-self-check  paced scroll track / its self check\n"
            "  --bulk-self-check  GL-free check that bulk_active / bulk_queued3 load really overlaps\n"
            "env: EDIT_GL_VBO=orphan|persistent; EDIT_GL_UPLOAD=subdata|orphan|persistent; DISPLAY must be Xvfb :99, never :0\n"
            "status: PASS | PASS_PARTIAL (renderer-only, no G3 claim) | MISS | UNKNOWN (power unknown) | REFUSED (too few samples)\n"
            "limits are enforced under [AC], [bat] and [unknown]; G3 p99 limit is the exact 1e9/180 ns (printed as num/den)\n"
            "exit: 0 ok, 1 gate miss or error, 2 no GL context (printed as SKIP; no numbers are fabricated)\n",
            argv[0],GL_GATE_SAMPLES_INTERACTION);
        return help ? 0 : 1;
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
        int first = gl_bench_run(15,false,true,false,false);
        int second = gl_bench_run(30,false,true,false,false);
        return first | second;
    }
    uint64_t text_bytes=gl_bench_text_bytes(argv[0]);
    printf("BENCH name=binary_size n=1 p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=0 gate_p99=0 text_bytes=%llu units=bytes status=TRACK pass=1 evidence=(M) power=%s load1=%s upload=%s\n",
        (unsigned long long)text_bytes,(unsigned long long)text_bytes,(unsigned long long)text_bytes,
        (unsigned long long)text_bytes,(unsigned long long)text_bytes,bench_evidence_tag(),load,upload!=NULL ? upload : "legacy");
    int a=gl_bench_run(15,quick,false,upload_only,bulk_only); if (a==2 || upload_only || bulk_only) return a;
    int b=bulk_only ? 0 : gl_bench_run(30,quick,false,upload_only,false); if (b==2) return 2;
    return a|b;
}
