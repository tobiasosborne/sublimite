#include "indent/indent.h"
#include "base/base.h"
#include "harness.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TYPING_SAMPLES 20000u
#define DETECT_SAMPLES 2000u
#define VIEW_ROWS 300u

typedef struct fixture { uint8_t *bytes; size_t length; piece_tree *tree; } fixture;
static int fixture_open(fixture *f, const char *path)
{
    memset(f,0,sizeof *f); int fd=open(path,O_RDONLY); if (fd<0) { perror(path); return -1; }
    struct stat st;
    if (fstat(fd,&st)!=0 || st.st_size<=0 || (uint64_t)st.st_size>SIZE_MAX) { close(fd); return -1; }
    f->length=(size_t)st.st_size;
    f->bytes=mmap(NULL,f->length,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if (f->bytes==MAP_FAILED) { f->bytes=NULL; return -1; }
    piece_allocator a=piece_default_allocator(); f->tree=piece_create(&a);
    if (!f->tree || piece_init_mapped(f->tree,f->bytes,f->length,NULL)!=PIECE_OK) {
        if (f->tree) piece_destroy(f->tree);
        munmap(f->bytes,f->length); return -1;
    }
    return 0;
}
static void fixture_close(fixture *f) { piece_destroy(f->tree); (void)munmap(f->bytes,f->length); }
static int report(const char *name, bench_samples *s, uint64_t gate)
{
    /* Re-stamp each row; no timing runs are repeated to seek a quieter box. */
    char power[32], load[32]="unknown";
    bench_battery_status(power,sizeof power); FILE *file=fopen("/proc/loadavg","r");
    if (file) { if (fscanf(file,"%31s",load)!=1) strcpy(load,"unknown"); fclose(file); }
    uint64_t p50=bench_p50(s), p99=bench_p99(s);
    int pass=p99<=gate && s->dropped==0;
    printf("BENCH %s TRACK n=%zu p50=%.3f us p99=%.3f us (M)%s load1=%s "
           "gate_p99<=%.3f us(G) pass=%d\n",name,s->n,(double)p50/1000.0,(double)p99/1000.0,
           bench__tag_from_power(power),load,(double)gate/1000.0,pass);
    return pass?0:1;
}
static void stamp(void)
{
    char power[32], load[32]="unknown";
    bench_battery_status(power,sizeof power); FILE *file=fopen("/proc/loadavg","r");
    if (file) { if (fscanf(file,"%31s",load)!=1) strcpy(load,"unknown"); fclose(file); }
    printf("POWER status=%s %s load1=%s; indent measurements TRACK only\n",power,bench__tag_from_power(power),load);
}
int main(void)
{
    fixture code, log;
    if (fixture_open(&code,"/tmp/edit-corpus/ascii_code.c")!=0) return 2;
    size_t rows=0, end=0, cursors[VIEW_ROWS];
    while (end<code.length && rows<VIEW_ROWS) {
        if (code.bytes[end]=='\n') cursors[rows++]=end;
        ++end;
    }
    if (rows!=VIEW_ROWS) { fixture_close(&code); return 2; }
    size_t brackets[VIEW_ROWS*16u], nb=0;
    for (size_t i=0;i<end && nb<sizeof brackets/sizeof brackets[0];++i)
        if (code.bytes[i]=='{' || code.bytes[i]=='}' || code.bytes[i]=='(' || code.bytes[i]==')' || code.bytes[i]=='[' || code.bytes[i]==']') brackets[nb++]=i;
    if (!nb) { fixture_close(&code); return 2; }
    edit_arena arena; if (edit_arena_init(&arena,TYPING_SAMPLES*sizeof(uint64_t))!=0) { fixture_close(&code); return 2; }
    uint64_t *times=edit_arena_alloc(&arena,TYPING_SAMPLES*sizeof(*times),_Alignof(uint64_t)); EDIT_ASSERT(times);
    bench_samples samples; bench_samples_init(&samples,times,TYPING_SAMPLES);
    uint8_t out[256]; size_t length; uint64_t match;
    (void)bench_now_ns(); stamp(); edit_malloc_guard_begin();
    for (size_t i=0;i<TYPING_SAMPLES;++i) {
        uint64_t start=bench_now_ns();
        indent_code rc=indent_on_enter(code.tree,cursors[i%rows],out,sizeof out,&length);
        (void)bench_add(&samples,bench_now_ns()-start); EDIT_ASSERT(rc==INDENT_OK);
    }
    size_t allocations=edit_malloc_guard_end(); int fail=report("indent_on_enter viewport_rows=300",&samples,20000);
    bench_samples_init(&samples,times,TYPING_SAMPLES);
    stamp(); edit_malloc_guard_begin();
    for (size_t i=0;i<TYPING_SAMPLES;++i) {
        uint64_t start=bench_now_ns();
        indent_code rc=indent_bracket_match(code.tree,brackets[i%nb],0,end,&match);
        (void)bench_add(&samples,bench_now_ns()-start);
        EDIT_ASSERT(rc==INDENT_OK && (match==INDENT_NONE || match<end));
    }
    allocations+=edit_malloc_guard_end(); fail|=report("indent_bracket_match viewport_rows=300",&samples,20000);
    if (allocations || !edit_malloc_guard_active()) fail=1;
    char guard_power[32], guard_load[32]="unknown";
    bench_battery_status(guard_power,sizeof guard_power); FILE *guard_file=fopen("/proc/loadavg","r");
    if (guard_file) { if (fscanf(guard_file,"%31s",guard_load)!=1) strcpy(guard_load,"unknown"); fclose(guard_file); }
    printf("typing allocations=%zu (M)%s load1=%s gate_allocations=0(G) guard=%s\n",
           allocations,bench__tag_from_power(guard_power),guard_load,edit_malloc_guard_active()?"active":"inactive");
    fixture_close(&code);
    uint8_t long_bytes[32768]; memset(long_bytes,' ',sizeof long_bytes);
    piece_allocator allocator=piece_default_allocator(); piece_tree *long_tree=piece_create(&allocator);
    EDIT_ASSERT(long_tree && piece_init_copy(long_tree,long_bytes,sizeof long_bytes)==PIECE_OK);
    for (unsigned query=0;query<2;query++) {
        bench_samples_init(&samples,times,TYPING_SAMPLES); stamp(); edit_malloc_guard_begin();
        for (size_t i=0;i<TYPING_SAMPLES;++i) {
            uint64_t cursor=(i&1u)?sizeof long_bytes:0; indent_edit edit;
            size_t cap=(i&2u)?sizeof out:0;
            uint64_t start=bench_now_ns();
            indent_code result=query==0?indent_on_enter(long_tree,cursor,out,cap,&length):
                indent_on_close_brace(long_tree,cursor,(indent_style){false,4},&edit);
            (void)bench_add(&samples,bench_now_ns()-start);
            EDIT_ASSERT(result==INDENT_ERR_LIMIT);
            EDIT_ASSERT(query==0?length==0:edit.length==0);
        }
        EDIT_ASSERT(edit_malloc_guard_end()==0);
        fail|=report(query==0?"indent_long_line_enter includes_zero_capacity":"indent_long_line_brace",&samples,20000);
    }
    piece_destroy(long_tree);
    if (fixture_open(&log,"/tmp/edit-corpus/log_1g.txt")!=0) { edit_arena_free(&arena); return 2; }
    piece_snapshot *snapshot=piece_snapshot_take(log.tree); EDIT_ASSERT(snapshot);
    bench_samples_init(&samples,times,TYPING_SAMPLES); indent_style style;
    stamp();
    for (size_t i=0;i<DETECT_SAMPLES;++i) {
        uint64_t start=bench_now_ns(); indent_code rc=indent_detect(snapshot,&style);
        (void)bench_add(&samples,bench_now_ns()-start); EDIT_ASSERT(rc==INDENT_OK);
    }
    fail|=report("indent_detect file=log_1g.txt prefix<=64KiB",&samples,1000000);
    piece_snapshot_release(snapshot); fixture_close(&log); edit_arena_free(&arena); return fail;
}
