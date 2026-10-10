/* edit-457.10 / P4.10: G5, G7, G6 through the editor loop on the large corpus
 * (1 GiB log, 10 GiB sparse single line), with zero, one and three bulk jobs outstanding.
 * TRACK is explicit and suppresses timing certification only. Missing fixtures,
 * populations, frames, allocations, content or worker results remain fatal.
 * Save rows write private worktree copies and never modify the corpus. */
#include "editor/editor.h"
#include "editor/large.h"
#include "editor/private.h"
#include "trace/trace_fmt.h"
#include "raster/raster.h"
#include "font/font.h"
#include "trace/trace.h"
#include "base/base.h"
#include "harness.h"
#include "editor_accounting.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG "/tmp/edit-corpus/log_1g.txt"
#define SPARSE "/tmp/edit-corpus/sparse_10g.bin"
#define MS(x) ((double)(x) / 1e6)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "editor_large_bench:%d failed: %s\n", __LINE__, #c); return -1; } } while (0)
#define G1_P50 UINT64_C(1000000)
#define G1_P99 UINT64_C(2000000)
#define G5_P50 UINT64_C(6000000)
#define G5_P99 UINT64_C(9000000)
#define G7_P50 UINT64_C(80000000)
#define G7_P99 UINT64_C(125000000)
#define G6_P50 UINT64_C(80000000)
#define G6_P99 UINT64_C(125000000)
#define FG_CAP 20000u
/* Keep the binding deep workload, even when it exposes editor stalls. */
#define TYPE_KEYS BENCH_INTERACTION_MIN_N
#define TYPE_LINE(e) ((editor_line_count(e) / 10u) * 9u + (editor_line_count(e) % 10u) * 9u / 10u)

typedef struct rig {
    editor_frame frame; uint64_t ingress, first_submit, t0;
    uint32_t open_trace;
    samples accounted;
    size_t edit_keys;
    char source_path[4096], journal_path[4096];
} rig;
static void on_ingress(void *c, uint64_t seq, uint64_t ns)
{ rig *r = c; ingress(&r->accounted, seq, ns); }
static void on_submit(void *c, const editor_frame *f)
{
    rig *r = c;
    if (!r->first_submit) {
        r->first_submit = trace_now_ns();
        trace_record_at(r->first_submit, TRACE_T4_PRESENT_SUBMITTED, r->open_trace);
    }
    submitted(&r->accounted, f);
}
static void on_present(void *c, const editor_frame *f)
{ rig *r = c; presented(&r->accounted, f); if (f->last_sequence) r->frame = *f; }
static void on_io(void *c, bool in)
{ rig *r = c; io_boundary(&r->accounted, in); }
static void stamp(bool track)
{
    char st[32] = "unknown", ld[24] = "unknown"; const char *tag = "[unknown]";
    FILE *f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (f) { if (fgets(st, sizeof st, f)) st[strcspn(st, "\n")] = 0; fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f) { if (fgets(ld, sizeof ld, f)) ld[strcspn(ld, " \n")] = 0; fclose(f); }
    if (!strcmp(st, "Charging") || !strcmp(st, "Full") || !strcmp(st, "Not charging")) tag = "[AC]";
    else if (!strcmp(st, "Discharging")) tag = "[bat]";
    printf("STAMP (M)%s BAT0=%s load1=%s shared box mode=%s\n",tag,st,ld,track ? "TRACK" : "GATE");
}
static int open_ed(editor **e, render_backend *b, rig *r, const char *path)
{
    font_cell font=font_ascii_cell(); memset(r,0,sizeof *r); *e=NULL;
    REQUIRE(realpath(path,r->source_path));
    char journal_name[]="build/editor-large-journal-XXXXXX";
    int fd=mkstemp(journal_name); REQUIRE(fd>=0); REQUIRE(close(fd)==0);
    REQUIRE(realpath(journal_name,r->journal_path));
    editor_config cfg={.path=r->source_path,.journal_path=r->journal_path,
        .cols=2880/font.cell_w,.rows=1800/font.cell_h,.font_px=30,
        .hook_ctx=r,.on_ingress=on_ingress,.on_submit=on_submit,.on_present=on_present,.on_io=on_io};
    cfg.max_cols=cfg.cols; cfg.max_rows=cfg.rows;
    REQUIRE(render_null_backend(b)==0);
    r->t0=trace_now_ns(); REQUIRE(editor_open(e,&cfg,b)==0);
    r->open_trace=editor_large_trace_frame((*e)->buffer->file);
    trace_record_at(r->t0,TRACE_T0_INGRESS,r->open_trace);
    if (r->first_submit) trace_record_at(r->first_submit,TRACE_T4_PRESENT_SUBMITTED,r->open_trace);
    const render_grid *grid=editor_grid(*e);
    REQUIRE(grid->dims.cols*grid->dims.cell_w==2880 && grid->dims.rows*grid->dims.cell_h==1800);
    return 0;
}
static void close_ed(editor *e, rig *r)
{ editor_close(e); if (r->journal_path[0]) (void)unlink(r->journal_path); }
static bool quiet(editor *e) { editor_stats s = editor_get_stats(e); return !s.pending && !s.render_active; }
static int settle(editor *e)
{
    uint64_t dl = bench_now_ns() + UINT64_C(60000000000);   /* first edit at 0.9 lines is slow: see report */
    while (!quiet(e)) { int rc = editor_step(e, 5); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE); REQUIRE(bench_now_ns() < dl); }
    return 0;
}
static int step_until(editor *e, bool (*done)(editor *), uint64_t budget_ns)
{
    uint64_t dl = bench_now_ns() + budget_ns;
    while (!done(e)) { int rc = editor_step(e, 2); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE); REQUIRE(bench_now_ns() < dl); }
    return 0;
}
static bool b_first(editor *e) { (void)e; return false; }
static bool b_exact(editor *e) { return editor_large_status_get(e).lines_exact; }
static bool b_warm(editor *e) { editor_large_status s = editor_large_status_get(e); return s.lines_exact && s.warm_done; }
static bool b_find(editor *e) { return editor_large_status_get(e).find_done; }
static bool b_save(editor *e) { return editor_large_status_get(e).save_done; }

