#include "editor/private.h"
#include "journal/journal.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define T(c) do { if (!(c)) { fprintf(stderr,"editor_close_test:%d: RED %s\n",__LINE__,#c); return 1; } } while (0)
static int held_init(render_backend *b, const render_config *c) { (void)b; (void)c; return 0; }
static int held_resize(render_backend *b, render_dims d) { (void)b; (void)d; return 0; }
static int held_submit(render_backend *b, const render_grid *g, const render_strip *s, size_t n)
{ (void)b; (void)g; (void)s; (void)n; return 0; }
static int held_present(render_backend *b, uint32_t id) { (void)b; (void)id; return RENDER_ERR_BUSY; }
static int held_event(render_backend *b, const render_event *e) { (void)b; (void)e; return RENDER_ERR_UNSUPPORTED; }
static void held_shutdown(render_backend *b) { (void)b; }
static int replay_insert(void *ctx, const journal_record *record)
{
    unsigned *inserts = ctx;
    if (record->type != JOURNAL_INSERT) return 0;
    const uint8_t expected[9] = {0,0,0,0,0,0,0,0,'a'};
    if (record->size != sizeof expected || memcmp(record->data,expected,sizeof expected)) return 1;
    (*inserts)++; return 0;
}
static int close_case(bool native)
{
    render_backend b = {.info={"held frame",16,16,native ? 0u : RENDER_CAP_HEADLESS},
        .ops={held_init,held_resize,held_submit,held_present,held_event,held_shutdown}};
    char journal_path[] = "build/edit-close-journal-XXXXXX";
    int fd = mkstemp(journal_path); T(fd >= 0); close(fd);
    editor_config cfg = {.cols=16,.rows=2,.max_cols=16,.max_rows=2,.journal_path=journal_path};
    editor *e = NULL; T(editor_open(&e,&cfg,&b) == 0);
    for (unsigned i = 0; i < 1000 && !b.active; i++) T(editor_step(e,0) >= 0);
    T(b.active && !b.presented);
    for (unsigned i = 0; i < 16; i++) T(editor_step(e,0) >= 0);
    /* A queued close must bypass both a saturated input queue and an
     * unfinished view continuation; no command after close may mutate. */
    plat_event key = {.kind=PLAT_EV_KEY,.press=true,.keysym='a',.utf8_len=1,.utf8={'a'}};
    T(editor_inject(e,&key) == 0);
    for (unsigned i = 0; i < 100 && !editor_length(e); i++) T(editor_step(e,0) >= 0);
    T(editor_length(e) == 1 && e->op_count == 1);
    for (size_t i = 0; i < EDITOR_INPUT_CAP; i++) T(editor_inject(e,&key) == 0);
    e->v.busy = true;
    if (native) {
        plat *p = &e->platform;
        xcb_client_message_event_t msg = {.response_type=XCB_CLIENT_MESSAGE,.format=32,.window=p->win,
            .type=p->wm_protocols,.data.data32={p->wm_delete,0,0,0,0}};
        xcb_generic_error_t *err = xcb_request_check(p->conn,xcb_send_event_checked(p->conn,0,p->win,0,(const char *)&msg));
        T(!err); free(err);
    } else {
        plat_event close = {.kind=PLAT_EV_CLOSE};
        T(editor_inject(e,&close) == 0);
    }
    /* The already-delivered close returns in this turn, without waiting for
     * the next frame or resuming the blocked continuation. */
    T(editor_step(e,0) == EDITOR_CLOSED); T(editor_length(e) == 1); T(b.active);
    T(editor_flush(e) == 0); T(e->op_count == 0 && b.active);
    journal_stats stats = journal_get_stats(e->journal);
    T(stats.durable_sequence == stats.accepted_sequence && !stats.error);
    e->v.busy = false; editor_close(e);
    unsigned inserts = 0; journal_replay_result replay;
    T(journal_replay_file(journal_path,replay_insert,&inserts,&replay) == JOURNAL_OK);
    T(inserts == 1); T(unlink(journal_path) == 0);
    printf("editor_close_test: %s close in one turn; full input/view continuation/held Present bypassed; staged journal replay PASS\n",native ? "WM_DELETE_WINDOW" : "injected");
    return 0;
}
int main(int argc, char **argv)
{
    trace_init(); T(trace_thread_register() >= 0);
    if (argc == 2 && !strcmp(argv[1],"--native")) return close_case(true);
    T(close_case(false) == 0); T(close_case(true) == 0); return 0;
}
