/* Full-frame CPU raster (bg fill + glyph alpha blend from atlas) into an XShm image, then XShmPutImage to a
   server-side pixmap and wait for ShmCompletion. Measures: raster ST, raster MT(4 bands), upload (server copy).
   Scenarios: after 15 s idle, then warmed. No window is mapped. */
#define _GNU_SOURCE
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int W,H,CW=18,CH=36; static uint32_t*fb; static uint8_t*atlas; /* 95 glyphs x CW x CH coverage */
static void raster_rows(int y0,int y1,int seed){
  uint32_t bg=0xff202020u, fg_r=0xd0,fg_g=0xd0,fg_b=0xd0;
  for(int y=y0;y<y1;y++){ uint32_t*row=fb+(size_t)y*W; for(int x=0;x<W;x++)row[x]=bg; }
  int cols=W/CW; for(int cy=y0/CH; cy<(y1+CH-1)/CH; cy++){ if(cy*CH>=H)break;
    for(int cx=0;cx<cols;cx++){ int g=(cx*7+cy*13+seed)%95; const uint8_t*ga=atlas+(size_t)g*CW*CH;
      for(int yy=0;yy<CH;yy++){ int y=cy*CH+yy; if(y<y0||y>=y1||y>=H)continue; uint32_t*d=fb+(size_t)y*W+cx*CW; const uint8_t*a=ga+yy*CW;
        for(int xx=0;xx<CW;xx++){ uint32_t al=a[xx]; if(!al)continue; uint32_t p=d[xx];
          uint32_t r=((p>>16&255)*(255-al)+fg_r*al)/255, gch=((p>>8&255)*(255-al)+fg_g*al)/255, b=((p&255)*(255-al)+fg_b*al)/255;
          d[xx]=0xff000000u|(r<<16)|(gch<<8)|b; } } } }
}
typedef struct{int y0,y1,seed;}band; static void*bandfn(void*v){band*b=v;raster_rows(b->y0,b->y1,b->seed);return 0;}
static double raster_mt(int n,int seed){ pthread_t th[8]; band bd[8]; double t0=now(); for(int i=0;i<n;i++){bd[i].y0=H*i/n;bd[i].y1=H*(i+1)/n;bd[i].seed=seed;pthread_create(&th[i],0,bandfn,&bd[i]);} for(int i=0;i<n;i++)pthread_join(th[i],0); return (now()-t0)*1e3; }
int main(int argc,char**argv){ setvbuf(stdout,NULL,_IOLBF,0);
  xcb_connection_t*c=xcb_connect(NULL,NULL); xcb_screen_t*s=xcb_setup_roots_iterator(xcb_get_setup(c)).data;
  W=s->width_in_pixels; H=s->height_in_pixels; if(argc>2){W=atoi(argv[1]);H=atoi(argv[2]);}
  size_t bytes=(size_t)W*H*4; int shmid=shmget(IPC_PRIVATE,bytes,IPC_CREAT|0600); fb=shmat(shmid,0,0);
  xcb_shm_seg_t seg=xcb_generate_id(c); xcb_generic_error_t*err=xcb_request_check(c,xcb_shm_attach_checked(c,seg,shmid,0)); if(err){printf("shm attach failed code %d\n",err->error_code);return 1;} shmctl(shmid,IPC_RMID,0);
  xcb_pixmap_t pm=xcb_generate_id(c); xcb_create_pixmap(c,s->root_depth,pm,s->root,W,H);
  xcb_gcontext_t gc=xcb_generate_id(c); xcb_create_gc(c,gc,pm,0,0); xcb_flush(c);
  atlas=malloc(95*CW*CH); for(int i=0;i<95*CW*CH;i++)atlas[i]=(i*2654435761u>>13)&255; /* pseudo coverage, ~all nonzero: pessimistic */
  printf("screen %dx%d  fb=%.1f MB  cells=%dx%d\n",W,H,bytes/1e6,W/CW,H/CH);
  struct timespec idle={15,0};
  for(int scen=0;scen<2;scen++){
    if(scen==0){ printf("-- sleeping 15 s (idle) --\n"); fflush(stdout); nanosleep(&idle,0);} else printf("-- warmed --\n");
    for(int r=0;r<(scen?5:1);r++){
      double t0=now(); raster_rows(0,H,r); double tr=(now()-t0)*1e3;
      t0=now(); xcb_shm_put_image(c,pm,gc,W,H,0,0,W,H,0,0,s->root_depth,XCB_IMAGE_FORMAT_Z_PIXMAP,0,seg,0); xcb_flush(c);
      {xcb_get_input_focus_reply_t*rp=xcb_get_input_focus_reply(c,xcb_get_input_focus(c),NULL); free(rp);} double tu=(now()-t0)*1e3;
      printf("ST raster %.2f ms  upload(sync roundtrip) %.2f ms  total %.2f ms\n",tr,tu,tr+tu);
    }
    if(scen==0){ nanosleep(&idle,0); double tm=raster_mt(4,9); printf("MT4 raster after idle %.2f ms\n",tm); }
    else { for(int r=0;r<3;r++){ double tm=raster_mt(4,r); printf("MT4 raster warmed %.2f ms\n",tm);} for(int r=0;r<2;r++){ double tm=raster_mt(8,r); printf("MT8 raster warmed %.2f ms\n",tm);} }
  }
  /* one-line strip update: raster one cell row + upload that strip */
  double t0=now(); raster_rows(CH*20,CH*21,3); double tr=(now()-t0)*1e3; t0=now();
  xcb_shm_put_image(c,pm,gc,W,H,0,CH*20,W,CH,0,CH*20,s->root_depth,XCB_IMAGE_FORMAT_Z_PIXMAP,0,seg,0); xcb_flush(c);
  {xcb_get_input_focus_reply_t*rp=xcb_get_input_focus_reply(c,xcb_get_input_focus(c),NULL); free(rp);} double tu=(now()-t0)*1e3;
  printf("one-line strip: raster %.3f ms upload %.3f ms\n",tr,tu);
  xcb_disconnect(c); return 0;
}