/* Workers must be joined before dumping/resetting the single-writer rings. */
static int attribution(const char *name, int sample, uint32_t frame, bool finding)
{
    char path[128];
    (void)snprintf(path, sizeof path, "build/edit-dnc-%s-%d.trace", name, sample);
    FILE *f = fopen(path, "wb"); REQUIRE(f);
    REQUIRE(trace_dump(f) == 0); REQUIRE(fclose(f) == 0);
    f = fopen(path, "rb"); REQUIRE(f);
    trace_rec *records = NULL; size_t count = 0;
    REQUIRE(trace_fmt_load(f, &records, &count) == 0); REQUIRE(fclose(f) == 0);
    uint64_t phases[TRACE_EV_COUNT] = {0};
    for (size_t i = 0; i < count; i++) if (records[i].frame_id == frame) {
        unsigned event = records[i].ev;
        if (event < TRACE_EV_COUNT && (!phases[event] || records[i].ns < phases[event]))
            phases[event] = records[i].ns;
    }
    const unsigned open_order[] = {0, 1, 2, 3, 5, 4, 6};
    const unsigned find_order[] = {0, 1, 2, 3, 4};
    const char *open_names[] = {"request_prefix", "prefix_mapping", "mapping_attachment", "attachment_index_submit", "index_submit_viewport", "viewport_exact"};
    const char *find_names[] = {"queue_worker", "search_kernel", "snapshot_release", "mailbox_adoption"};
    const unsigned *order = finding ? find_order : open_order;
    const char **names = finding ? find_names : open_names;
    size_t pairs = finding ? 4u : 6u;
    for (size_t i = 0; i < pairs; i++) {
        uint64_t begin = phases[order[i]], end = phases[order[i + 1]];
        if (begin && end && end >= begin)
            printf("ATTR %s sample=%d phase=%s ns=%" PRIu64 " (M)[AC]\n", name, sample, names[i], end - begin);
        else printf("ATTR %s sample=%d phase=%s unavailable\n", name, sample, names[i]);
    }
    if (!finding && phases[4] && phases[3]) {
        printf("ATTR %s sample=%d viewport_before_attachment=%d (M)[AC]\n",
            name, sample, phases[4] < phases[3]);
        if (phases[4] < phases[3])
            printf("ATTR %s sample=%d phase=viewport_attachment ns=%" PRIu64 " (M)[AC]\n",
                name, sample, phases[3] - phases[4]);
        if (phases[4] < phases[5])
            printf("ATTR %s sample=%d phase=viewport_index_submit ns=%" PRIu64 " (M)[AC]\n",
                name, sample, phases[5] - phases[4]);
    }
    free(records); trace_reset(); return 0;
}
static bool b_all3(editor *e) { editor_large_status s = editor_large_status_get(e); return s.lines_exact && s.find_done && s.save_done && s.warm_done; }

/* Foreground probe: one key (arrow, or insert/backspace pair) at a time while
 * the required job remains outstanding; samples injection -> correlated T4. */
