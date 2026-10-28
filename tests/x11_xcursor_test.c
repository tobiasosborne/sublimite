#include "x11/private.h"
#include "xcursor_fixture.h"
#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define T(x) do { if (!(x)) { fprintf(stderr,"x11_xcursor_test:%d: %s\n",__LINE__,#x); failures++; } } while (0)
static void event(void *ctx, const plat_event *ev)
{ if (ev->kind==PLAT_EV_LEAVE) (*(unsigned *)ctx)++; }
int main(void)
{
    char root[]="/tmp/x11-cursor-test-XXXXXX"; T(mkdtemp(root));
    const char *dirs[]={".icons",".icons/test",".icons/test/cursors"}; char path[4096];
    for (size_t i=0;i<sizeof dirs/sizeof *dirs;i++) { (void)snprintf(path,sizeof path,"%s/%s",root,dirs[i]); T(mkdir(path,0700)==0); }
    uint8_t bytes[256]; size_t size=test_xcursor_fixture(bytes);
    const char *names[]={"default","text"}; /* force both aliases */
    for (size_t i=0;i<2;i++) {
        (void)snprintf(path,sizeof path,"%s/.icons/test/cursors/%s",root,names[i]);
        FILE *f=fopen(path,"wb"); T(f); if (f) { T(fwrite(bytes,1,size,f)==size); fclose(f); }
    }
    T(setenv("HOME",root,1)==0 && setenv("XCURSOR_THEME","test",1)==0 && setenv("XCURSOR_SIZE","31",1)==0);
    plat p; plat_config cfg={.title="xcursor fixture",.width=128,.height=128}; /* fixture window */
    int rc=plat_init(&p,&cfg); T(rc==PLAT_OK);
    if (!rc) {
        x11_plat *x=x11_private(&p); T(x->cursors[0] && x->cursors[1]);
        xcb_connection_t *c=x->conn;
        unsigned before=xcb_no_operation(c).sequence;
        plat_set_cursor(&p,PLAT_CURSOR_DEFAULT);
        T(xcb_no_operation(c).sequence==before+1u); /* only our probe request */
        before=xcb_no_operation(c).sequence;
        plat_set_cursor(&p,PLAT_CURSOR_TEXT); plat_set_cursor(&p,PLAT_CURSOR_TEXT);
        T(x->cursor_shape==PLAT_CURSOR_TEXT && xcb_no_operation(c).sequence==before+2u);
        plat_set_cursor(&p,PLAT_CURSOR_DEFAULT);
        plat_map(&p);
        xcb_leave_notify_event_t leave={.response_type=XCB_LEAVE_NOTIFY,.event=x->win,.same_screen_focus=1};
        xcb_send_event(c,0,x->win,XCB_EVENT_MASK_LEAVE_WINDOW,(const char *)&leave); xcb_flush(c);
        unsigned leaves=0; plat_callbacks cb={.ud=&leaves,.on_event=event};
        T(plat_run_for(&p,&cb,100)==0 && leaves>0); /* (E) existing X11 suite's bounded event pump */
        plat_shutdown(&p);
    }
    T(setenv("XCURSOR_THEME","missing-cursor-fixture",1)==0);
    rc=plat_init(&p,&cfg); T(rc==PLAT_OK);
    if (!rc) {
        x11_plat *x=x11_private(&p); T(!x->cursors[0] && !x->cursors[1]);
        unsigned before=xcb_no_operation(x->conn).sequence;
        plat_set_cursor(&p,PLAT_CURSOR_TEXT);
        T(xcb_no_operation(x->conn).sequence==before+1u); plat_shutdown(&p);
    }
    for (size_t i=0;i<2;i++) { (void)snprintf(path,sizeof path,"%s/.icons/test/cursors/%s",root,names[i]); unlink(path); }
    for (size_t i=sizeof dirs/sizeof *dirs;i;i--) { (void)snprintf(path,sizeof path,"%s/%s",root,dirs[i-1]); T(rmdir(path)==0); }
    T(rmdir(root)==0);
    if (failures) return 1;
    puts("x11_xcursor_test: themed ARGB cursors, aliases, cached changes, leave event and failure fallback passed"); return 0;
}
