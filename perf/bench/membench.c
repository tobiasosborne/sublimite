#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
/* pointer-chase latency over a random cyclic permutation of n cache lines */
static double chase(size_t bytes){
  size_t n=bytes/64; uint64_t*a=aligned_alloc(64,n*64); size_t*p=malloc(n*sizeof(size_t));
  for(size_t i=0;i<n;i++)p[i]=i;
  srand(1); for(size_t i=n-1;i>0;i--){size_t j=rand()%(i+1);size_t t=p[i];p[i]=p[j];p[j]=t;}
  for(size_t i=0;i<n;i++)a[p[i]*8]=p[(i+1)%n];
  size_t idx=0; size_t iters = bytes<=4<<20 ? 20000000 : 5000000;
  for(size_t i=0;i<1000000&&i<n;i++)idx=a[idx*8];
  double t0=now(); for(size_t i=0;i<iters;i++)idx=a[idx*8]; double dt=now()-t0;
  volatile size_t sink=idx;(void)sink; free(a);free(p); return dt/iters*1e9;
}
typedef struct{char*src,*dst;size_t n;int reps;}job;
static void*cpy(void*v){job*j=v;for(int r=0;r<j->reps;r++)memcpy(j->dst,j->src,j->n);return 0;}
static void*rd(void*v){job*j=v;uint64_t s=0;for(int r=0;r<j->reps;r++){uint64_t*p=(uint64_t*)j->src;for(size_t i=0;i<j->n/8;i+=4)s+=p[i]+p[i+1]+p[i+2]+p[i+3];}*(volatile uint64_t*)j->dst=s;return 0;}
static void bw(int threads,size_t per,int reps,void*(*fn)(void*),const char*name){
  pthread_t th[16];job jb[16];
  for(int i=0;i<threads;i++){jb[i].src=malloc(per);jb[i].dst=malloc(per);memset(jb[i].src,1,per);memset(jb[i].dst,2,per);jb[i].n=per;jb[i].reps=reps;}
  for(int i=0;i<threads;i++)pthread_create(&th[i],0,fn,&jb[i]);
  double t0=now();for(int i=0;i<threads;i++)pthread_join(th[i],0);double dt=now()-t0; /* approx, includes thread start */
  double bytes=(double)threads*per*reps*(fn==cpy?2:1);
  printf("%-28s threads=%2d  %.1f GB/s\n",name,threads,bytes/dt/1e9);
  for(int i=0;i<threads;i++){free(jb[i].src);free(jb[i].dst);}
}
int main(){
  size_t sizes[]={16<<10,32<<10,256<<10,1<<20,2<<20,8<<20,32<<20,256<<20,1024u<<20};
  const char*lab[]={"16K L1","32K L1","256K L2","1M L2","2M L2/L3","8M L3","32M DRAM","256M DRAM","1G DRAM"};
  for(int i=0;i<9;i++)printf("latency %-10s %6.1f ns\n",lab[i],chase(sizes[i]));
  bw(1,256<<20,4,rd,"seq read 1 thread");
  bw(1,256<<20,4,cpy,"memcpy 1 thread (r+w)");
  bw(4,128<<20,4,rd,"seq read 4 threads");
  bw(8,64<<20,4,rd,"seq read 8 threads");
  bw(8,64<<20,4,cpy,"memcpy 8 threads (r+w)");
  bw(1,1<<20,2000,cpy,"memcpy 1 thread L2-resident");
  return 0;
}
