/* G3 minimap slice, (G) p99 <=500000 ns, 1800-pixel strip. Shared box TRACK.
 * Warm fill includes cached summary rendering, viewport overlay and damage.
 * A separate gated TRACK row reports UI publication/fill after real edits;
 * worker preparation and index refresh are explicitly outside its timer. */
#include "minimap/minimap.h"
#include "trace/trace.h"
#include "harness.h"
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#define SAMPLES 2000u
#define HEIGHT 1800u
#define COLS 10u
#define WIDTH 6u
#define GATE_NS UINT64_C(500000)
#define NEED(c) do { if (!(c)) { fprintf(stderr,"minimap_bench:%d: %s\n",__LINE__,#c); return 1; } } while (0)
typedef struct flat { uint8_t *p; size_t n, calls; } flat;
static size_t span(void *ctx, uint64_t off, const uint8_t **p)
{
    flat *f = ctx; f->calls++;
    if (off >= f->n) return 0;
    *p = f->p + off; return f->n - (size_t)off;
}
static bool space(uint8_t c) { return c == ' ' || (c >= 9 && c <= 13); }
static int reference(const minimap *m, const flat *f)
{
    /* Fresh computation from bytes, with independent grouping/count loops. */
    uint64_t sums[HEIGHT] = {0}, counts[HEIGHT] = {0};
    if (!m->sampled) {
        uint64_t line = 0, ink = 0;
        for (size_t j = 0; j <= f->n; j++) {
            if (j == f->n || f->p[j] == '\n') {
                size_t row = (size_t)(line++ / m->lines_per_row);
                NEED(row < HEIGHT); sums[row] += ink > 80 ? 80 : ink;
                counts[row]++; ink = 0;
            } else ink += !space(f->p[j]);
        }
        for (uint32_t r = 0; r < HEIGHT; r++) {
            uint64_t den = counts[r] ? sums[r]*255/(counts[r]*80) : 0;
            NEED(m->rows[r].density == den && m->rows[r].ink == sums[r]);
        }
    } else {
        size_t chunks = f->n/LINEIDX_CHUNK + (f->n%LINEIDX_CHUNK != 0);
        for (uint32_t r = 0; r < m->active_rows; r++) {
            size_t samples = m->active_rows < MINIMAP_MAX_SAMPLES ? m->active_rows : MINIMAP_MAX_SAMPLES;
            if (chunks < samples) samples = chunks;
            size_t group = samples*r/m->active_rows;
            size_t start = chunks*group/samples*LINEIDX_CHUNK;
            NEED(m->rows[r].first_byte == start);
            size_t end = start + MINIMAP_SAMPLE_BYTES;
            if (end > f->n) end = f->n;
            size_t first = start;
            if (start) { while (first < end && f->p[first] != '\n') first++; if (first < end) first++; else first = start; }
            uint64_t ink = 0;
            for (size_t j = first; j < end && f->p[j] != '\n'; j++) ink += !space(f->p[j]);
            if (ink > 80) ink = 80;
            NEED(m->rows[r].ink == ink && m->rows[r].density == ink*255/80);
        }
    }
    return 0;
}
static void discard(const work_msg *msg, void *ctx) { (void)msg; (void)ctx; }
static int run(flat *f, const char *name, work_pool *pool)
{
    lineidx_src src = {f,f->n,span,NULL}; lineidx *idx = lineidx_create(f->n); NEED(idx);
    minimap_row *rows = calloc(HEIGHT,sizeof *rows), *worker_rows = calloc(HEIGHT,sizeof *worker_rows);
    render_cell *cells = calloc(HEIGHT*COLS,sizeof *cells);
    uint64_t dirty[(HEIGHT+63)/64], values[SAMPLES]; minimap m, worker; render_grid g;
    NEED(rows && worker_rows && cells); NEED(minimap_init(&m,rows,HEIGHT) == 0);
    NEED(minimap_init(&worker,worker_rows,HEIGHT) == 0);
    NEED(render_grid_init(&g,(render_dims){COLS,HEIGHT,1,1},cells,HEIGHT*COLS,dirty,(HEIGHT+63)/64) == 0);
    NEED(render_frame_begin(&g,1) == 0);
    minimap_style style = {0x101010,0x999999,0x224466,0x663300};
    minimap_input in = {src,1,1,false}; minimap_target target;
    size_t pending_calls=f->calls;
    NEED(minimap_fill_cached(&m,&in,&g,4,WIDTH,0,1,&style) == 0);
    NEED(minimap_hit_approx(&in,HEIGHT,HEIGHT-1,&target) == 0 && !target.exact && target.byte == f->n);
    NEED(cells[4].bg != style.background && f->calls == pending_calls);
    printf("minimap_%s incomplete-index sidebar / byte drag: ok\n",name);
    NEED(lineidx_build_start(idx,pool,&src) == 0);
    uint64_t deadline = bench_now_ns()+UINT64_C(120000000000);
    while (lineidx_building(idx)) {
        (void)lineidx_poll(idx);
        NEED(bench_now_ns() < deadline);
        struct pollfd pfd = {work_pool_eventfd(pool),POLLIN,0}; (void)poll(&pfd,1,1);
        (void)work_mailbox_drain(pool,discard,NULL);
    }
    (void)lineidx_poll(idx); NEED(lineidx_complete(idx));
    in.lines=lineidx_line_count(idx).value; in.index_ready=true;
    NEED(minimap_prepare(&worker,&in,HEIGHT) == 0);
    NEED(minimap_publish(&m,&in,&worker) == 0);
    bench_samples samples; bench_samples_init(&samples,values,SAMPLES);
    char power[32], load[32] = "unknown";
    bench_battery_status(power,sizeof power);
    FILE *lf = fopen("/proc/loadavg","r"); if (lf) { if (fscanf(lf,"%31s",load) != 1) strcpy(load,"unknown"); fclose(lf); }
    const char *tag = bench__tag_from_power(power);
    for (uint32_t i = 0; i < SAMPLES+20; i++) {
        uint64_t top = (uint64_t)i*101%in.lines;
        uint64_t t = bench_now_ns();
        int rc = minimap_fill_cached(&m,&in,&g,4,WIDTH,top,100,&style);
        uint64_t elapsed = bench_now_ns()-t; NEED(rc == 0);
        if (i >= 20) NEED(bench_add(&samples,elapsed) == 0);
    }
    uint64_t p50 = bench_p50(&samples), p99 = bench_p99(&samples);
    printf("BENCH minimap_%s TRACK (M)%s load1=%s p50_ns=%llu p99_ns=%llu (G)gate_p99_ns=%llu pass=%d height_px=1800 width_cols=6 lines=%llu sampled=%d n=%u\n",
           name,tag,load,(unsigned long long)p50,(unsigned long long)p99,
           (unsigned long long)GATE_NS,p99<=GATE_NS,(unsigned long long)in.lines,m.sampled,SAMPLES);
    NEED(reference(&m,f) == 0);
    bench_samples refill; uint64_t refresh_values[200];
    bench_samples_init(&refill,refresh_values,200);
    for (uint32_t i = 0; i < 200; i++) {
        NEED(lineidx_edit(idx,1,1,1) == 0);
        f->p[1] = f->p[1] == ' ' ? 'x' : ' ';
        in.revision++; in.index_ready = false; NEED(minimap_stale(&m,&in));
        (void)lineidx_refresh(idx,&src); NEED(lineidx_complete(idx));
        in.lines = lineidx_line_count(idx).value; in.index_ready = true;
        NEED(minimap_prepare(&worker,&in,HEIGHT) == 0);
        uint64_t t = bench_now_ns();
        NEED(minimap_publish(&m,&in,&worker) == 0);
        int rc = minimap_fill_cached(&m,&in,&g,4,WIDTH,0,100,&style);
        uint64_t elapsed = bench_now_ns()-t; NEED(rc == 0);
        NEED(bench_add(&refill,elapsed) == 0);
    }
    uint64_t refill_p50 = bench_p50(&refill), refill_p99 = bench_p99(&refill);
    printf("BENCH minimap_%s_after_edit TRACK (M)%s load1=%s p50_ns=%llu p99_ns=%llu (G)gate_p99_ns=%llu pass=%d n=200 index_refresh_and_worker_prepare_outside_timer=1 publish_inside_timer=1\n",
           name,tag,load,(unsigned long long)refill_p50,(unsigned long long)refill_p99,
           (unsigned long long)GATE_NS,refill_p99<=GATE_NS);
    NEED(reference(&m,f) == 0);
    /* Real same-size mutation including line-index invalidation and refresh. */
    NEED(lineidx_edit(idx,1,1,1) == 0); f->p[1] = f->p[1] == ' ' ? 'x' : ' ';
    in.revision++; in.index_ready = false; NEED(minimap_stale(&m,&in));
    size_t calls = f->calls;
    NEED(minimap_fill_cached(&m,&in,&g,4,WIDTH,0,100,&style) == 0 && m.stale && calls == f->calls);
    (void)lineidx_refresh(idx,&src); NEED(lineidx_complete(idx));
    in.lines = lineidx_line_count(idx).value; in.index_ready = true;
    NEED(minimap_stale(&m,&in));
    NEED(minimap_prepare(&worker,&in,HEIGHT) == 0);
    NEED(minimap_publish(&m,&in,&worker) == 0);
    NEED(minimap_fill_cached(&m,&in,&g,4,WIDTH,0,100,&style) == 0 && !minimap_stale(&m,&in));
    NEED(reference(&m,f) == 0);
    printf("minimap_%s stale-after-edit / fresh-refill-reference: ok %s load1=%s\n",name,tag,load);
    minimap_fini(&m); minimap_fini(&worker); free(worker_rows); free(rows); free(cells); lineidx_destroy(idx);
    return p99 > GATE_NS || refill_p99 > GATE_NS;
}
static int cold_sample_check(void)
{
    char path[]="/tmp/edit-457.22-cold-XXXXXX";
    int fd=mkstemp(path); NEED(fd>=0); NEED(unlink(path)==0);
    uint8_t block[65536];
    for (size_t i=0;i<sizeof block;i++) block[i]=i%64==63?'\n':'x';
    for (unsigned i=0;i<64;i++) NEED(write(fd,block,sizeof block)==(ssize_t)sizeof block);
    NEED(fdatasync(fd)==0);
    size_t n=sizeof block*64;
    uint8_t *p=mmap(NULL,n,PROT_READ,MAP_PRIVATE,fd,0); NEED(p!=MAP_FAILED);
    flat f={p,n,0}; minimap_input in={{&f,n,span,NULL},65537,1,true};
    minimap_row rows[HEIGHT], worker_rows[HEIGHT]; minimap m, worker;
    render_cell cells[HEIGHT*2]={0}; uint64_t dirty[(HEIGHT+63)/64]; render_grid g;
    minimap_style style={0,0xffffff,0x224466,0x663300};
    NEED(minimap_init(&m,rows,HEIGHT)==0 && minimap_init(&worker,worker_rows,HEIGHT)==0);
    NEED(render_grid_init(&g,(render_dims){2,HEIGHT,1,1},cells,HEIGHT*2,dirty,(HEIGHT+63)/64)==0);
    NEED(render_frame_begin(&g,1)==0);
    NEED(minimap_prepare(&worker,&in,HEIGHT)==0 && minimap_publish(&m,&in,&worker)==0);
    NEED(madvise(p,n,MADV_DONTNEED)==0);
    NEED(posix_fadvise(fd,0,(off_t)n,POSIX_FADV_DONTNEED)==0);
    char power[32],load[32]="unknown"; bench_battery_status(power,sizeof power);
    FILE *lf=fopen("/proc/loadavg","r");
    if (lf) { if (fscanf(lf,"%31s",load)!=1) strcpy(load,"unknown"); fclose(lf); }
    struct rusage before, after; NEED(getrusage(RUSAGE_THREAD,&before)==0);
    size_t calls=f.calls;
    NEED(minimap_fill_cached(&m,&in,&g,1,1,0,100,&style)==0);
    in.revision++;
    NEED(minimap_fill_cached(&m,&in,&g,1,1,0,100,&style)==0 && m.stale);
    NEED(getrusage(RUSAGE_THREAD,&after)==0);
    long faults=after.ru_majflt-before.ru_majflt;
    printf("minimap_cold_samples TRACK foreground_major_faults=%ld source_calls=%zu (M)%s load1=%s eviction=requested pass=%d\n",
           faults,f.calls-calls,bench__tag_from_power(power),load,faults==0 && f.calls==calls);
    NEED(faults==0 && f.calls==calls);
    NEED(munmap(p,n)==0); NEED(close(fd)==0); return 0;
}
int main(void)
{
    trace_init(); work_pool pool; NEED(work_pool_init(&pool,1,0) == 0);
    uint8_t *small = malloc((size_t)MINIMAP_SMALL_BYTES); NEED(small);
    for (size_t i = 0; i < MINIMAP_SMALL_BYTES; i++) small[i] = i%65 == 64 ? '\n' : i%9 == 0 ? ' ' : 'x';
    flat s = {small,(size_t)MINIMAP_SMALL_BYTES,0}; int result = run(&s,"small_64k",&pool); free(small);
    int fd = open("/tmp/edit-corpus/log_1g.txt",O_RDONLY); NEED(fd >= 0);
    struct stat st; NEED(fstat(fd,&st) == 0 && st.st_size > 1);
    size_t n = (size_t)st.st_size;
    uint8_t *p = mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0); close(fd); NEED(p != MAP_FAILED);
    flat big = {p,n,0}; result |= run(&big,"log_1g",&pool);
    munmap(p,n); work_pool_shutdown(&pool);
    result |= cold_sample_check();
    return result;
}
