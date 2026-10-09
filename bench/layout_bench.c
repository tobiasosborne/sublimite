/* layout bench (P3.1): viewport -> cell grid. Gate (G): 300 rows x 360 cols full
 * layout p50/p99 <= 150 us on ascii_code.c and log_1g.txt. unicode.txt,
 * malformed.txt, the 120-row screen and the one-row typing relayout are TRACK.
 * Args: [corpus-dir [name-filter]] (default /tmp/edit-corpus). Indicative under concurrent builds. */
#include "layout/layout.h"
#include "utf8/utf8.h"
#include "harness.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>

#define SAMPLES 2000u
#define STARTS 64u
#define GATE_NS 150000u

static int stub_glyph(void *ctx, const uint8_t *c, size_t n, uint32_t w, uint32_t *slot)
{
    (void)ctx; (void)c; (void)n; (void)w;
    *slot = '?' - 0x20u;
    return 0;
}
static void hold(void *c) { (void)c; }
static const piece_map_hooks hooks = { NULL, hold, hold };

typedef struct unicode_bench {
    edit_arena files, storage;
    font_family family;
    font_fallback fallback;
    font_cache cache;
    int ready, result;
} unicode_bench;
static void unicode_job(work_ctx *ctx)
{
    unicode_bench *u = ctx->arg;
    size_t len = 0;
    unsigned char *bytes = font_load_file("vendor/DejaVuSansMono.ttf", &u->files, &len);
    font_t primary;
    EDIT_ASSERT(bytes && font_init(&primary, bytes, len) == FONT_OK && font_set_px(&primary, 15) == FONT_OK);
    font_fallback_discover(&u->fallback, "libfontconfig.so.1");
    u->result = font_family_load(&u->family, &primary, &u->fallback, &u->files);
    work_msg msg = {0}; msg.kind = FONT_FALLBACK_MSG_KIND;
    EDIT_ASSERT(work_publish(ctx, &msg));
}
static void unicode_ready(const work_msg *msg, void *arg)
{
    EDIT_ASSERT(msg->kind == FONT_FALLBACK_MSG_KIND);
    ((unicode_bench *)arg)->ready = 1;
}

/* Independent stage replay: same first 300 lines, text clipped to 356 columns.
 * Each pass isolates one cost; totals are not an additive wall-time profile. */
typedef struct profile_token { const uint8_t *p; size_t avail, len; int width; bool general; } profile_token;
static bool profiling;
static void profile(const uint8_t *map, size_t size, const char *file)
{
    profile_token *tokens = malloc(108000u * sizeof *tokens);
    render_cell *out = malloc(108000u * sizeof *out);
    if (!tokens || !out) exit(1);
    size_t nt = 0, ng = 0, nc = 0, off = 0;
    for (uint32_t row = 0; row < 300 && off < size; row++) {
        uint32_t col = 0;
        while (off < size && map[off] != '\n' && col < 356) {
            int w; size_t len;
            const uint8_t *p = map + off;
            bool general = p[0] >= 0x80 || (p[0] >= 0x20 && off + 1 < size && p[1] >= 0x80);
            if (p[0] == '\t') { w = 4 - (int)(col % 4); len = 1; }
            else if (p[0] < 0x20 || p[0] == 0x7f) { w = 1; len = 1; }
            else len = utf8_cluster(p, size - off, &w);
            tokens[nt++] = (profile_token){p, size - off, len, w, general};
            ng += general; nc += general && utf8_decode(p, size - off).valid && w > 0;
            col += (uint32_t)w; off += len;
        }
        const uint8_t *nl = memchr(map + off, '\n', size - off);
        off = nl ? (size_t)(nl - map) + 1 : size;
    }
    uint64_t values[200], sink = 0;
    for (uint32_t stage = 0; stage < 4; stage++) {
        bench_samples samples; bench_samples_init(&samples, values, 200);
        for (uint32_t it = 0; it < 220; it++) {
            uint64_t t0 = bench_now_ns();
            for (size_t i = 0; i < nt; i++) {
                profile_token v = tokens[i];
                if (stage == 0 && v.general) sink += utf8_grapheme_next(v.p, v.avail);
                else if (stage == 1 && v.general) sink += (uint64_t)utf8_cluster_width(v.p, v.len);
                else if (stage == 2 && v.general && v.width > 0) {
                    uint32_t slot; (void)stub_glyph(NULL, v.p, v.len, (uint32_t)v.width, &slot); sink += slot;
                } else if (stage == 3) out[i] = (render_cell){v.p[0], 31, 0xdddddd, 0x101010, 0, 0};
            }
            if (it >= 20) (void)bench_add(&samples, bench_now_ns() - t0);
        }
        char name[128]; static const char *names[] = {"grapheme", "width", "callback", "cell_writes"};
        snprintf(name, sizeof name, "profile_%s_%s_TRACK", file, names[stage]);
        (void)bench_report(name, &samples, 0, 0);
    }
    printf("profile tokens=%zu general=%zu callback=%zu sink=%llu cell=%u\n", nt, ng, nc,
           (unsigned long long)sink, nt ? out[nt - 1].fg : 0);
    free(tokens); free(out);
}

