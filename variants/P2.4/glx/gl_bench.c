#include "harness.h"
#include "gl/gl.h"
#include "base/base.h"
#include "font/font.h"
#include "trace/trace.h"
#include "work/work.h"
#include "x11/plat.h"
#include <elf.h>
#include <poll.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define GL_BENCH_INIT UINT32_C(0x474c494e)
typedef struct gl_bench {
    render_backend backend;
    render_config config;
    plat platform;
    work_pool *pool;
    edit_arena arena;
    void *state;
    render_grid grid;
    render_glyph glyphs[95];
    render_atlas_page page;
    uint64_t samples[10000];
    uint64_t t5, t6, msc, init_ns;
    uint32_t next_id;
    int error, init_result;
    bool init_done, pool_ready, platform_ready;
} gl_bench;
typedef struct gl_init_result { int result; uint32_t reserved; uint64_t ns; } gl_init_result;
static void gl_bench_init_worker(work_ctx *ctx)
{
    gl_bench *bench=ctx->arg;
    uint64_t start=bench_now_ns();
    int rc=render_backend_init(&bench->backend,&bench->config,bench->state,bench->backend.info.state_size);
    gl_init_result result={rc,0,bench_now_ns()-start};
    work_msg message={.kind=GL_BENCH_INIT};
    memcpy(message.data,&result,sizeof result); (void)work_publish(ctx,&message);
}
static void gl_bench_init_message(const work_msg *message, void *ud)
{
    gl_bench *bench=ud;
    if (message->kind!=GL_BENCH_INIT) return;
    gl_init_result result; memcpy(&result,message->data,sizeof result);
    bench->init_result=result.result; bench->init_ns=result.ns; bench->init_done=true;
}
static void gl_bench_t5(void *ud, uint32_t id, uint64_t ns)
{ gl_bench *bench=ud; bench->t5=ns; render_trace_device_done(NULL,id,ns); }
static void gl_bench_t6(void *ud, uint32_t id, uint64_t ns)
{ gl_bench *bench=ud; bench->t6=ns; render_trace_present_complete(NULL,id,ns); }
static void gl_bench_event(void *ud, const plat_event *event) { (void)ud; (void)event; }
static void gl_bench_complete(void *ud, uint32_t serial, uint64_t ust, uint64_t msc)
{
    gl_bench *bench=ud; render_backend *b=&bench->backend;
    if (!b->active || !b->presented) return;
    gl_present_message native={serial,0,ust,msc};
    work_msg message={.kind=GL_PRESENT_MSG_KIND}; memcpy(message.data,&native,sizeof native);
    render_event event={RENDER_EVENT_WORK,b->active_frame,0,&message};
    int rc=render_backend_event(b,&event);
    if (rc==RENDER_OK) bench->msc=msc;
    else if (rc!=RENDER_ERR_FRAME) bench->error=rc;
}
static int gl_bench_pump(gl_bench *bench)
{
    render_backend *b=&bench->backend;
    work_msg message={.kind=GL_POLL_MSG_KIND};
    render_event poll_event={RENDER_EVENT_WORK,b->active_frame,0,&message};
    int rc=render_backend_event(b,&poll_event);
    if (rc!=RENDER_OK) return rc;
    plat_callbacks callbacks={.ud=bench,.on_event=gl_bench_event,.on_present_complete=gl_bench_complete};
    if (plat_run_for(&bench->platform,&callbacks,0)!=PLAT_OK) return RENDER_ERR_DEVICE;
    return bench->error;
}
static uint32_t gl_bench_random(uint32_t *seed)
{ *seed=*seed*UINT32_C(1664525)+UINT32_C(1013904223); return *seed; }
static void gl_bench_fill(render_cell *cells, size_t count, uint32_t seed)
{
    for (size_t i=0;i<count;i++) {
        uint32_t v=gl_bench_random(&seed);
        uint32_t slot=(v%10u)<3u?0u:1u+(v/10u)%94u;
        cells[i]=(render_cell){32u+slot,slot,UINT32_C(0x808080)|(gl_bench_random(&seed)&UINT32_C(0x7f7f7f)),
            gl_bench_random(&seed)&UINT32_C(0x3f3f3f),0,0};
    }
}
static int gl_bench_setup(gl_bench *bench, uint32_t px)
{
    memset(bench,0,sizeof *bench); bench->next_id=1;
    const font_ascii_atlas *atlas=font_ascii_atlas_for_px(px);
    if (!atlas) return RENDER_ERR_INIT;
    render_dims dims={2880u/atlas->cell.cell_w,1800u/atlas->cell.cell_h,atlas->cell.cell_w,atlas->cell.cell_h};
    size_t cells=(size_t)dims.cols*dims.rows;
    if (edit_arena_init(&bench->arena,cells*sizeof(render_cell)+sizeof(work_pool)+UINT32_C(1048576))!=0) return RENDER_ERR_INIT;
    bench->pool=edit_arena_alloc(&bench->arena,sizeof(work_pool),_Alignof(work_pool));
    render_cell *storage=edit_arena_alloc(&bench->arena,cells*sizeof(render_cell),_Alignof(render_cell));
    uint64_t *dirty=edit_arena_alloc(&bench->arena,((dims.rows+63u)/64u)*sizeof(uint64_t),_Alignof(uint64_t));
    if (!bench->pool || !storage || !dirty || work_pool_init(bench->pool,1,0)!=0) return RENDER_ERR_INIT;
    bench->pool_ready=true;
    plat_config pc={.title="GLX renderer benchmark",.width=2880,.height=1800,.work_eventfd=-1};
    if (plat_init(&bench->platform,&pc)!=PLAT_OK) return RENDER_ERR_INIT;
    bench->platform_ready=true;
    /* Keep the fixture at the physical target size: a managed oversized
     * window may otherwise be shrunk by the WM's decorations/work area. */
    uint32_t override_redirect=1;
    xcb_change_window_attributes(bench->platform.conn,bench->platform.win,XCB_CW_OVERRIDE_REDIRECT,&override_redirect);
    plat_map(&bench->platform);
    if (render_gl_backend(&bench->backend)!=RENDER_OK) return RENDER_ERR_INIT;
    bench->state=edit_arena_alloc(&bench->arena,bench->backend.info.state_size,bench->backend.info.state_align);
    if (!bench->state) return RENDER_ERR_INIT;
    bench->config=(render_config){.dims=dims,.max_width=2880,.max_height=1800,.max_cells=cells,.max_glyphs=95,
        .max_pages=1,.max_atlas_bytes=atlas->pixels_len,.platform=&bench->platform,.workers=bench->pool,
        .hooks={gl_bench_t5,gl_bench_t6,bench}};
    work_handle handle=work_submit(bench->pool,(work_job){gl_bench_init_worker,bench,1,WORK_BULK});
    if (!handle.epoch) return RENDER_ERR_INIT;
    uint64_t deadline=bench_now_ns()+UINT64_C(10000000000);
    struct pollfd fd={.fd=work_pool_eventfd(bench->pool),.events=POLLIN};
    while (!bench->init_done && bench_now_ns()<deadline) {
        (void)work_mailbox_drain(bench->pool,gl_bench_init_message,bench);
        if (!bench->init_done) (void)poll(&fd,1,5);
    }
    if (!bench->init_done) return RENDER_ERR_INIT;
    if (bench->init_result!=RENDER_OK) return bench->init_result;
    if (render_grid_init(&bench->grid,dims,storage,cells,dirty,(dims.rows+63u)/64u)!=RENDER_OK) return RENDER_ERR_INIT;
    bench->page=(render_atlas_page){atlas->pixels,atlas->pixels_len,(size_t)atlas->cell.cell_w*95u,atlas->cell.cell_w*95u,atlas->cell.cell_h};
    for (uint32_t i=0;i<95;i++) bench->glyphs[i]=(render_glyph){32u+i,0,i*atlas->cell.cell_w,0,atlas->cell.cell_w,atlas->cell.cell_h};
    bench->grid.pages=&bench->page; bench->grid.page_count=1;
    bench->grid.glyphs=bench->glyphs; bench->grid.glyph_count=95;
    gl_bench_fill(storage,cells,UINT32_C(0x47c001));
    return RENDER_OK;
}
static void gl_bench_shutdown(gl_bench *bench)
{
    /* Join startup even on timeout before touching its exclusive backend. */
    if (bench->pool_ready) work_pool_shutdown(bench->pool);
    render_backend_shutdown(&bench->backend);
    if (bench->platform_ready) plat_shutdown(&bench->platform);
    edit_arena_free(&bench->arena);
}
static int gl_bench_frame(gl_bench *bench, bool row, bool scroll, uint64_t *duration)
{
    render_grid *grid=&bench->grid; uint32_t id=bench->next_id++;
    if (render_frame_begin(grid,id)!=RENDER_OK) return RENDER_ERR_FRAME;
    if (scroll) {
        size_t count=(size_t)grid->dims.cols*(grid->dims.rows-1u);
        memmove(grid->cells,grid->cells+grid->dims.cols,count*sizeof(render_cell));
        gl_bench_fill(grid->cells+count,grid->dims.cols,id);
    } else grid->cells[(size_t)(grid->dims.rows/2u)*grid->dims.cols].bg=id&UINT32_C(0x00ffffff);
    render_strip strip={row?grid->dims.rows/2u:0u,row?1u:grid->dims.rows};
    int rc=row?render_mark_rows(grid,strip.first_row,1):render_mark_full(grid);
    if (rc!=RENDER_OK) return rc;
    bench->t5=0; bench->t6=0; bench->msc=0;
    uint64_t ingress=bench_now_ns();
    rc=render_backend_submit(&bench->backend,grid,&strip,1);
    if (rc!=RENDER_OK) return rc;
    uint64_t submit_end=bench_now_ns();
    rc=render_backend_present(&bench->backend,id);
    if (rc!=RENDER_OK) return rc;
    uint64_t present_end=bench_now_ns();
    struct pollfd fd={.fd=xcb_get_file_descriptor(bench->platform.conn),.events=POLLIN};
    uint64_t deadline=ingress+UINT64_C(5000000000);
    while (bench->backend.active) {
        rc=gl_bench_pump(bench); if (rc!=RENDER_OK) return rc;
        if (bench_now_ns()>deadline) return RENDER_ERR_DEVICE;
        if (!bench->backend.active) break;
        if (bench->t5) (void)poll(&fd,1,1);
        else sched_yield();
    }
    if (!bench->t5 || !bench->t6 || !bench->msc) return RENDER_ERR_DEVICE;
    *duration=bench->t5-ingress;
    if (id<4 && getenv("EDIT_GL_BENCH_DEBUG")) fprintf(stderr,"frame=%u submit=%llu present=%llu T5=%llu T6=%llu\n",id,
        (unsigned long long)(submit_end-ingress),(unsigned long long)(present_end-submit_end),
        (unsigned long long)*duration,(unsigned long long)(bench->t6-ingress));
    return RENDER_OK;
}
static uint64_t gl_bench_text_size(void)
{
    char path[4096]; ssize_t n=readlink("/proc/self/exe",path,sizeof path-1);
    if (n<=0) return 0;
    path[n]='\0'; char *last=strrchr(path,'/'); if(!last) return 0; *last='\0';
    last=strrchr(path,'/'); if(!last) return 0;
    const char *suffix="/rel/src/gl/gl.o";
    if ((size_t)(last-path)+strlen(suffix)+1>sizeof path) return 0;
    strcpy(last,suffix);
    FILE *file=fopen(path,"rb"); if(!file) return 0;
    Elf64_Ehdr header; uint64_t bytes=0;
    if (fread(&header,sizeof header,1,file)==1 && !memcmp(header.e_ident,ELFMAG,SELFMAG)) {
        for (uint16_t i=0;i<header.e_shnum;i++) {
            Elf64_Shdr section;
            if (fseek(file,(long)(header.e_shoff+(uint64_t)i*header.e_shentsize),SEEK_SET)!=0 || fread(&section,sizeof section,1,file)!=1) break;
            if (section.sh_type==SHT_PROGBITS && (section.sh_flags&SHF_EXECINSTR)) bytes+=section.sh_size;
        }
    }
    fclose(file); return bytes;
}
static int gl_bench_run(uint32_t px, bool quick, bool skip_idle)
{
    gl_bench bench;
    int rc=gl_bench_setup(&bench,px);
    if (rc!=RENDER_OK) {
        bench_samples failed_init;
        bench_samples_init(&failed_init,bench.samples,10000);
        if (bench.init_done) {
            (void)bench_add(&failed_init,bench.init_ns);
            printf("# TRACK init_cost atlas=%u init_result=%d (failed_init_only)\n",px,rc);
            (void)bench_report("init_cost",&failed_init,0,0);
        }
        const char *names[]={px==15?"full_frame_warm_15px":"full_frame_warm_30px",
                            "typing_row","first_frame_after_idle","scroll_10k"};
        size_t rows=px==15?4u:3u;
        for (size_t i=0;i<rows;i++) printf("BENCH name=%s status=SKIP reason=GLX_init_failed_rc_%d atlas=%u power=%s\n",
            names[i],rc,px,bench_evidence_tag());
        gl_bench_shutdown(&bench); return rc==RENDER_ERR_UNSUPPORTED?0:1;
    }
    printf("# backend=%s atlas=%upx grid=%ux%u no_minimap reserve_minimap_ns=180000(E) concurrent_workers=indicative\n",
        bench.backend.info.name,px,bench.grid.dims.cols,bench.grid.dims.rows);
    bench_samples samples; bench_samples_init(&samples,bench.samples,10000);
    (void)bench_add(&samples,bench.init_ns);
    printf("# TRACK init_cost worker dlopen/context/resources/Present_probe\n"); (void)bench_report("init_cost",&samples,0,0);
    size_t warm=quick?20u:200u, count=quick?100u:2000u; uint64_t duration=0;
    for (size_t i=0;i<warm;i++) if ((rc=gl_bench_frame(&bench,false,false,&duration))!=RENDER_OK) goto fail;
    bench_samples_init(&samples,bench.samples,10000);
    for (size_t i=0;i<count;i++) {
        if ((rc=gl_bench_frame(&bench,false,false,&duration))!=RENDER_OK) goto fail;
        (void)bench_add(&samples,duration);
    }
    bool track=strcmp(bench_evidence_tag(),"[AC]")!=0 || getenv("EDIT_GL_BENCH_TRACK")!=NULL;
    if (track) puts("# TRACK indicative_only G3_reference_ns=5000000/5560000(G)");
    int missed=bench_report(px==15?"full_frame_warm_15px":"full_frame_warm_30px",&samples,track?0u:5000000u,track?0u:5560000u);
    count=quick?100u:2000u; bench_samples_init(&samples,bench.samples,10000);
    for (size_t i=0;i<count;i++) {
        if ((rc=gl_bench_frame(&bench,true,false,&duration))!=RENDER_OK) goto fail;
        (void)bench_add(&samples,duration);
    }
    printf("# TRACK typing_row atlas=%u\n",px); (void)bench_report("typing_row",&samples,0,0);
    if (px==15) {
        count=quick?200u:10000u; bench_samples_init(&samples,bench.samples,10000);
        uint64_t previous=0,first=0,gaps=0; bool valid=true;
        for (size_t i=0;i<count;i++) {
            if ((rc=gl_bench_frame(&bench,false,true,&duration))!=RENDER_OK) goto fail;
            (void)bench_add(&samples,duration);
            if (!first) first=bench.msc;
            if (previous && bench.msc<=previous) valid=false;
            if (previous && bench.msc>previous+1u) gaps+=bench.msc-previous-1u;
            previous=bench.msc;
        }
        uint64_t lo=0,hi=0; bench_ci95(&samples,0.5,&lo,&hi);
        printf("BENCH name=scroll_10k n=%zu p50=%llu p99=%llu ci95=[%llu,%llu] gate_p50=0 gate_p99=0 "
            "gate_misses=0 misses=%llu first_msc=%llu last_msc=%llu pass=%d status=%s power=%s\n",samples.n,
            (unsigned long long)bench_p50(&samples),(unsigned long long)bench_p99(&samples),
            (unsigned long long)lo,(unsigned long long)hi,(unsigned long long)gaps,
            (unsigned long long)first,(unsigned long long)previous,valid&&(track || gaps==0)?1:0,
            valid?(track?"TRACK":(gaps==0?"PASS":"FAIL")):"SKIP_nonmonotonic_MSC",bench_evidence_tag());
        if (!valid || (!track && gaps)) missed=1;
    }
    if (!skip_idle) {
        bench_samples_init(&samples,bench.samples,10000);
        for (size_t i=0;i<5;i++) {
            struct timespec idle={15,0}; while (nanosleep(&idle,&idle)!=0) { }
            if ((rc=gl_bench_frame(&bench,false,false,&duration))!=RENDER_OK) goto fail;
            (void)bench_add(&samples,duration);
        }
        printf("# TRACK first_frame_after_idle atlas=%u provisional_G3i_ns=8000000/11100000\n",px);
        (void)bench_report("first_frame_after_idle",&samples,0,0);
    }
    gl_bench_shutdown(&bench); return missed;
fail:
    fprintf(stderr,"GLX bench frame failed rc=%d frame=%u\n",rc,bench.next_id-1u);
    printf("BENCH name=scroll_10k status=SKIP reason=matching_Present_or_fence_unavailable power=%s\n",bench_evidence_tag());
    gl_bench_shutdown(&bench); return 1;
}
int main(int argc, char **argv)
{
    bool quick=false,skip_idle=false;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--quick")) { quick=true; skip_idle=true; }
        else if (!strcmp(argv[i],"--skip-idle")) skip_idle=true;
        else { fprintf(stderr,"usage: %s [--quick] [--skip-idle] (EDIT_GL_VBO=persistent|orphan)\n",argv[0]); return 2; }
    }
    char power[32]; bench_battery_status(power,sizeof power);
    printf("# power=%s evidence=(M)%s mode=%s\n",power,bench_evidence_tag(),getenv("EDIT_GL_VBO")?getenv("EDIT_GL_VBO"):"orphan"); fflush(stdout);
    if (!getenv("DISPLAY")) { puts("BENCH name=scroll_10k status=SKIP reason=no_DISPLAY"); return 0; }
    trace_init(); if(trace_thread_register()<0) return 1;
    int result=gl_bench_run(15,quick,skip_idle);
    result|=gl_bench_run(30,quick,skip_idle);
    uint64_t bytes=gl_bench_text_size(); bench_samples samples; bench_samples_init(&samples,&bytes,1); (void)bench_add(&samples,bytes);
    printf("# TRACK binary_text unit=bytes\n"); (void)bench_report("binary_text",&samples,0,0);
    return result;
}
