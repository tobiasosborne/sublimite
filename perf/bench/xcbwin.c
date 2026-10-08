#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(){
  double t0=now();
  xcb_connection_t*c=xcb_connect(NULL,NULL); const xcb_setup_t*s=xcb_get_setup(c); xcb_screen_t*scr=xcb_setup_roots_iterator(s).data;
  double t1=now();
  xcb_window_t w=xcb_generate_id(c); uint32_t vals[2]={scr->white_pixel,XCB_EVENT_MASK_EXPOSURE};
  xcb_create_window(c,XCB_COPY_FROM_PARENT,w,scr->root,100,100,1440,900,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,scr->root_visual,XCB_CW_BACK_PIXEL|XCB_CW_EVENT_MASK,vals);
  xcb_gcontext_t gc=xcb_generate_id(c); uint32_t gv[1]={scr->black_pixel}; xcb_create_gc(c,gc,w,XCB_GC_FOREGROUND,gv);
  xcb_map_window(c,w); xcb_flush(c);
  double t2=now(), texp=0, tdraw=0;
  xcb_generic_event_t*e;
  while((e=xcb_wait_for_event(c))){ if((e->response_type&~0x80)==XCB_EXPOSE){ texp=now(); free(e); break;} free(e);}
  /* draw a screenful of "text" as rectangles: 142x47 cells, then sync roundtrip */
  xcb_rectangle_t*r=malloc(sizeof(xcb_rectangle_t)*6674); int k=0;
  for(int y=0;y<47;y++)for(int x=0;x<142;x++){r[k].x=x*9+2;r[k].y=y*18+4;r[k].width=5;r[k].height=10;k++;}
  xcb_poly_fill_rectangle(c,w,gc,k,r);
  xcb_get_input_focus_reply_t*rep=xcb_get_input_focus_reply(c,xcb_get_input_focus(c),NULL); free(rep);
  tdraw=now();
  printf("connect %.2f  create+map %.2f  ->expose %.2f  draw+sync %.2f  total-in-process %.2f ms\n",(t1-t0)*1e3,(t2-t1)*1e3,(texp-t2)*1e3,(tdraw-texp)*1e3,(tdraw-t0)*1e3);
  xcb_disconnect(c); return 0;
}