static void checkpoint_message(const work_msg *msg, void *ud)
{
    (void)layout_checkpoint_event(ud, msg);
}
static const char *only;
static int run(const char *dir, const char *file, uint32_t cols, uint32_t rows, bool gated, bool typing, bool real)
{
    if (only && !strstr(file, only)) return 0;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) { fprintf(stderr, "layout_bench: cannot open %s\n", path); return 1; }
    const uint8_t *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) return 1;
    for (off_t o = 0; o < st.st_size; o += 4096) (void)*(volatile const uint8_t *)(map + o);  /* warm page cache */
    if (profiling) profile(map, (size_t)st.st_size, file);
    bool longline = strcmp(file, "oneline_1g.txt") == 0;
    piece_allocator al = piece_default_allocator();
    piece_tree *t = piece_create(&al);
    if (!t || piece_init_mapped(t, map, (size_t)st.st_size, &hooks) != PIECE_OK) return 1;

    const font_ascii_atlas *a = font_ascii_atlas_for_px(15);
    render_cell *cells = malloc((size_t)cols * rows * sizeof *cells);
    uint64_t bits[8] = {0}, *row_byte = malloc(rows * sizeof *row_byte);
    uint32_t *row_used = malloc(rows * sizeof *row_used);
    render_glyph glyphs[LAYOUT_ASCII_GLYPHS];
    render_grid g;
    layout_config cfg = {0};
    static layout l;                         /* 16 KiB window: keep off the stack */
    if (!cells || !row_byte || !row_used) return 1;
    if (render_grid_init(&g, (render_dims){cols, rows, a->cell.cell_w, a->cell.cell_h}, cells,
                         (size_t)cols * rows, bits, 8) != RENDER_OK) return 1;
    layout_ascii_glyphs(a, glyphs);
    render_atlas_page page = {a->pixels, a->pixels_len, (size_t)a->cell.cell_w * 95, a->cell.cell_w * 95, a->cell.cell_h};
    g.pages = &page; g.page_count = 1; g.glyphs = glyphs; g.glyph_count = LAYOUT_ASCII_GLYPHS;
    cfg.tab_width = 4; cfg.gutter = true; cfg.fg = 0xdddddd; cfg.bg = 0x101010;
    cfg.gutter_fg = 0x808080; cfg.gutter_bg = 0x202020; cfg.glyph = stub_glyph;
    unicode_bench *u = NULL;
    if (real) {
        u = calloc(1, sizeof *u); EDIT_ASSERT(u);
        EDIT_ASSERT(edit_arena_init(&u->files, 64u << 20) == 0 && edit_arena_init(&u->storage, 12u << 20) == 0);
        work_pool *workers = malloc(sizeof *workers);
        EDIT_ASSERT(workers && work_pool_init(workers, 1, 0) == 0);
        EDIT_ASSERT(work_submit(workers, (work_job){unicode_job, u, 1, WORK_BULK}).epoch);
        struct pollfd fd_ready = {work_pool_eventfd(workers), POLLIN, 0};
        while (!u->ready) {
            EDIT_ASSERT(poll(&fd_ready, 1, 10000) > 0);
            (void)work_mailbox_drain(workers, unicode_ready, u);
        }
        work_pool_shutdown(workers); free(workers);
        EDIT_ASSERT(u->result == FONT_OK);
        EDIT_ASSERT(font_cache_init(&u->cache, &u->family, &u->storage, 3, 4096, 1u << 20, 1u << 20) == FONT_OK);
        EDIT_ASSERT(font_cache_bind(&u->cache, &g) == FONT_OK);
        cfg.glyph = font_cache_glyph; cfg.glyph_ctx = &u->cache;
    }
    if (layout_init(&l, &g, &cfg, row_byte, row_used) != LAYOUT_DONE) return 1;

    uint32_t frame = 0;
    layout_checkpoint_store store; edit_arena arena = {0}; work_pool *pool = NULL;
    if (longline) {
        if (render_frame_begin(&g, ++frame) != RENDER_OK ||
            layout_begin(&l, t, (layout_viewport){0, 0, 1000000, 1}) != LAYOUT_DONE) return 1;
        uint64_t t0 = bench_now_ns();
        if (layout_run(&l) != LAYOUT_DONE || !layout_approximate(&l) || l.bytes_scanned > LAYOUT_BYTE_BUDGET) return 1;
        uint64_t vals_before[1] = {bench_now_ns() - t0}; bench_samples before;
        bench_samples_init(&before, vals_before, 1); before.n = 1;
        (void)bench_report("layout_oneline_before_checkpoint_TRACK", &before, 0, 0);
        printf("long-line approximate bytes_scanned=%llu bytes_read=%llu\n",
               (unsigned long long)l.bytes_scanned, (unsigned long long)l.bytes_read);
        size_t reserve = ((size_t)st.st_size / LAYOUT_CHECKPOINT_STRIDE + 2) * sizeof(layout_checkpoint);
        if (edit_arena_init(&arena, reserve) != 0 ||
            layout_checkpoint_init(&store, &arena, (uint64_t)st.st_size) != LAYOUT_DONE ||
            layout_set_checkpoints(&l, &store) != LAYOUT_DONE) return 1;
        pool = malloc(sizeof *pool);
        if (!pool || work_pool_init(pool, 1, 0) != 0) return 1;
        piece_snapshot *snapshot = piece_snapshot_take(t);
        if (!snapshot) return 1;
        t0 = bench_now_ns();
        if (layout_checkpoint_request(&store, pool, snapshot, t, 0, 4) != LAYOUT_DONE) return 1;
        piece_snapshot_release(snapshot);
        struct pollfd event = {work_pool_eventfd(pool), POLLIN, 0};
        while (store.pending) { (void)poll(&event, 1, 100); (void)work_mailbox_drain(pool, checkpoint_message, &store); }
        uint64_t elapsed = bench_now_ns() - t0;
        if (!store.complete || store.newline) return 1;
        printf("checkpoint_build_TRACK ns=%llu entries=%zu reserved_bytes=%zu power=%s\n",
               (unsigned long long)elapsed, store.count, reserve, bench_evidence_tag());
    }
    uint64_t lines = longline ? 1 : piece_line_count(t), starts[STARTS], first_line[STARTS];
    /* the P1.3 stub kernel needs ~1 s per line lookup on a 1 GiB file: fewer starts there */
    uint32_t nstarts = st.st_size > (off_t)64 << 20 ? 4u : STARTS;
    for (uint32_t i = 0; i < nstarts; i++) {
        first_line[i] = (lines > 2u * rows) ? (lines / nstarts) * i : 0;
        starts[i] = piece_line_to_byte(t, first_line[i]);
    }
    static uint64_t vals[SAMPLES];
    bench_samples s; bench_samples_init(&s, vals, SAMPLES);
    uint64_t sum = 0, hits = 0, misses = 0, scanned_bytes = 0;
    static uint64_t vals_deep[SAMPLES]; bench_samples deep;
    bench_samples_init(&deep, vals_deep, SAMPLES);
    for (uint32_t it = 0; it < (longline ? 2u * SAMPLES : SAMPLES) + 64; it++) {
        uint32_t k = it % nstarts;
        if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
        uint32_t hs = longline ? (it & 1u ? 100000000u : 1000000u) : 0;
        uint64_t t0 = bench_now_ns();
        int rc = layout_begin(&l, t, (layout_viewport){starts[k], first_line[k], hs, lines});
        if (rc >= 0) rc = layout_run(&l);
        uint64_t dt = bench_now_ns() - t0;
        if (rc != LAYOUT_DONE) { fprintf(stderr, "layout_bench: rc=%d\n", rc); return 1; }
        if (!typing && it >= 64) {
            (void)bench_add(longline && (it & 1u) ? &deep : &s, dt);
            hits += l.cache_hits; misses += l.cache_misses; scanned_bytes += l.bytes_scanned;
        }
        if (longline && (layout_approximate(&l) || l.bytes_scanned > 2u * LAYOUT_WIN ||
                         cells[l.gw].glyph_index != map[hs])) return 1;
        sum += row_byte[rows - 1];
        if (typing) {
            uint64_t row = rows / 2, off = row_byte[row] + 1;
            if (it < 64 || row_byte[row] == LAYOUT_VOID_ROW) continue;
            uint8_t ch = 'x';
            if (piece_insert(t, off, &ch, 1) != PIECE_OK) return 1;
            if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
            t0 = bench_now_ns();
            rc = layout_edit(&l, off, 0, 1, 0, 0);
            if (rc >= 0) rc = layout_run(&l);
            dt = bench_now_ns() - t0;
            if (rc != LAYOUT_DONE) return 1;
            (void)bench_add(&s, dt);
            if (piece_delete(t, off, 1, NULL) != PIECE_OK) return 1;
            if (render_frame_begin(&g, ++frame) != RENDER_OK) return 1;
            if (layout_edit(&l, off, 1, 0, 0, 0) < 0 || layout_run(&l) != LAYOUT_DONE) return 1;
        }
    }
    if (render_grid_validate(&g) != RENDER_OK && !typing) {
        fprintf(stderr, "layout_bench: grid invalid\n");
        return 1;
    }
    char name[96];
    snprintf(name, sizeof name, "layout_%s_%ux%u%s%s%s", file, cols, rows, typing ? "_typing_row" : "",
             real ? "_font_cache" : "", gated ? "" : "_TRACK");
    if (longline) snprintf(name, sizeof name, "layout_oneline_1g.txt_column_1000000");
    int miss = bench_report(name, &s, gated ? GATE_NS : 0, gated ? GATE_NS : 0);
    if (longline) miss |= bench_report("layout_oneline_1g.txt_column_100000000", &deep, GATE_NS, GATE_NS);
    printf("layout_stats file=%s cluster_cache_hits=%llu misses=%llu scanned_bytes=%llu\n", file,
           (unsigned long long)hits, (unsigned long long)misses, (unsigned long long)scanned_bytes);
    if (longline) { work_pool_shutdown(pool); free(pool); edit_arena_free(&arena); }
    if (u) {
        printf("font_cache_stats hits=%llu misses=%llu glyphs=%zu pages=%u\n",
               (unsigned long long)u->cache.hits, (unsigned long long)u->cache.misses,
               u->cache.glyph_count, u->cache.atlas.npages);
        edit_arena_free(&u->storage); edit_arena_free(&u->files); free(u);
    }
    if (sum == 1) puts("");
    piece_destroy(t); munmap((void *)map, (size_t)st.st_size); close(fd);
    free(cells); free(row_byte); free(row_used);
    return miss;
}

