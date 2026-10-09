#define TABS_MODEL_ONLY
#include "../tests/tabs_test.c"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tabs_set s; CHECK(tabs_init(&s,MODEL_CAP,MODEL_CLOSED)==0);
    tabs_model m={.next=1}; view_state live={0};
    /* Model driver uses borrowed handles without dereferencing. */
    undo_log undo={0}; uint8_t token=0;
    piece_tree *buffer=(piece_tree *)(void *)&token;
    for(size_t i=0;i+2<size && i<3072;i+=3)
        model_op(&s,&m,&live,buffer,&undo,data[i],data[i+1],data[i+2]);

    /* Arbitrary byte titles and strip windows: validate wide pairs, widths,
     * row damage and untouched rows through the frozen renderer validator. */
    render_cell cells[192]; uint64_t dirty[1]; render_grid g;
    CHECK(render_grid_init(&g,(render_dims){64,3,1,1},cells,192,dirty,1)==0);
    for(uint32_t frame=1;frame<=16 && size; ++frame) {
        char title[TABS_TITLE_BYTES]; size_t n=size<TABS_TITLE_BYTES-1?size:TABS_TITLE_BYTES-1;
        for(size_t j=0;j<n;++j) { uint8_t byte=data[(j+frame)%size]; title[j]=byte?(char)byte:'_'; }
        if(!tabs_count(&s)) {
            tabs_desc d={.buffer=buffer,.undo=&undo,.title=title,.title_len=n}; uint64_t id;
            CHECK(tabs_open(&s,&d,&live,&id)==0);
        } else CHECK(tabs_rename(&s,0,title,n,NULL,0)==0);
        for(size_t j=0;j<192;++j) cells[j]=(render_cell){.atlas_slot=RENDER_NO_SLOT,.fg=7,.bg=8};
        CHECK(render_frame_begin(&g,frame)==0);
        uint8_t byte=data[(size_t)frame%size]; uint32_t offset=byte%32u;
        tabs_strip strip={.row=1,.first_col=offset,.col_count=64u-offset,
            .tab_cols=1u+(uint32_t)(byte%32u),.fg=1,.bg=2,.active_fg=3,.active_bg=4};
        CHECK(tabs_strip_render(&s,&g,&strip)==0 && render_grid_validate(&g)==0);
        CHECK(dirty[0]==2);
        for(size_t j=0;j<192;++j) if(j<64u+offset || j>=128u)
            CHECK(cells[j].fg==7 && cells[j].bg==8 && cells[j].atlas_slot==RENDER_NO_SLOT);
    }
    tabs_fini(&s); return 0;
}
