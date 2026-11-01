/* Font preparation may grow geometry; ordinary typing and resize only use it. */
#if defined(_WIN32)
#include "editor/private.h"
#include <string.h>

int editor_grid_prepare(editor *e,font_cell cell)
{
    /* (P) ceil(surface pixels / prepared cell pixels), including overscan.
     * Keep the inherited grid dimension bound and never shrink reservations. */
    uint32_t cols=e->font.max_width/cell.cell_w+(e->font.max_width%cell.cell_w!=0);
    uint32_t rows=e->font.max_height/cell.cell_h+(e->font.max_height%cell.cell_h!=0);
    if(cols<e->max_cols) cols=e->max_cols;
    if(rows<e->max_rows) rows=e->max_rows;
    if(cols==e->max_cols && rows==e->max_rows) return 0;
    if(cols>8192 || rows>8192) return EDITOR_ERR_CAPACITY;
    /* The highlight worker borrows its cell arrays. Font jobs retain their
     * old grid bindings until both atlas versions are idle. */
    if(e->hl.snapshot || !font_async_idle(e->font.atlas[0].fonts) ||
       !font_async_idle(e->font.atlas[1].fonts) || e->startup.pending) return EDITOR_MORE;
    size_t reserved_rows=(size_t)rows+1u, cells=(size_t)cols*reserved_rows;
    size_t words=(reserved_rows+63u)/64u, strips=reserved_rows/2u;
    /* (P) Exact array payload plus alignment slack for the allocations below.
     * Pixel surfaces, atlas and minimap retain their existing reservations. */
    size_t bytes=cells*(3u*sizeof(hl_span)+sizeof(editor_hit)+2u*sizeof(render_cell))+
        reserved_rows*(2u*sizeof(layout_wrap_row)+sizeof(uint64_t)+sizeof(uint32_t))+
        (size_t)rows*sizeof(indent_range)+strips*sizeof(render_strip)+
        words*(2u+e->font_row_slots)*sizeof(uint64_t);
    edit_arena next={0};
    /* At most one alignment unit per allocation; types are all <= max_align_t. */
    enum { ALLOCATIONS=14 }; /* (P) the allocations below, including three spans. */
    if(edit_arena_init(&next,bytes+ALLOCATIONS*_Alignof(max_align_t))) return EDITOR_ERR_MEMORY;
#define GRID_ALLOC(type,name,count) type *name=edit_arena_alloc(&next,(count)*sizeof(type),_Alignof(type))
    GRID_ALLOC(hl_span,worker,cells);
    GRID_ALLOC(hl_span,staged,cells);
    GRID_ALLOC(hl_span,published,cells);
    GRID_ALLOC(editor_hit,hits,cells);
    GRID_ALLOC(render_cell,full,cells);
    GRID_ALLOC(render_cell,text,cells);
    GRID_ALLOC(layout_wrap_row,wrap,2u*reserved_rows);
    GRID_ALLOC(uint64_t,row_byte,reserved_rows);
    GRID_ALLOC(uint32_t,row_used,reserved_rows);
    GRID_ALLOC(indent_range,ws_ranges,rows);
    GRID_ALLOC(render_strip,strip,strips);
    GRID_ALLOC(uint64_t,full_bits,words);
    GRID_ALLOC(uint64_t,text_bits,words);
    GRID_ALLOC(uint64_t,font_rows,words*e->font_row_slots);
#undef GRID_ALLOC
    if(!worker || !staged || !published || !hits || !full || !text || !wrap ||
       !row_byte || !row_used || !ws_ranges || !strip || !full_bits || !text_bits || !font_rows) {
        edit_arena_free(&next); return EDITOR_ERR_MEMORY;
    }
    memset(wrap,0,2u*reserved_rows*sizeof *wrap);
    memset(font_rows,0,words*e->font_row_slots*sizeof *font_rows);
    memset(full_bits,0,words*sizeof *full_bits);
    memset(text_bits,0,words*sizeof *text_bits);
    /* Retire the backend on its existing exclusive lifecycle lane before
     * rebuilding its CPU snapshots and GPU instances for the larger grid.
     * The old UI storage stays valid throughout mailbox/native pumping. */
    render_config previous=e->backend->config, config=previous;
    void *state=e->backend->state;
    config.max_cells=cells;
    editor_backend_shutdown_sync(e,e->backend);
    int rc=editor_backend_init_sync(e,config,state);
    if(rc) {
        /* Retain the old font/storage if preparation cannot be completed. */
        int restore=editor_backend_init_sync(e,previous,state);
        edit_arena_free(&next); return restore ? restore : rc;
    }
    edit_arena old=e->grid_reservation;
    e->grid_reservation=next;
    e->max_cols=cols; e->max_rows=rows;
    e->hl.worker_cells=worker; e->hl.staged=staged; e->hl.published=published;
    e->hl.capacity=cells; e->hl.count=e->hl.staged_count=e->hl.published_count=0; e->hl.published_owner=0;
    memset(&e->hl.wanted,0,sizeof e->hl.wanted);
    e->hits=hits; e->row_byte=row_byte; e->row_used=row_used; e->ws_ranges=ws_ranges;
    e->strips=strip; e->strip_cap=strips;
    e->font_rows=font_rows; e->font_row_words=words;
    e->grid.cells=full; e->grid.cell_capacity=cells;
    e->grid.dirty=full_bits; e->grid.dirty_word_capacity=words;
    e->text_grid.cells=text; e->text_grid.cell_capacity=cells;
    e->text_grid.dirty=text_bits; e->text_grid.dirty_word_capacity=words;
    e->lay.wrap_rows=wrap; e->lay.wrap_plan=wrap+reserved_rows;
    e->lay.wrap_capacity=(uint32_t)reserved_rows;
    e->win_surface_valid=false; e->win_frame_owed=true;
    edit_arena_free(&old);
    return 0;
}
#endif