static int run_wrapped(const char *dir);

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/edit-corpus";
    profiling = argc > 3 && strcmp(argv[3], "--profile") == 0;
    only = argc > 2 ? argv[2] : NULL;      /* optional file-name filter */
    setvbuf(stdout, NULL, _IOLBF, 0);
    char status[32]; bench_battery_status(status, sizeof status);
    printf("power=%s %s; (M) indicative under concurrent builds\n", status, bench_evidence_tag());
    printf("G: 300-row x 360-col full layout <= 150 us p50 and p99 (PLAN P3.1); layout state=%zu B\n", sizeof(layout));
    if(argc>3 && strcmp(argv[3],"--wrap-only")==0) return run_wrapped(dir);
    int miss = 0;
    miss |= run(dir, "ascii_code.c", 360, 300, true, false, false);
    miss |= run(dir, "log_1g.txt", 360, 300, true, false, false);
    miss |= run(dir, "unicode.txt", 360, 300, false, false, false);
    miss |= run(dir, "malformed.txt", 360, 300, false, false, false);
    (void)run(dir, "ascii_code.c", 360, 120, false, false, false);
    (void)run(dir, "log_1g.txt", 360, 120, false, false, false);
    (void)run(dir, "ascii_code.c", 360, 120, false, true, false);
    miss |= run(dir, "oneline_1g.txt", 360, 300, true, false, false);
    miss |= run(dir, "unicode.txt", 360, 300, false, false, true);
    miss |= run(dir, "unicode.txt", 360, 120, false, true, true);
    miss |= run_wrapped(dir);
    return miss;
}

