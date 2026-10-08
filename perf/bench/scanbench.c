#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <emmintrin.h>
#include <immintrin.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
/* SSE2 newline count + non-ASCII flag, 64 B per iteration */
static uint64_t scan_sse2(const uint8_t*p,size_t n,int*nonascii){
  __m128i nl=_mm_set1_epi8('\n'); __m128i acc=_mm_setzero_si128(); uint64_t cnt=0; __m128i hi=_mm_setzero_si128();
  size_t i=0;
  for(;i+64<=n;i+=64){
    __m128i a=_mm_loadu_si128((const __m128i*)(p+i)),b=_mm_loadu_si128((const __m128i*)(p+i+16)),c=_mm_loadu_si128((const __m128i*)(p+i+32)),d=_mm_loadu_si128((const __m128i*)(p+i+48));
    hi=_mm_or_si128(hi,_mm_or_si128(_mm_or_si128(a,b),_mm_or_si128(c,d)));
    __m128i s=_mm_add_epi8(_mm_add_epi8(_mm_cmpeq_epi8(a,nl),_mm_cmpeq_epi8(b,nl)),_mm_add_epi8(_mm_cmpeq_epi8(c,nl),_mm_cmpeq_epi8(d,nl)));
    acc=_mm_sub_epi8(acc,s); /* -1 per match -> count */
    if(((i>>6)&31)==31){ cnt+=_mm_cvtsi128_si64(_mm_sad_epu8(acc,_mm_setzero_si128()))+_mm_extract_epi16(_mm_sad_epu8(acc,_mm_setzero_si128()),4); acc=_mm_setzero_si128(); }
  }
  cnt+=_mm_cvtsi128_si64(_mm_sad_epu8(acc,_mm_setzero_si128()))+_mm_extract_epi16(_mm_sad_epu8(acc,_mm_setzero_si128()),4);
  for(;i<n;i++)cnt+=p[i]=='\n';
  *nonascii=_mm_movemask_epi8(hi)!=0; return cnt;
}
__attribute__((target("avx2"))) static uint64_t scan_avx2(const uint8_t*p,size_t n,int*nonascii){
  __m256i nl=_mm256_set1_epi8('\n'); __m256i acc=_mm256_setzero_si256(); uint64_t cnt=0; __m256i hi=_mm256_setzero_si256(); size_t i=0;
  for(;i+128<=n;i+=128){
    __m256i a=_mm256_loadu_si256((const __m256i*)(p+i)),b=_mm256_loadu_si256((const __m256i*)(p+i+32)),c=_mm256_loadu_si256((const __m256i*)(p+i+64)),d=_mm256_loadu_si256((const __m256i*)(p+i+96));
    hi=_mm256_or_si256(hi,_mm256_or_si256(_mm256_or_si256(a,b),_mm256_or_si256(c,d)));
    __m256i s=_mm256_add_epi8(_mm256_add_epi8(_mm256_cmpeq_epi8(a,nl),_mm256_cmpeq_epi8(b,nl)),_mm256_add_epi8(_mm256_cmpeq_epi8(c,nl),_mm256_cmpeq_epi8(d,nl)));
    acc=_mm256_sub_epi8(acc,s);
    if(((i>>7)&31)==31){ __m256i sad=_mm256_sad_epu8(acc,_mm256_setzero_si256()); cnt+=_mm256_extract_epi64(sad,0)+_mm256_extract_epi64(sad,1)+_mm256_extract_epi64(sad,2)+_mm256_extract_epi64(sad,3); acc=_mm256_setzero_si256(); }
  }
  __m256i sad=_mm256_sad_epu8(acc,_mm256_setzero_si256()); cnt+=_mm256_extract_epi64(sad,0)+_mm256_extract_epi64(sad,1)+_mm256_extract_epi64(sad,2)+_mm256_extract_epi64(sad,3);
  for(;i<n;i++)cnt+=p[i]=='\n';
  *nonascii=_mm256_movemask_epi8(hi)!=0; return cnt;
}
int main(){
  size_t n=1000000000; uint8_t*buf=malloc(n); /* 1 GB decimal */
  for(size_t i=0;i<n;i++)buf[i]=(i%41==40)?'\n':'a'+(i%26); /* 40-byte lines */
  int na; uint64_t c; double t0,dt;
  for(int r=0;r<3;r++){ t0=now(); c=scan_sse2(buf,n,&na); dt=now()-t0; printf("SSE2 nl-count+ascii 1GB: %.1f ms  %.2f GB/s  (lines=%lu nonascii=%d)\n",dt*1e3,n/dt/1e9,(unsigned long)c,na); }
  for(int r=0;r<3;r++){ t0=now(); c=scan_avx2(buf,n,&na); dt=now()-t0; printf("AVX2 nl-count+ascii 1GB: %.1f ms  %.2f GB/s\n",dt*1e3,n/dt/1e9); }
  for(int r=0;r<3;r++){ t0=now(); const void*m=memchr(buf,'\0',n); dt=now()-t0; printf("glibc memchr (no match) 1GB: %.1f ms  %.2f GB/s %p\n",dt*1e3,n/dt/1e9,m); }
  /* 64 KiB scan from L2 */
  t0=now(); for(int r=0;r<10000;r++){ c+=scan_avx2(buf+((r*65536)%(8<<20)),65536,&na);} dt=now()-t0; printf("AVX2 64KiB chunk (L2/L3-resident): %.2f us each\n",dt/10000*1e6);
  /* clock ramp: sleep 2s idle then time a 20 MB scan immediately vs warmed */
  struct timespec s={2,0}; nanosleep(&s,0);
  t0=now(); scan_avx2(buf,20000000,&na); dt=now()-t0; printf("AVX2 20MB scan right after 2s idle: %.2f ms (%.2f GB/s)\n",dt*1e3,0.02/dt);
  for(int r=0;r<5;r++){ t0=now(); scan_avx2(buf,20000000,&na); dt=now()-t0; } printf("AVX2 20MB scan warmed clock: %.2f ms (%.2f GB/s)\n",dt*1e3,0.02/dt);
  nanosleep(&s,0); t0=now(); memcpy(buf+500000000,buf,20000000); dt=now()-t0; printf("memcpy 20MB right after 2s idle: %.2f ms\n",dt*1e3);
  for(int r=0;r<5;r++){ t0=now(); memcpy(buf+500000000,buf,20000000); dt=now()-t0; } printf("memcpy 20MB warmed: %.2f ms (%.1f GB/s payload)\n",dt*1e3,0.02/dt);
  return (int)(c&1);
}