static int probe(editor *e, rig *r, bool (*done)(editor *), bool edit, bench_samples *out, uint64_t budget_ns)
{
    uint64_t dl = bench_now_ns() + budget_ns; unsigned i = 0;
    while (!done(e) && out->n < out->cap) {
        plat_event ev = {.kind = PLAT_EV_KEY, .press = true};
        if (edit) { i = (unsigned)(r->edit_keys++ & 1u); ev.keysym = (i & 1u) ? XKB_KEY_BackSpace : XKB_KEY_x; if (!(i & 1u)) { ev.utf8_len = 1; ev.utf8[0] = 'x'; } }
        else ev.keysym = (i & 1u) ? XKB_KEY_Left : XKB_KEY_Right;
        i++; r->frame = (editor_frame){0};
        key_sample key = {.injected = bench_now_ns()};
        r->accounted.keys = &key; r->accounted.count = 1; r->accounted.completed = 0;
        r->accounted.first_sequence = r->accounted.sequence + 1; r->accounted.measuring = edit;
        editor_stats before = editor_get_stats(e);
        uint64_t old_length=editor_length(e), old_cursor=e->v.state.selection.cursor;
        REQUIRE(editor_inject(e, &ev) == 0); REQUIRE(settle(e) == 0);
        r->accounted.measuring = false; r->accounted.keys = NULL;
        REQUIRE(!r->accounted.invalid && r->accounted.completed == 1 && key.t4 >= key.injected);
        REQUIRE(!r->accounted.allocations && !r->accounted.guard && !r->accounted.suspended);
        if (edit) {
            editor_stats after=editor_get_stats(e);
            bool invalid=typing_structure(&r->accounted,before,after,1);
            if (invalid) fprintf(stderr,"STRUCTURAL editor_large mutations=%" PRIu64 " journals=%" PRIu64 " completed=%zu allocations=%zu journal_error=%d\n",after.mutations-before.mutations,after.journal_records-before.journal_records,r->accounted.completed,r->accounted.allocations,after.journal_error);
            REQUIRE(!invalid);
            if (ev.keysym==XKB_KEY_x) {
                uint8_t inserted=0; REQUIRE(editor_length(e)==old_length+1 && editor_read(e,old_cursor,&inserted,1)==0 && inserted=='x');
            } else REQUIRE(old_length && editor_length(e)==old_length-1);
        }
        REQUIRE(bench_add(out, key.t4 - key.injected) == 0);
        int rc = editor_step(e, 3); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
        REQUIRE(bench_now_ns() < dl);
    }
    return 0;
}
static int report_fg(const char *name, bench_samples *s, const char *mix, bool track, bool editing)
{
    char n[96], power[32]; bench_battery_status(power, sizeof power);
    (void)snprintf(n,sizeof n,editing ? "editor_large_%s_injection_T4_G1" : "editor_large_%s_navigation_regression",name);
    int timing = bench_gate_report(n, s, G1_P50, G1_P99, BENCH_INTERACTION_MIN_N, track, power, "-");
    bool structural = s->n < BENCH_INTERACTION_MIN_N || s->dropped != 0;
    printf("FG %s mix=%s samples=%zu (M)%s metric=%s mode=%s structural=%s\n",name,mix,s->n,bench__tag_from_power(power),editing ? "G1 injection_T4 (G)1/2ms" : "navigation regression (G)1/2ms",track ? "TRACK" : "GATE",structural ? "FAIL" : "OK");
    return row_result(timing, structural, track);
}

