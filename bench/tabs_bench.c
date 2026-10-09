#include "tabs/tabs.h"
#include "harness.h"
#include <string.h>

#define SAMPLES 20000u
static bool resident_glyph(void *ctx, const uint8_t *text, size_t len, int width,
                           uint32_t *identity, uint32_t *slot)
{
    (void)ctx; (void)width; utf8_step unit=utf8_decode(text,len);
    if(unit.cp>=32 && unit.cp<=126) { *identity=unit.cp; *slot=unit.cp-32; return true; }
    if(unit.cp==0x4e2d) { *identity=unit.cp; *slot=95; return true; }
    if(unit.cp==0x1f469) { *identity=unit.cp; *slot=96; return true; }
    return false;
}
static void load_stamp(char *load, size_t n)
{
    FILE *f=fopen("/proc/loadavg","r");
    if(f) { if(fgets(load,(int)n,f)==NULL) strcpy(load,"unknown"); fclose(f); }
    else strcpy(load,"unknown");
    char *space=strchr(load,' '); if(space) *space=0;
}
static int run(tabs_set *s, view_state *live, render_grid *g, uint32_t cols,
               bool all_visible, const char *name)
{
    uint64_t samples[SAMPLES]; bench_samples times; bench_samples_init(&times,samples,SAMPLES);
    tabs_strip strip={.col_count=cols,.tab_cols=24,.fg=0xe0e0e0,.bg=0x202020,
        .active_fg=0xffffff,.active_bg=0x405060,.glyph=resident_glyph};
    /* Stamp before this measurement; no power/load I/O inside timing. */
    char power[32], load[64]; bench_battery_status(power,sizeof power); load_stamp(load,sizeof load);
    const char *tag=bench__tag_from_power(power);
    uint32_t visible=cols/24u;
    for(uint32_t i=0;i<1000u+SAMPLES;++i) {
        size_t index=(size_t)((i*37u)%100u);
        strip.first_tab=all_visible?0:(index/visible)*visible;
        live->selection.cursor=i; live->selection.anchor=i/2u;
        live->first_line=i; live->first_byte=(uint64_t)i*32u; live->hscroll=i%8u;
        EDIT_ASSERT(render_frame_begin(g,i+1u)==0);
        uint64_t start=bench_now_ns();
        EDIT_ASSERT(tabs_select(s,index,live)==0);
        EDIT_ASSERT(tabs_strip_render(s,g,&strip)==0);
        uint64_t elapsed=bench_now_ns()-start;
        if(i>=1000u) EDIT_ASSERT(bench_add(&times,elapsed)==0);
    }
    uint64_t p50=bench_p50(&times), p99=bench_p99(&times), lo, hi;
    bench_ci95(&times,0.99,&lo,&hi);
    int pass=p99<=500000u && times.n==SAMPLES && times.dropped==0;
    printf("tabs_bench: %s (M)%s load1=%s power=%s tabs=100 cols=%u n=%zu "
           "p50_ns=%llu p99_ns=%llu p99_ci95=[%llu,%llu] gate_p99_ns=500000(G) "
           "module_share=%s verdict=TRACK\n",name,tag,load,power,cols,times.n,
           (unsigned long long)p50,(unsigned long long)p99,(unsigned long long)lo,
           (unsigned long long)hi,pass?"PASS":"MISS");
    return pass?0:1;
}
int main(void)
{
    /* Borrowed small-file resources are set up outside measurements. */
    tabs_set s; EDIT_ASSERT(tabs_init(&s,100,16)==0);
    piece_allocator allocator=piece_default_allocator(); piece_tree *trees[100]; undo_log logs[100];
    view_state live={0}; size_t undo_bytes=0;
    for(size_t i=0;i<100;++i) {
        trees[i]=piece_create(&allocator); EDIT_ASSERT(trees[i]);
        EDIT_ASSERT(piece_init_copy(trees[i],(const uint8_t *)"small file\n",11)==0);
        EDIT_ASSERT(undo_init(&logs[i],trees[i],8)==0);
        undo_bytes+=sizeof logs[i]+undo_get_stats(&logs[i]).reserved_bytes;
        char title[128], path[128];
        (void)snprintf(title,sizeof title,"small-%03zu-中👩‍💻.txt",i);
        (void)snprintf(path,sizeof path,"/small/%03zu.txt",i);
        tabs_desc d={.buffer=trees[i],.undo=&logs[i],.title=title,.path=path,
            .title_len=strlen(title),.path_len=strlen(path),.modified=(i%3u)==0};
        uint64_t id; EDIT_ASSERT(tabs_open(&s,&d,&live,&id)==0);
    }
    char power[32], load[64]; bench_battery_status(power,sizeof power); load_stamp(load,sizeof load);
    size_t own=tabs_owned_bytes(&s), total=own+undo_bytes;
    int miss=total>1000000u;
    printf("tabs_bench: memory (M)%s load1=%s power=%s tabs=100 closed_capacity=16 "
           "tab_set_bytes=%zu caller_undo_bytes=%zu own_plus_undo_bytes=%zu "
           "gate_bytes=1000000(G) excluding_buffers=1 memory=%s verdict=TRACK\n",
           bench__tag_from_power(power),load,power,own,undo_bytes,total,miss?"MISS":"PASS");
    render_cell cells[2400]; uint64_t dirty[1]; render_grid g;
    render_glyph glyphs[97]; uint8_t pixels[194]={0};
    for(uint32_t i=0;i<97;++i) glyphs[i]=(render_glyph){.glyph_index=i<95?i+32:i==95?0x4e2d:0x1f469,
        .page=0,.x=i*2u,.w=i<95?1u:2u,.h=1};
    render_atlas_page page={pixels,sizeof pixels,194,194,1};
    for(size_t i=0;i<2400;++i) cells[i]=(render_cell){.atlas_slot=RENDER_NO_SLOT};
    EDIT_ASSERT(render_grid_init(&g,(render_dims){240,1,1,1},cells,2400,dirty,1)==0);
    g.pages=&page; g.page_count=1; g.glyphs=glyphs; g.glyph_count=97;
    miss|=run(&s,&live,&g,240,false,"switch+viewport_strip");
    EDIT_ASSERT(render_grid_validate(&g)==0);
    EDIT_ASSERT(render_grid_init(&g,(render_dims){2400,1,1,1},cells,2400,dirty,1)==0);
    g.pages=&page; g.page_count=1; g.glyphs=glyphs; g.glyph_count=97;
    miss|=run(&s,&live,&g,2400,true,"switch+all100_strip");
    EDIT_ASSERT(render_grid_validate(&g)==0);
    tabs_fini(&s);
    for(size_t i=0;i<100;++i) { undo_destroy(&logs[i]); piece_destroy(trees[i]); }
    return miss?1:0;
}
