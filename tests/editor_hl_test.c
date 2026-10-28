/* edit-11v: (P) byte offsets/geometry derive from this C fixture; first row
 * and column are zero, next ordinal is one. Refresh interval = ns/second
 * divided by panel Hz from perf/00-hardware-profile.md (M)[bat]; synthetic
 * timestamps exercise the caller contract, not a new display measurement.
 * The wait window reuses editor_test's existing settle window (E). */
#include "editor/private.h"
#include "trace/trace.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include "hl/lang.h"
#define T(c) do { if (!(c)) { fprintf(stderr,"editor_hl_test: FAIL %s\n",#c); return 1; } } while (0)
typedef struct held_worker { _Atomic bool started, release; } held_worker;
static void hold(work_ctx *ctx)
{
    held_worker *held=ctx->arg;
    atomic_store(&held->started,true);
    while (!atomic_load(&held->release) && !work_should_stop(ctx)) sched_yield();
}
static int text_ready(editor *e)
{
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000); /* E: existing settle window */
    while (trace_now_ns()<deadline) {
        T(editor_step(e,0)>=0);
        editor_stats stats=editor_get_stats(e);
        if (!stats.pending && !stats.render_active) return 0;
    }
    T(false); return 1;
}
static int syntax_ready(editor *e)
{
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000); /* E: existing settle window */
    do {
        T(editor_step(e,0)>=0);
        if (!e->hl.snapshot && e->hl.published_owner==e->buffer->id && e->hl.published_count)
            return text_ready(e);
    } while (trace_now_ns()<deadline);
    T(false); return 1;
}
static int start_hold(editor *e, held_worker *held)
{
    atomic_init(&held->started,false); atomic_init(&held->release,false);
    T(work_submit(&e->pool,(work_job){hold,held,0,WORK_BULK}).epoch);
    uint64_t deadline=trace_now_ns()+UINT64_C(5000000000); /* E: existing settle window */
    while (!atomic_load(&held->started)) { T(editor_step(e,0)>=0); T(trace_now_ns()<deadline); }
    return 0;
}
int main(void)
{
    trace_init(); T(trace_thread_register() >= 0);
    const char source[] = "int main(void) { return 0; }\n";
    char dir[] = "build/hl-editor-XXXXXX"; T(mkdtemp(dir));
    char path[IPC_PATH_CAP]; T(snprintf(path,sizeof path,"%s/fixture.c",dir) > 0);
    int fd = open(path,O_CREAT|O_WRONLY,S_IRUSR|S_IWUSR); T(fd >= 0);
    T(write(fd,source,sizeof source-1u) == (ssize_t)(sizeof source-1u)); close(fd);
    render_backend backend = {0}; T(render_null_backend(&backend) == 0);
    editor_config cfg = {.path=path,.cols=sizeof source + 5u + 8u + 1u, /* (P) gutter + map + outer margin */.rows=sizeof source,.wrap_mode=-1};
    editor *e = NULL; T(editor_open(&e,&cfg,&backend) == 0);
    held_worker held; T(start_hold(e,&held)==0);
    uint64_t interval = UINT64_C(1000000000)/90u;
    T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false) == 0);
    T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false) == 0);
    uint32_t keyword = config_theme_color(e->preferences.theme,"syntax.keyword");
    size_t offset = (size_t)(strstr(source,"return")-source);
    T(text_ready(e)==0);
    T(e->hl.snapshot && !e->hl.published_count);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+offset].fg ==
      config_theme_color(e->preferences.theme,"editor.foreground"));
    atomic_store(&held.release,true); T(syntax_ready(e)==0);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+offset].fg == keyword);
    /* The space following int has no query capture. */
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+strlen("int")].fg ==
      config_theme_color(e->preferences.theme,"editor.foreground"));
    /* Hold the parser during a mutation: the UI retains the published scopes. */
    T(start_hold(e,&held)==0);
    plat_event slash={.kind=PLAT_EV_KEY,.press=true,.keysym='/',.utf8={'/'},.utf8_len=1};
    T(editor_inject(e,&slash)==0); T(editor_inject(e,&slash)==0); T(text_ready(e)==0);
    uint32_t retained=0; editor_hl_paint(e,offset,&retained); T(retained==keyword);
    atomic_store(&held.release,true); T(syntax_ready(e)==0);
    offset+=strlen("//");
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+offset].fg ==
      config_theme_color(e->preferences.theme,"syntax.comment"));
    /* Theme uses the same published scope without recompiling a query. */
    e->preferences.theme=CONFIG_LIGHT; editor_theme_cache(e, CONFIG_LIGHT); T(editor_full_layout(e)==0); T(text_ready(e)==0);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+offset].fg ==
      config_theme_color(CONFIG_LIGHT,"syntax.comment"));
    /* A new buffer must not inherit byte-identical old captures. */
    uint64_t id; T(editor_add_buffer(e,NULL,(const uint8_t *)source,sizeof source-1u,&id)==0);
    T(text_ready(e)==0); T(!e->hl.published_count);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+offset-strlen("//")].fg ==
      e->layout_cfg.fg);
    T(hl_language_path("fixture.c")==hl_language_c());
    T(hl_language_path("fixture.rs")==hl_language_rust());
    T(hl_language_path("fixture.py")==hl_language_python());
    T(hl_language_path("fixture.js")==hl_language_javascript());
    T(hl_language_path("fixture.json")==hl_language_json());
    T(hl_language_path("fixture.toml")==hl_language_toml());
    T(hl_language_path("fixture.yaml")==hl_language_yaml());
    T(!hl_language_path("fixture.txt") && !hl_language_path("fixture.jl") && !hl_language_path("fixture.ts"));
    editor_close(e);
    /* (P) Two mailbox-sized viewport windows ensure more span messages than
     * fit in a ring and enough source for a real scroll. This derives work
     * from the transport ABI rather than inventing a capture budget. */
    fd=open(path,O_WRONLY|O_TRUNC); T(fd>=0);
    for (size_t row=0;row<WORK_MAILBOX_CAP+WORK_MAILBOX_CAP;row++)
        T(write(fd,source,sizeof source-1u)==(ssize_t)(sizeof source-1u));
    close(fd);
    cfg.rows=WORK_MAILBOX_CAP+1u; cfg.max_rows=cfg.rows;
    T(render_null_backend(&backend)==0); T(editor_open(&e,&cfg,&backend)==0);
    T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false)==0);
    T(editor_on_refresh(e,0,e->refresh_sequence+1u,e->refresh_ns+interval,false)==0);
    T(syntax_ready(e)==0); T(e->hl.published_count>WORK_MAILBOX_CAP);
    size_t return_byte=(size_t)(strstr(source,"return")-source);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+return_byte].fg==keyword);
    uint64_t top=editor_view(e).first_byte;
    uint32_t generation=e->hl.generation;
    plat_event wheel={.kind=PLAT_EV_WHEEL,.dy=PLAT_WHEEL_UNIT};
    T(editor_inject(e,&wheel)==0); T(text_ready(e)==0); T(syntax_ready(e)==0);
    T(editor_view(e).first_byte!=top && e->hl.generation>generation);
    T(e->grid.cells[(size_t)e->tab_rows*e->grid.dims.cols+e->lay.gw+return_byte].fg==keyword);
    editor_close(e); unlink(path); rmdir(dir);

    puts("editor_hl_test: PASS null frame: plain/published C, edit/stale, light theme, new buffer, default/optional dispatch, mailbox backpressure/scroll"); return 0;
}