/* P4.1 rows are kept separate from the existing P3.1 gates above. */
static int wrap_complete(layout *l)
{
    int rc; do { rc=layout_run(l); } while(rc==LAYOUT_MORE); return rc;
}
static int wrapped_file(const char *dir,const char *file)
{
    if(only && !strstr(file,only)) return 0;
    char path[512]; snprintf(path,sizeof path,"%s/%s",dir,file);
    int fd=open(path,O_RDONLY); struct stat st;
    if(fd<0 || fstat(fd,&st)!=0 || st.st_size<=0) return 1;
    size_t size=(size_t)st.st_size;
    const uint8_t *map=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);
    if(map==MAP_FAILED) { close(fd); return 1; }
    bool prose=!strcmp(file,"oneline_1g.txt");
    piece_allocator al=piece_default_allocator(); piece_tree *tree=piece_create(&al);
    if(!tree || piece_init_mapped(tree,map,size,&hooks)!=PIECE_OK) return 1;
    const font_ascii_atlas *atlas=font_ascii_atlas_for_px(15);
    render_cell *cells=malloc(360u*300u*sizeof *cells);
    uint64_t rb[300],bits[8]; uint32_t ru[300]; render_glyph glyphs[95];
    render_grid grid; render_atlas_page page={atlas->pixels,atlas->pixels_len,(size_t)atlas->cell.cell_w*95,atlas->cell.cell_w*95,atlas->cell.cell_h};
    layout l; layout_config cfg={0}; cfg.fg=0xdddddd; cfg.bg=0x101010; cfg.gutter=true; cfg.glyph=stub_glyph;
    edit_arena arena;
    if(!cells || edit_arena_init(&arena,prose?((size/LAYOUT_CHECKPOINT_STRIDE+2)*sizeof(layout_checkpoint)+65536):65536)!=0 ||
       render_grid_init(&grid,(render_dims){360,300,atlas->cell.cell_w,atlas->cell.cell_h},cells,108000,bits,8)!=0) return 1;
    layout_ascii_glyphs(atlas,glyphs); grid.glyphs=glyphs; grid.glyph_count=95; grid.pages=&page; grid.page_count=1;
    if(layout_init(&l,&grid,&cfg,rb,ru)!=0 || layout_wrap_init(&l,&arena)!=0 || layout_set_wrap(&l,true)!=0) return 1;
    uint64_t lines=prose?1:piece_line_count(tree), starts[STARTS], first[STARTS];
    for(uint32_t i=0;i<STARTS;i++) { first[i]=prose?0:(lines/STARTS)*i; starts[i]=prose?0:piece_line_to_byte(tree,first[i]); }
    uint64_t values[SAMPLES], typing_values[SAMPLES]; bench_samples samples,typing;
    bench_samples_init(&samples,values,SAMPLES); bench_samples_init(&typing,typing_values,SAMPLES);
    uint32_t frame=0; uint64_t scanned=0;
    for(uint32_t i=0;i<SAMPLES+64;i++) {
        uint32_t k=i%STARTS;
        if(render_frame_begin(&grid,++frame)!=0) return 1;
        uint64_t begin=bench_now_ns();
        if(layout_begin(&l,tree,(layout_viewport){starts[k],first[k],0,lines})!=0 || wrap_complete(&l)!=0) return 1;
        uint64_t dt=bench_now_ns()-begin;
        if(i>=64) { (void)bench_add(&samples,dt); scanned+=l.bytes_scanned; }
        /* A relayout of the edited logical line, excluding mutation/setup.
         * Unicode's corpus lines are short; prose's first logical line spans
         * the viewport, so that TRACK row intentionally updates all its rows. */
        if(i>=64) {
            uint64_t off=rb[150]+1;
            if(off==0 || off>piece_len(tree) || piece_insert(tree,off,(const uint8_t *)"x",1)!=0 || render_frame_begin(&grid,++frame)!=0) return 1;
            begin=bench_now_ns();
            if(layout_edit(&l,off,0,1,0,0)<0 || wrap_complete(&l)!=0) return 1;
            (void)bench_add(&typing,bench_now_ns()-begin);
            if(piece_delete(tree,off,1,NULL)!=0 || render_frame_begin(&grid,++frame)!=0 ||
               layout_edit(&l,off,1,0,0,0)<0 || wrap_complete(&l)!=0) return 1;
        }
    }
    if(render_grid_validate(&grid)!=0) return 1;
    char name[128]; snprintf(name,sizeof name,"layout_wrapped_%s_360x300",file);
    int miss=bench_report(name,&samples,300000,300000);
    snprintf(name,sizeof name,"layout_wrapped_%s_typing_row_TRACK",file);
    (void)bench_report(name,&typing,0,0);
    printf("wrap_stats file=%s scanned_bytes=%llu descriptor_bytes=%zu (M) %s\n",file,(unsigned long long)scanned,600u*sizeof(layout_wrap_row),bench_evidence_tag());
    if(prose) {
        layout_checkpoint_store store; work_pool *pool=malloc(sizeof *pool);
        if(!pool || layout_checkpoint_init(&store,&arena,size)!=0 || layout_set_checkpoints(&l,&store)!=0 || work_pool_init(pool,1,0)!=0) return 1;
        piece_snapshot *snapshot=piece_snapshot_take(tree); if(!snapshot) return 1;
        if(layout_checkpoint_request(&store,pool,snapshot,tree,0,4)!=0) return 1;
        piece_snapshot_release(snapshot);
        struct pollfd event={work_pool_eventfd(pool),POLLIN,0};
        while(store.pending) { (void)poll(&event,1,100); (void)work_mailbox_drain(pool,checkpoint_message,&store); }
        if(!store.complete) return 1;
        layout_wrap_row seed; bool approximate=false;
        if(layout_visual_row(&l,tree,100000000,0,&seed,&approximate)!=0 || seed.start>100000000 || seed.next<=100000000 || seed.column!=seed.start) return 1;
        bench_samples deep; bench_samples_init(&deep,values,200);
        for(unsigned i=0;i<216;i++) {
            if(render_frame_begin(&grid,++frame)!=0) return 1;
            uint64_t begin=bench_now_ns();
            if(layout_begin_visual(&l,tree,(layout_viewport){0,0,0,1},&seed)!=0 || wrap_complete(&l)!=0) return 1;
            if(i>=16) (void)bench_add(&deep,bench_now_ns()-begin);
            if(l.bytes_scanned>150000 || l.bytes_read>10u*LAYOUT_WIN) return 1;
        }
        miss|=bench_report("layout_wrapped_oneline_1g.txt_byte_100000000_TRACK",&deep,300000,300000);
        printf("wrap_deep_stats seed=%llu column=%llu approximate=%u scanned=%llu read=%llu (M) %s\n",
               (unsigned long long)seed.start,(unsigned long long)seed.column,(unsigned)approximate,
               (unsigned long long)l.bytes_scanned,(unsigned long long)l.bytes_read,bench_evidence_tag());
        work_pool_shutdown(pool); free(pool);
    }
    edit_arena_free(&arena); free(cells); piece_destroy(tree); munmap((void *)map,size); close(fd);
    return miss;
}
static int run_wrapped(const char *dir)
{
    char status[32],load[32]="unknown"; bench_battery_status(status,sizeof status);
    FILE *f=fopen("/proc/loadavg","r"); if(f) { if(fscanf(f,"%31s",load)!=1) memcpy(load,"unknown",8); fclose(f); }
    printf("wrap power=%s %s load1=%s; (M) shared-box TRACK verdict, gates unchanged\n",status,bench_evidence_tag(),load);
    int miss=wrapped_file(dir,"unicode.txt");
    miss|=wrapped_file(dir,"oneline_1g.txt");
    return miss;
}