static int row_open(const char *path, const char *name, bool gated, int runs, bool track)
{
    enum { N = BENCH_INTERACTION_MIN_N };
    int n_runs = runs < N ? runs : N;
    uint64_t v5[N], v7[N]; bench_samples g5, g7; bench_samples_init(&g5, v5, N); bench_samples_init(&g7, v7, N);
    uint64_t est_lines = 0, exact_lines = 0;
    uint64_t fgv[FG_CAP]; bench_samples fg; bench_samples_init(&fg, fgv, FG_CAP);
    for (int k = 0; k < n_runs; k++) {
        render_backend b = {0}; rig r; editor *e;
        REQUIRE(open_ed(&e, &b, &r, path) == 0);
        uint64_t first_deadline=bench_now_ns()+UINT64_C(60000000000);
        while (!r.first_submit) { int rc=editor_step(e,5); REQUIRE(rc==EDITOR_OK || rc==EDITOR_MORE); REQUIRE(bench_now_ns()<first_deadline); }
        (void)bench_add(&g5, r.first_submit - r.t0);
        /* G5 ends at real content, rather than an empty submitted grid. */
        uint8_t prefix[16]; REQUIRE(editor_read(e, 0, prefix, sizeof prefix) == 0);
        const render_grid *grid = editor_grid(e);
        uint32_t gutter = 2;
        for (uint64_t lines = editor_line_count(e); lines >= 10; lines /= 10) gutter++;
        for (uint32_t i = 0; i < sizeof prefix; i++) {
            uint32_t glyph = prefix[i] == ' ' ? 0u : prefix[i] < 0x20 || prefix[i] == 0x7f ? '?' : prefix[i];
            const render_cell *cell = &grid->cells[grid->dims.cols + gutter + i];
            REQUIRE(cell->glyph_index == glyph);
            if (prefix[i] < 0x20) REQUIRE(cell->attrs & RENDER_ATTR_INVERSE);
        }
        editor_large_status s = editor_large_status_get(e); est_lines = editor_line_count(e);
        REQUIRE(s.mapped && !s.lines_exact);
        REQUIRE(probe(e,&r,b_exact,false,&fg,UINT64_C(60000000000))==0);
        REQUIRE(step_until(e,b_exact,UINT64_C(60000000000))==0);
        (void)bench_add(&g7, trace_now_ns() - r.t0);
        exact_lines = editor_line_count(e);
        REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
        close_ed(e,&r);
        if (k==0) REQUIRE(attribution(name,k,r.open_trace,false)==0);
    }
    char n[96]; (void)snprintf(n, sizeof n, "editor_large_%s_G5_open_first_viewport", name);
    char power[32]; bench_battery_status(power,sizeof power);
    int miss=bench_gate_report(n,&g5,G5_P50,G5_P99,BENCH_INTERACTION_MIN_N,track,power,"-");
    (void)snprintf(n,sizeof n,gated ? "editor_large_%s_G7_open_index_exact" : "editor_large_%s_index_completion_TRACK",name);
    miss=bench_merge_exit(miss,bench_gate_report(n,&g7,gated ? G7_P50 : 0,gated ? G7_P99 : 0,BENCH_INTERACTION_MIN_N,track,power,"-"));
    printf("LINES %s estimated_at_first_viewport=%" PRIu64 " exact_after_index=%" PRIu64 " (G7 gate is 1 GB; sparse_10g tracked)\n", name, est_lines, exact_lines);
    REQUIRE(report_fg(name,&fg,"index_active",track,false) == 0);
    return row_result(miss,g5.n<BENCH_INTERACTION_MIN_N || g7.n<BENCH_INTERACTION_MIN_N || g5.dropped || g7.dropped,track);
}
/* Cached-mapping find regression, followed by the binding deep G1 workload. */
/* Independent corpus oracle, outside every request timer. */
static int log_oracle(uint64_t *deep, uint64_t *matches)
{
    int fd=open(LOG,O_RDONLY|O_CLOEXEC); struct stat metadata;
    if (fd<0 || fstat(fd,&metadata) || metadata.st_size!=(off_t)1073741824) { if (fd>=0) close(fd); return -1; }
    size_t length=(size_t)metadata.st_size;
    const uint8_t *bytes=mmap(NULL,length,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if (bytes==MAP_FAILED) return -1;
    uint64_t lines=1; *deep=UINT64_MAX; *matches=0;
    const uint8_t needle[]="ERROR render";
    for (size_t pos=0;pos<length;pos++) {
        if (bytes[pos]=='\n') { lines++; if (lines-1==8053057) *deep=pos+1u; }
        if (bytes[pos]==needle[0] && length-pos>=sizeof needle-1 && !memcmp(bytes+pos,needle,sizeof needle-1)) (*matches)++;
    }
    int rc=lines==8947842 && *deep!=UINT64_MAX ? 0 : -1;
    (void)munmap((void *)bytes,length); return rc;
}
static bool correct_saved_copy(const char *path)
{
    int a=open(LOG,O_RDONLY|O_CLOEXEC), b=open(path,O_RDONLY|O_CLOEXEC);
    bool same=a>=0 && b>=0; uint8_t left[65536], right[65536]; uint64_t offset=0;
    while (same && offset<UINT64_C(1073741824)) {
        ssize_t n=pread(a,left,sizeof left,(off_t)offset), m=pread(b,right,sizeof right,(off_t)offset);
        if (n<=0 || n!=m || memcmp(left,right,(size_t)n)) { same=false; break; }
        offset+=(uint64_t)n;
    }
    struct stat metadata; if (same && (fstat(b,&metadata) || metadata.st_size!=(off_t)1073741824)) same=false;
    if (a>=0) close(a);
    if (b>=0) close(b);
    return same;
}
static int required_fixtures(const char *log_path, const char *sparse_path)
{
    struct stat metadata;
    if (stat(log_path,&metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size!=(off_t)1073741824 ||
        stat(sparse_path,&metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size!=(off_t)10737418240) {
        fputs("editor_large_bench: UNAVAILABLE required fixtures\n",stderr); return 2;
    }
    return 0;
}
static int row_find(const char *name, bool track)
{
    uint64_t deep=0, expected_matches=0; REQUIRE(log_oracle(&deep,&expected_matches)==0);
    render_backend b = {0}; rig r; editor *e; REQUIRE(open_ed(&e, &b, &r, LOG) == 0);
    REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
    const uint8_t needle[] = "ERROR render";
    enum { N = BENCH_INTERACTION_MIN_N }; uint64_t v6[N]; bench_samples g6; bench_samples_init(&g6, v6, N);
    uint64_t fgv[FG_CAP]; bench_samples fg; bench_samples_init(&fg, fgv, FG_CAP);
    uint64_t total = 0;
    uint32_t find_trace = 0;
    for (int k = 0; k < N; k++) {
        uint64_t t = trace_now_ns();
        REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
        if (k == 0) find_trace = editor_large_trace_frame(e->buffer->lg.find);
        REQUIRE(probe(e,&r,b_find,false,&fg,UINT64_C(60000000000))==0);
        REQUIRE(step_until(e,b_find,UINT64_C(60000000000))==0);
        (void)bench_add(&g6, trace_now_ns() - t);
        editor_large_status s=editor_large_status_get(e); REQUIRE(s.find_done && s.find_rc==0 && s.find_total==expected_matches); total=s.find_total;
    }
    char n[96]; (void)snprintf(n, sizeof n, "editor_large_%s_find_cached_mapping_regression", name);
    char power[32]; bench_battery_status(power,sizeof power);
    REQUIRE(g6.n==BENCH_INTERACTION_MIN_N && !g6.dropped);
    int timing=bench_gate_report(n,&g6,0,0,BENCH_INTERACTION_MIN_N,track,power,"-");
    puts("G6 editor cached-mapping row is a regression only; new-mapping certification UNAVAILABLE here (findui --count measures it)");
    printf("FIND %s total=%" PRIu64 " (exact count; first 4096 offsets stored)\n", name, total);
    REQUIRE(report_fg("find_1worker_arrows",&fg,"find_active",track,false) == 0);
    /* Repeat full find jobs while typing at 0.9 of the exact line count. */
    uint64_t line = TYPE_LINE(e); bool ex = false;
    uint64_t byte=editor_large_line_to_byte(e,line,&ex); REQUIRE(ex && byte==deep);
    REQUIRE(editor_large_goto_byte(e,byte)==0); REQUIRE(settle(e)==0);
    REQUIRE(e->v.state.selection.cursor==deep);
    REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
    uint64_t tv[FG_CAP]; bench_samples ty; bench_samples_init(&ty, tv, TYPE_KEYS);
    while (ty.n < TYPE_KEYS) {
        if (editor_large_status_get(e).find_done) REQUIRE(editor_large_find_begin(e, needle, sizeof needle - 1) == 0);
        bench_samples one; uint64_t ov[2]; bench_samples_init(&one, ov, 2);
        REQUIRE(probe(e, &r, b_find, true, &one, UINT64_C(60000000000)) == 0);
        for (size_t i = 0; i < one.n; i++) (void)bench_add(&ty, one.v[i]);
    }
    REQUIRE(report_fg("typing_deep_0.9lines_find_1worker",&ty,"find_active",track,true) == 0);
    printf("ALLOC typing_1worker allocations=%zu (G)=0\n", r.accounted.allocations);
    close_ed(e,&r); REQUIRE(attribution("find",0,find_trace,true)==0); return row_result(timing,false,track);
}
/* Each sample admits all three types on a newly opened source. A completed
 * find never authorizes a sample with the save/index already gone. */
static bool b_any_three(editor *e)
{
    editor_large_status state=editor_large_status_get(e);
    return state.lines_exact || state.find_done || state.save_done;
}
static int row_three(bool track, size_t count)
{
    uint64_t deep=0, expected_matches=0; REQUIRE(log_oracle(&deep,&expected_matches)==0);
    char copy[]="build/editor-large-bench-XXXXXX"; int fd=mkstemp(copy); REQUIRE(fd>=0); close(fd);
    char cmd[256]; (void)snprintf(cmd,sizeof cmd,"cp --reflink=auto %s %s",LOG,copy); REQUIRE(system(cmd)==0);
    uint64_t values[TYPE_KEYS]; bench_samples typing; bench_samples_init(&typing,values,TYPE_KEYS);
    const uint8_t needle[]="ERROR render";
    for (size_t sample=0;sample<count;sample++) {
        render_backend backend={0}; rig r; editor *e;
        REQUIRE(open_ed(&e,&backend,&r,copy)==0);
        /* A known corpus line start near 0.9 of the file. Resolve the exact
         * boundary off path once per fixture; do not move to line 1000. */
        uint64_t byte=deep;
        REQUIRE(editor_large_goto_byte(e,byte)==0 && e->v.state.selection.cursor==deep);
        REQUIRE(editor_large_find_begin(e,needle,sizeof needle-1)==0);
        REQUIRE(editor_large_save_begin(e)==0);
        editor_large_status before=editor_large_status_get(e);
        REQUIRE(!before.lines_exact && !before.find_done && !before.save_done && before.find_running && before.save_running);
        REQUIRE(lineidx_building(e->buffer->index) && !work_handle_finished(&e->pool,e->buffer->lg.find_h) && file_save_busy(e->buffer->file));
        uint64_t value=0; bench_samples one; bench_samples_init(&one,&value,1);
        REQUIRE(probe(e,&r,b_any_three,true,&one,UINT64_C(60000000000))==0);
        REQUIRE(one.n==1 && !one.dropped && bench_add(&typing,value)==0);
        REQUIRE(step_until(e,b_all3,UINT64_C(180000000000))==0);
        editor_large_status after=editor_large_status_get(e);
        REQUIRE(after.lines_exact && after.warm_done && after.find_done && after.find_rc==0 && after.find_total==expected_matches && after.save_done && after.save_status==0);
        REQUIRE(!r.accounted.allocations && editor_length(e)==UINT64_C(1073741825));
        REQUIRE(correct_saved_copy(copy));
        close_ed(e,&r);
    }
    REQUIRE(unlink(copy)==0);
    return report_fg("typing_deep_three_jobs",&typing,"index+find+save outstanding at each injection",track,true);
}
static bool b_never(editor *e) { (void)e; return false; }
static int row_newline_free(bool track, size_t count)
{
    render_backend backend={0}; rig r; editor *e; REQUIRE(open_ed(&e,&backend,&r,SPARSE)==0);
    REQUIRE(step_until(e,b_warm,UINT64_C(120000000000))==0);
    REQUIRE(editor_large_goto_byte(e,UINT64_C(9663676416))==0); REQUIRE(settle(e)==0);
    REQUIRE(e->v.state.selection.cursor==UINT64_C(9663676416));
    uint64_t values[TYPE_KEYS]; bench_samples typing; bench_samples_init(&typing,values,count);
    REQUIRE(probe(e,&r,b_never,true,&typing,UINT64_C(180000000000))==0);
    REQUIRE(editor_length(e)==UINT64_C(10737418240)+(count&1u));
    int rc=report_fg("typing_newline_free_10GiB",&typing,"serial",track,true);
    close_ed(e,&r); return rc;
}

typedef struct index_probe {
    piece_snapshot *snapshot;
    const uint8_t *bytes; size_t length;
    bool flat;
    uint64_t first, last, callback_ns; size_t calls;
    uint64_t first_cpu, last_cpu;
} index_probe;
static uint64_t worker_cpu_now(void)
{
    struct timespec ts; (void)clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static size_t index_span_probe(void *ctx, uint64_t off, const uint8_t **p)
{
    index_probe *probe_state = ctx;
    uint64_t begin = trace_now_ns();
    if (!probe_state->calls) {
        probe_state->first = begin;
        probe_state->first_cpu = worker_cpu_now();
        trace_record_at(begin, TRACE_T1_DEQUEUE, editor_large_trace_frame(probe_state));
    }
    size_t length = 0;
    if (probe_state->flat) {
        if (off < probe_state->length) { *p = probe_state->bytes + off; length = probe_state->length - (size_t)off; }
    } else {
        piece_iter it; piece_iter_begin_snapshot(&it, probe_state->snapshot, off);
        (void)piece_iter_next(&it, p, &length);
    }
    uint64_t end = trace_now_ns();
    probe_state->callback_ns += end - begin; probe_state->calls++;
    probe_state->last = end;
    if (off < probe_state->length && probe_state->length - off <= LINEIDX_CHUNK) {
        probe_state->last_cpu = worker_cpu_now();
        trace_record_at(end, TRACE_T2_MUTATION_DONE, editor_large_trace_frame(probe_state));
    }
    return length;
}
/* Same pool, mapping and poll policy, flat kernel floor then editor snapshot
 * callback. Source is prefaulted by the untimed editor warm job. */
static int row_index_floor(const char *path, const char *name)
{
    enum { N = 3 }; uint64_t flat_values[N], snapshot_values[N];
    bench_samples flat_samples, snapshot_samples;
    bench_samples_init(&flat_samples, flat_values, N);
    bench_samples_init(&snapshot_samples, snapshot_values, N);
    for (int sample = 0; sample < N; sample++) {
        render_backend backend = {0}; rig r; editor *e;
        REQUIRE(open_ed(&e, &backend, &r, path) == 0);
        REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
        for (unsigned variant = 0; variant < 2; variant++) {
            index_probe state = {.snapshot = piece_snapshot_take(e->tree), .flat = variant == 0};
            REQUIRE(state.snapshot);
            REQUIRE(file_snapshot_bytes(state.snapshot, NULL, &state.bytes, &state.length) == 1);
            uint64_t begin = trace_now_ns();
            lineidx *index = lineidx_create(state.length); REQUIRE(index);
            REQUIRE(lineidx_bind_snapshot(index, state.snapshot) == 0);
            lineidx_src source = {&state, state.length, index_span_probe, NULL};
            trace_record_at(begin, TRACE_T0_INGRESS, editor_large_trace_frame(&state));
            REQUIRE(lineidx_build_start(index, &e->pool, &source) == 0);
            uint64_t deadline = begin + UINT64_C(60000000000), exact = 0;
            while (!exact || lineidx_building(index)) {
                (void)work_mailbox_drain(&e->pool, editor_route_work, e);
                (void)lineidx_poll(index);
                if (!exact && lineidx_complete(index)) exact = trace_now_ns();
                REQUIRE(trace_now_ns() < deadline);
                if (!lineidx_build_needs_poll(index) && lineidx_building(index)) {
                    struct pollfd ready = {work_pool_eventfd(&e->pool), POLLIN, 0};
                    (void)poll(&ready, 1, 1);
                }
            }
            REQUIRE(lineidx_line_count(index).value == editor_line_count(e));
            bench_samples *population = state.flat ? &flat_samples : &snapshot_samples;
            (void)bench_add(population, exact - begin);
            trace_record_at(exact, TRACE_T4_PRESENT_SUBMITTED, editor_large_trace_frame(&state));
            printf("ATTR %s sample=%d variant=%s queue_ns=%" PRIu64 " worker_extent_ns=%" PRIu64
                " worker_cpu_ns=%" PRIu64 " callback_ns=%" PRIu64 " callbacks=%zu tail_adoption_ns=%" PRIu64 " (M)[AC]\n",
                name, sample, state.flat ? "flat" : "snapshot", state.first - begin,
                state.last - state.first, state.last_cpu - state.first_cpu, state.callback_ns, state.calls, exact > state.last ? exact - state.last : 0);
            lineidx_destroy(index); piece_snapshot_release(state.snapshot);
        }
        close_ed(e,&r);
    }
    char label[96];
    (void)snprintf(label, sizeof label, "editor_large_%s_index_flat_floor_TRACK", name);
    (void)bench_report(label, &flat_samples, 0, 0);
    (void)snprintf(label, sizeof label, "editor_large_%s_index_snapshot_source_TRACK", name);
    (void)bench_report(label, &snapshot_samples, 0, 0);
    return 0;
}
typedef struct find_kernel_probe {
    find_source source; find_result result; find_code rc;
    uint64_t begin, end;
} find_kernel_probe;
static void find_floor_fn(work_ctx *ctx)
{
    find_kernel_probe *state = ctx->arg;
    const uint8_t needle[] = "ERROR render";
    find_control control = {.work = ctx};
    state->begin = trace_now_ns();
    trace_record_at(state->begin, TRACE_T1_DEQUEUE, editor_large_trace_frame(state));
    state->rc = find_literal(&state->source, needle, sizeof needle - 1, &control, &state->result);
    state->end = trace_now_ns();
    trace_record_at(state->end, TRACE_T2_MUTATION_DONE, editor_large_trace_frame(state));
}
static int row_find_floor(void)
{
    enum { N = 5 }; uint64_t flat_values[N], snapshot_values[N];
    bench_samples flat_samples, snapshot_samples;
    bench_samples_init(&flat_samples, flat_values, N); bench_samples_init(&snapshot_samples, snapshot_values, N);
    render_backend backend = {0}; rig r; editor *e;
    REQUIRE(open_ed(&e, &backend, &r, LOG) == 0);
    REQUIRE(step_until(e, b_warm, UINT64_C(120000000000)) == 0);
    piece_snapshot *snapshot = piece_snapshot_take(e->tree); REQUIRE(snapshot);
    const uint8_t *bytes; size_t length;
    REQUIRE(file_snapshot_bytes(snapshot, NULL, &bytes, &length) == 1);
    for (int sample = 0; sample < N; sample++) {
        uint64_t total = 0;
        for (unsigned variant = 0; variant < 2; variant++) {
            find_kernel_probe state = {.source = {.bytes = bytes, .length = length, .snapshot = variant ? snapshot : NULL}};
            work_handle handle = work_submit(&e->pool, (work_job){find_floor_fn, &state, 0, WORK_BULK}); REQUIRE(handle.epoch);
            uint64_t deadline = trace_now_ns() + UINT64_C(60000000000);
            while (!work_handle_finished(&e->pool, handle)) {
                int rc = editor_step(e, 1); REQUIRE(rc == EDITOR_OK || rc == EDITOR_MORE);
                REQUIRE(trace_now_ns() < deadline);
            }
            REQUIRE(state.rc == FIND_OK);
            if (!variant) total = state.result.total; else REQUIRE(total == state.result.total);
            (void)bench_add(variant ? &snapshot_samples : &flat_samples, state.end - state.begin);
            printf("ATTR find_floor sample=%d variant=%s kernel_ns=%" PRIu64 " total=%" PRIu64 " (M)[AC]\n",
                sample, variant ? "snapshot" : "flat", state.end - state.begin, state.result.total);
        }
    }
    piece_snapshot_release(snapshot); close_ed(e,&r);
    (void)bench_report("editor_large_log_1g_find_flat_kernel_floor_TRACK", &flat_samples, 0, 0);
    (void)bench_report("editor_large_log_1g_find_snapshot_kernel_TRACK", &snapshot_samples, 0, 0);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--self-check")) {
        uint64_t v = 1; bench_samples tiny; bench_samples_init(&tiny, &v, 1); tiny.n = 1;
        int verdict = report_fg("self_check_missing_population",&tiny,"none",true,true);
        printf("PROBE editor_large missing_population TRACK exit=%d expected=1 (G)\n", verdict);
        REQUIRE(verdict==1);
        REQUIRE(required_fixtures("build/edit-2j9-absent-log","build/edit-2j9-absent-sparse")==2);
        render_backend backend={0}; rig r={0}; editor *e=NULL;
        editor_config cfg={.initial=(const uint8_t *)"base",.initial_len=4,.cols=32,.rows=8,
            .hook_ctx=&r,.on_ingress=on_ingress,.on_submit=on_submit,.on_present=on_present,.on_io=on_io};
        REQUIRE(render_null_backend(&backend)==0 && editor_open(&e,&cfg,&backend)==0 && settle(e)==0);
        key_sample key={.injected=bench_now_ns()}; r.accounted.keys=&key; r.accounted.count=1;
        r.accounted.first_sequence=1; r.accounted.measuring=true;
        plat_event event={.kind=PLAT_EV_KEY,.press=true,.keysym=XKB_KEY_x,.utf8_len=1,.utf8={'x'}};
        REQUIRE(editor_inject(e,&event)==0);
        REQUIRE(nanosleep(&(struct timespec){0,20000000},NULL)==0 && settle(e)==0);
        REQUIRE(r.accounted.completed==1 && !r.accounted.invalid && !r.accounted.allocations && key.t4-key.injected>=UINT64_C(20000000));
        uint8_t text[5]; REQUIRE(editor_read(e,0,text,5)==0 && !memcmp(text,"xbase",5));
        printf("PROBE editor_large injection_T4 includes queued delay ns=%" PRIu64 " (M)%s\n",key.t4-key.injected,bench_evidence_tag());
        r.accounted.allocations=1;
        REQUIRE(row_result(0,typing_structure(&r.accounted,(editor_stats){0},editor_get_stats(e),1),true)==1);
        r.accounted.allocations=0; r.accounted.keys=NULL; r.accounted.measuring=false; close_ed(e,&r);
        puts("PROBE editor_large missing fixtures/content/allocation policy checked");
        return 0;
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    trace_init(); (void)trace_thread_register();
    if (required_fixtures(LOG,SPARSE)) return 2;
    bool track=argc==2 && !strcmp(argv[1],"--track");
    stamp(track || (argc==2 && (!strncmp(argv[1],"--attribute-",12) || !strncmp(argv[1],"--probe-",8))));
    if (argc==2 && !strcmp(argv[1],"--probe-three")) { int rc=row_three(true,1); return rc<0 ? 1 : rc; }
    if (argc==2 && !strcmp(argv[1],"--probe-newline-free")) { int rc=row_newline_free(true,1); return rc<0 ? 1 : rc; }
    if (argc == 2 && strcmp(argv[1], "--attribute-find-floor") == 0) return row_find_floor();
    if (argc == 2 && strcmp(argv[1], "--attribute-index") == 0) {
        if (row_index_floor(LOG,"log_1g")) return 1;
        if (row_index_floor(SPARSE,"sparse_10g")) return 1;
        FILE *dump = fopen("build/edit-dnc-index-floor.trace", "wb"); REQUIRE(dump);
        REQUIRE(trace_dump(dump) == 0); REQUIRE(fclose(dump) == 0); return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--attribute-log") == 0) return row_open(LOG,"log_1g",true,3,true);
    if (argc == 2 && strcmp(argv[1], "--attribute-sparse") == 0) return row_open(SPARSE,"sparse_10g",false,3,true);
    if (argc == 2 && strcmp(argv[1], "--attribute-find") == 0) return row_find("log_1g",track);
    if (argc>1 && !track) { fputs("usage: editor_large_bench [--track|--self-check|--attribute-*]\n",stderr); return 2; }
    int result=row_open(LOG,"log_1g",true,BENCH_INTERACTION_MIN_N,track); if (result<0) return 1;
    int rc=row_find("log_1g",track); if (rc<0) return 1; result=bench_merge_exit(result,rc);
    rc=row_three(track,TYPE_KEYS); if (rc<0) return 1; result=bench_merge_exit(result,rc);
    rc=row_open(SPARSE,"sparse_10g",false,BENCH_INTERACTION_MIN_N,track); if (rc<0) return 1; result=bench_merge_exit(result,rc);
    rc=row_newline_free(track,TYPE_KEYS); if (rc<0) return 1; result=bench_merge_exit(result,rc);
    (void)b_first; (void)b_save;
    return !track && result==0 ? 3 : result; /* editor G6 new mapping remains unavailable */
}
