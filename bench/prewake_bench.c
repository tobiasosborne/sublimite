/* P3.7: common executable protocol, private production render-path access.
 * This observes EGL swap + fence on :99; it never synthesises Present/T6. */
#include "harness.h"
#include "font/font.h"
#include "prewake/prewake.h"
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#define render_gl_backend prewake_gl_backend
#define gl_present_complete prewake_gl_complete
#define gl_buffer_mode prewake_gl_mode
#define gl_device_name prewake_gl_device
#define gl_displayed_msc prewake_gl_msc
#define gl_read_pixels prewake_gl_readback
#include "../src/gl/gl.c"
#undef render_gl_backend
#undef gl_present_complete
#undef gl_buffer_mode
#undef gl_device_name
#undef gl_displayed_msc
#undef gl_read_pixels
#ifndef PREWAKE_IMPL
#define PREWAKE_IMPL "prewake/a/prewake.c"
#endif
#include PREWAKE_IMPL
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"prewake_bench:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
typedef struct prewake_fixture {
    gl_state gl;
    plat platform;
    render_backend backend;
    render_config config;
    render_grid grid;
    edit_arena arena;
    render_glyph glyphs[95];
    render_atlas_page page;
    uint64_t dirty[4];
    int init_result;
    uint32_t frame;
    uint64_t warmups;
    bool real_display;
} prewake_fixture;
static uint64_t prewake_clock(clockid_t id)
{
    struct timespec ts;
    if (clock_gettime(id,&ts)!=0) return 0;
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}
static int prewake_sleep_until(uint64_t deadline)
{
    struct timespec ts={(time_t)(deadline/UINT64_C(1000000000)),(long)(deadline%UINT64_C(1000000000))};
    int rc;
    do { rc=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&ts,NULL); } while (rc==EINTR);
    return rc==0 ? 0 : -1;
}
static int prewake_spin(void *ctx,uint64_t ns)
{
    (void)ctx;
    uint64_t end=bench_now_ns()+ns;
    while (bench_now_ns()<end) { __asm__ volatile("" ::: "memory"); }
    return 0;
}
static int prewake_fence(gl_state *s)
{
    if (s->fence==NULL) return -1;
    uint64_t end=bench_now_ns()+UINT64_C(3000000000);
    GLenum status=GL_TIMEOUT_EXPIRED;
    while (status==GL_TIMEOUT_EXPIRED && bench_now_ns()<end)
        status=s->gClientWaitSync(s->fence,GL_SYNC_FLUSH_COMMANDS_BIT,UINT64_C(1000000));
    s->gDeleteSync(s->fence); s->fence=NULL;
    return status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED ? 0 : -1;
}
static int prewake_warm(void *ctx)
{
    prewake_fixture *f=ctx; gl_state *s=&f->gl;
    f->warmups++;
    if (!gl_bind(s)) return -1;
    /* Touch the retained production draw state without changing pixels. */
    s->gBindFramebuffer(GL_FRAMEBUFFER,s->fbo);
    s->gUseProgram(s->program); s->gBindVertexArray(s->vao);
    s->gDrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,0);
    s->fence=s->gFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); s->gFlush();
    if (prewake_fence(s)!=0) return -1;
    return s->gGetError()==GL_NO_ERROR ? 0 : -1;
}
static void *prewake_init_worker(void *ctx)
{
    prewake_fixture *f=ctx; gl_state *s=&f->gl;
    s->platform=&f->platform; s->dims=f->config.dims;
    s->max_width=f->config.max_width; s->max_height=f->config.max_height;
    f->init_result=gl_context_init(s,&f->config,0);
    if (f->init_result==0) {
        if (!s->eMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)) f->init_result=-1;
        else { s->bound=false; if (!s->eReleaseThread()) f->init_result=-1; }
    }
    return NULL;
}
static int prewake_setup(prewake_fixture *f)
{
    const font_ascii_atlas *a=font_ascii_atlas_for_px(15);
    REQUIRE(a!=NULL);
    render_dims dims={2880u/a->cell.cell_w,1800u/a->cell.cell_h,a->cell.cell_w,a->cell.cell_h};
    size_t count=(size_t)dims.cols*dims.rows;
    REQUIRE(edit_arena_init(&f->arena,count*sizeof(render_cell)+4096)==0);
    render_cell *cells=edit_arena_alloc(&f->arena,count*sizeof(render_cell),_Alignof(render_cell));
    REQUIRE(cells!=NULL && render_grid_init(&f->grid,dims,cells,count,f->dirty,4)==0);
    for (size_t i=0;i<count;i++) cells[i]=(render_cell){32u+(uint32_t)(i%95), (uint32_t)(i%95),0xc0c0c0,0x202020,0,0};
    for (uint32_t i=0;i<95;i++) f->glyphs[i]=(render_glyph){32u+i,0,i*dims.cell_w,0,dims.cell_w,dims.cell_h};
    f->page=(render_atlas_page){a->pixels,a->pixels_len,(size_t)dims.cell_w*95,dims.cell_w*95,dims.cell_h};
    f->grid.pages=&f->page; f->grid.page_count=1; f->grid.glyphs=f->glyphs; f->grid.glyph_count=95;
    f->config=(render_config){.dims=dims,.max_width=2880,.max_height=1800,.max_cells=count,.max_glyphs=95,.max_pages=1,.max_atlas_bytes=a->pixels_len};
    plat_config pc={.title="P3.7 prewake TRACK",.width=dims.cols*dims.cell_w,.height=dims.rows*dims.cell_h,.work_eventfd=-1};
    REQUIRE(plat_init(&f->platform,&pc)==0);
    plat_map(&f->platform); plat_set_blink(&f->platform,0); plat_set_repeat(&f->platform,0,0);
    pthread_t worker;
    REQUIRE(pthread_create(&worker,NULL,prewake_init_worker,f)==0);
    REQUIRE(pthread_join(worker,NULL)==0 && f->init_result==0);
    f->backend.state=&f->gl;
    return 0;
}
static int prewake_frame(prewake_fixture *f,uint64_t *elapsed,size_t *allocs)
{
    uint64_t start=bench_now_ns();
    f->grid.cells[0].bg^=UINT32_C(0x010101);
    render_strip full={0,f->grid.dims.rows};
    edit_malloc_guard_begin();
    int rc=gl_submit(&f->backend,&f->grid,&full,1);
    *allocs=edit_malloc_guard_end();
    if (rc!=0 || *allocs!=0 || gl_present(&f->backend,++f->frame)!=0 || prewake_fence(&f->gl)!=0) return -1;
    *elapsed=bench_now_ns()-start;
    return 0;
}
static int prewake_stamp(char *status,size_t cap,char *power,size_t power_cap,double *load)
{
    FILE *fp=fopen("/sys/class/power_supply/BAT0/status","r");
    if (fp==NULL || fgets(status,(int)cap,fp)==NULL) { if (fp!=NULL) fclose(fp); return -1; }
    fclose(fp); status[strcspn(status,"\n")]='\0';
    const char *tag=strcmp(status,"Discharging")==0 ? "bat" :
        strcmp(status,"Charging")==0 || strcmp(status,"Full")==0 || strcmp(status,"Not charging")==0 ? "AC" : "unknown";
    (void)snprintf(power,power_cap,"%s",tag);
    fp=fopen("/proc/loadavg","r");
    if (fp==NULL) return -1;
    int rc=fscanf(fp,"%lf",load); fclose(fp);
    return rc==1 ? 0 : -1;
}
typedef struct prewake_idle_sink {
    prewake_fixture *fixture;
    prewake_state state;
    uint64_t events;
    int error;
} prewake_idle_sink;
static void prewake_idle_event(void *ctx,const plat_event *event)
{
    prewake_idle_sink *sink=ctx;
    prewake_ops ops={sink->fixture,prewake_warm,prewake_spin};
    sink->events++;
    int rc=prewake_hint(&sink->state,event,bench_now_ns(),&ops);
    if (rc!=0) sink->error=rc;
}
static int prewake_idle(prewake_fixture *f)
{
    prewake_idle_sink sink={.fixture=f,.state={.last_activity_ns=bench_now_ns()}};
    plat_callbacks cb={.ud=&sink,.on_event=prewake_idle_event};
    /* Give the app the required idle interval before observing post-idle
     * wakeups. A 100 ms startup drain can leave native initialization replies
     * outstanding on the loaded box. Do not reset activity after this interval:
     * hints must actually be eligible during the measured no-event workload.
     * No periodic timer is enabled; only external observation deadlines run. */
    REQUIRE(plat_run_for(&f->platform,&cb,15000)==PLAT_OK && sink.error==0);
    uint64_t settle_wakes=f->platform.iterations;
    sink.events=0;
    uint64_t before=f->platform.iterations,warmups=f->warmups;
    char status[64],power[16]; double load;
    REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
    uint64_t cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID),start=bench_now_ns();
    REQUIRE(plat_run_for(&f->platform,&cb,11000)==PLAT_OK && sink.error==0);
    uint64_t elapsed=bench_now_ns()-start;
    cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu;
    uint64_t wakes=f->platform.iterations-before;
    printf("G11 scope=UI_event_loop idle_ns=%llu process_cpu_ns=%llu wakeups=%llu hint_events=%llu warmups=%llu settle_wakeups=%llu power=(M)[%s]%s load1=%.2f (G)wakeups=0\n",
        (unsigned long long)elapsed,(unsigned long long)cpu,(unsigned long long)wakes,
        (unsigned long long)sink.events,(unsigned long long)(f->warmups-warmups),
        (unsigned long long)settle_wakes,power,
        f->real_display ? "[real-display, proxy only]" : "[xvfb, indicative only]",load);
    REQUIRE(wakes==0 && sink.events==0 && f->warmups==warmups);
    return 0;
}
int main(int argc,char **argv)
{
    (void)setvbuf(stdout,NULL,_IOLBF,0);
    bool real=argc==3 && strcmp(argv[2],"--real-display")==0;
    const char *display=getenv("DISPLAY"),*edit_display=getenv("EDIT_DISPLAY");
    REQUIRE(display!=NULL && edit_display!=NULL && strcmp(display,edit_display)==0);
    if (real) {
        const char *allow=getenv("EDIT_ALLOW_REAL_DISPLAY");
        REQUIRE(allow!=NULL && strcmp(allow,"1")==0 && display[0]!='\0');
    } else {
        REQUIRE(getenv("EDIT_ALLOW_REAL_DISPLAY")==NULL && strcmp(display,":99")==0);
    }
    if ((argc!=2 && !real) || (strcmp(argv[1],"--protocol")!=0 && strcmp(argv[1],"--display-check")!=0)) {
        fprintf(stderr,"usage: %s --protocol|--display-check [--real-display] (commands N/H/I on stdin; caller supplies >=15s idle)\n",argv[0]); return 1;
    }
    if (strcmp(argv[1],"--display-check")==0) {
        printf("DISPLAY_CHECK display=%s mode=%s PASS (no window)\n",display,real ? "coordinator-opt-in" : "xvfb"); return 0;
    }
    char status[64],power[16]; double load;
    REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
    prewake_fixture f={.real_display=real}; REQUIRE(prewake_setup(&f)==0);
    uint64_t ns; size_t allocs;
    for (unsigned i=0;i<3;i++) REQUIRE(prewake_frame(&f,&ns,&allocs)==0);
    printf("READY renderer=%s surface=%ux%u cells=%zu power=(M)[%s] evidence=%s load1=%.2f status=%s\n",f.gl.device_name,f.platform.width,f.platform.height,(size_t)f.grid.dims.cols*f.grid.dims.rows,power,real ? "real-display-proxy" : "xvfb-indicative-only",load,status);
    uint64_t last=bench_now_ns();
    int ch;
    while ((ch=getchar())!=EOF) {
        if (ch=='\n') continue;
        if (ch=='I') { REQUIRE(prewake_idle(&f)==0); last=bench_now_ns(); continue; }
        REQUIRE(ch=='N' || ch=='H');
        uint64_t idle=bench_now_ns()-last;
        REQUIRE(prewake_stamp(status,sizeof status,power,sizeof power,&load)==0);
        uint64_t hint_wall=0,hint_cpu=0,key_due=bench_now_ns();
        if (ch=='H') {
            prewake_state state={.last_activity_ns=last};
            plat_event event={.kind=PLAT_EV_MOTION};
            prewake_ops ops={&f,prewake_warm,prewake_spin};
            uint64_t cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID),start=bench_now_ns();
            key_due=start+UINT64_C(50000000);
            REQUIRE(prewake_hint(&state,&event,start,&ops)==0);
            hint_wall=bench_now_ns()-start; hint_cpu=prewake_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu;
            if (bench_now_ns()<key_due) REQUIRE(prewake_sleep_until(key_due)==0);
        }
        REQUIRE(prewake_frame(&f,&ns,&allocs)==0);
        uint64_t key_ns=bench_now_ns()-key_due; last=bench_now_ns();
        struct rusage ru; REQUIRE(getrusage(RUSAGE_SELF,&ru)==0);
        printf("SAMPLE condition=%c idle_ns=%llu frame_ns=%llu key_ns=%llu hint_wall_ns=%llu hint_cpu_ns=%llu rss_kib=%ld minflt=%ld majflt=%ld allocations=%zu power=(M)[%s] load1=%.2f status=%s\n",ch,(unsigned long long)idle,(unsigned long long)ns,(unsigned long long)key_ns,(unsigned long long)hint_wall,(unsigned long long)hint_cpu,ru.ru_maxrss,ru.ru_minflt,ru.ru_majflt,allocs,power,load,status);
    }
    gl_release(&f.gl); plat_shutdown(&f.platform); edit_arena_free(&f.arena);
    return 0;
}
