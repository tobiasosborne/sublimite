/* Variant: realistic ~25% glyph coverage + SSE2 blend kernel (4 px/iter, 16-bit math). No X: pure raster timing. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <emmintrin.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int W=2880,H=1800,CW=18,CH=36; static uint32_t*fb; static uint8_t*atlas; static int use_simd=1;
static inline void blend4(uint32_t*d,const uint8_t*a){ /* fg = 0xd0d0d0 */
  __m128i zero=_mm_setzero_si128();
  __m128i al=_mm_cvtsi32_si128(*(const uint32_t*)a); /* 4 alphas */
  al=_mm_unpacklo_epi8(al,zero); /* 4 x u16 alpha in low 64 */
  __m128i a16=_mm_unpacklo_epi16(al,al); /* a0 a0 a1 a1 a2 a2 a3 a3 */
  __m128i alo=_mm_unpacklo_epi32(a16,a16), ahi=_mm_unpackhi_epi32(a16,a16); /* per-channel alpha for px0,1 / px2,3 */
  __m128i p=_mm_loadu_si128((const __m128i*)d);
  __m128i plo=_mm_unpacklo_epi8(p,zero), phi=_mm_unpackhi_epi8(p,zero);
  __m128i fg=_mm_set1_epi16(0xd0), m255=_mm_set1_epi16(255);
  __m128i rlo=_mm_add_epi16(_mm_mullo_epi16(plo,_mm_sub_epi16(m255,alo)),_mm_mullo_epi16(fg,alo));
  __m128i rhi=_mm_add_epi16(_mm_mullo_epi16(phi,_mm_sub_epi16(m255,ahi)),_mm_mullo_epi16(fg,ahi));
  rlo=_mm_srli_epi16(_mm_add_epi16(rlo,_mm_srli_epi16(rlo,8)),8); rhi=_mm_srli_epi16(_mm_add_epi16(rhi,_mm_srli_epi16(rhi,8)),8); /* /255 approx */
  _mm_storeu_si128((__m128i*)d,_mm_or_si128(_mm_packus_epi16(rlo,rhi),_mm_set1_epi32(0xff000000)));
}
static void raster_rows(int y0,int y1,int seed){
  uint32_t bg=0xff202020u;
  for(int y=y0;y<y1;y++){ uint32_t*row=fb+(size_t)y*W; __m128i b=_mm_set1_epi32(bg); for(int x=0;x<W;x+=4)_mm_storeu_si128((__m128i*)(row+x),b); }
  int cols=W/CW; for(int cy=y0/CH; cy<(y1+CH-1)/CH; cy++){ if(cy*CH>=H)break;
    for(int cx=0;cx<cols;cx++){ int g=(cx*7+cy*13+seed)%95; const uint8_t*ga=atlas+(size_t)g*CW*CH;
      for(int yy=0;yy<CH;yy++){ int y=cy*CH+yy; if(y<y0||y>=y1||y>=H)continue; uint32_t*d=fb+(size_t)y*W+cx*CW; const uint8_t*a=ga+yy*CW;
        if(use_simd){ for(int xx=0;xx<CW;xx+=4){ if(*(const uint32_t*)(a+xx)==0)continue; blend4(d+xx,a+xx);} /* CW=18 -> last chunk reads 2 bytes past row; atlas padded */ }
        else for(int xx=0;xx<CW;xx++){ uint32_t al=a[xx]; if(!al)continue; uint32_t p=d[xx];
          uint32_t r=((p>>16&255)*(255-al)+0xd0*al)/255, gch=((p>>8&255)*(255-al)+0xd0*al)/255, b=((p&255)*(255-al)+0xd0*al)/255;
          d[xx]=0xff000000u|(r<<16)|(gch<<8)|b; } } } }
}
typedef struct{int y0,y1,seed;}band; static void*bandfn(void*v){band*b=v;raster_rows(b->y0,b->y1,b->seed);return 0;}
static double raster_mt(int n,int seed){ pthread_t th[8]; band bd[8]; double t0=now(); for(int i=0;i<n;i++){bd[i].y0=H*i/n;bd[i].y1=H*(i+1)/n;bd[i].seed=seed;pthread_create(&th[i],0,bandfn,&bd[i]);} for(int i=0;i<n;i++)pthread_join(th[i],0); return (now()-t0)*1e3; }
int main(int argc,char**argv){ setvbuf(stdout,NULL,_IOLBF,0);
  int cov=argc>1?atoi(argv[1]):25; use_simd=argc>2?atoi(argv[2]):1; if(argc>4){W=atoi(argv[3]);H=atoi(argv[4]);}
  fb=aligned_alloc(64,(size_t)W*H*4+64); atlas=calloc(95*CW*CH+64,1);
  srand(7); for(int i=0;i<95*CW*CH;i++) atlas[i]=(rand()%100<cov)?(64+rand()%192):0; /* cov % of pixels have coverage */
  printf("%dx%d coverage=%d%% kernel=%s\n",W,H,cov,use_simd?"SSE2":"scalar");
  struct timespec idle={15,0}; nanosleep(&idle,0);
  double t0=now(); raster_rows(0,H,0); printf("ST after 15s idle: %.2f ms\n",(now()-t0)*1e3);
  double best=1e9,sum=0; for(int r=0;r<10;r++){ t0=now(); raster_rows(0,H,r); double d=(now()-t0)*1e3; sum+=d; if(d<best)best=d; } printf("ST warmed: best %.2f ms, mean %.2f ms\n",best,sum/10);
  nanosleep(&idle,0); printf("MT4 after 15s idle: %.2f ms\n",raster_mt(4,1));
  best=1e9; for(int r=0;r<5;r++){ double d=raster_mt(4,r); if(d<best)best=d;} printf("MT4 warmed best: %.2f ms\n",best);
  best=1e9; for(int r=0;r<5;r++){ double d=raster_mt(8,r); if(d<best)best=d;} printf("MT8 warmed best: %.2f ms\n",best);
  t0=now(); raster_rows(CH*20,CH*21,3); printf("one-line strip: %.3f ms\n",(now()-t0)*1e3);
  return fb[12345]&1; }
