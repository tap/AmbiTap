/* SAF benchmark: ambi_enc, rotator, ambi_dec, ambi_bin at orders 1/3/5.
 * Methodology mirrors AmbiTap bench/: 48 kHz, per-block timing, median of
 * 9 runs x 400 blocks. SAF examples process at their compiled frame size
 * (reported); results are normalized to us per 64-sample block for
 * comparison. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "ambi_enc.h"
#include "ambi_dec.h"
#include "ambi_bin.h"
#include "rotator.h"
#include "_common.h"

#define SR 48000.0f
#define RUNS 9
#define BLOCKS 400
#define MAXCH 64

static double now_us(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1e6 + ts.tv_nsec/1e3;
}
static int cmpd(const void*a,const void*b){double d=*(double*)a-*(double*)b;return d<0?-1:d>0;}
static double median(double*v,int n){qsort(v,n,sizeof(double),cmpd);return v[n/2];}

static float* bufs[MAXCH]; static float* obufs[MAXCH];
static void alloc_bufs(int frame){
    for(int i=0;i<MAXCH;i++){
        bufs[i]=calloc(frame,sizeof(float));
        obufs[i]=calloc(frame,sizeof(float));
        for(int j=0;j<frame;j++) bufs[i][j]=sinf(0.01f*j+i);
    }
}

typedef void (*proc_fn)(void*, const float*const*, float*const*, int,int,int);

static double bench(void* h, proc_fn fn, int nin, int nout, int frame){
    double runs[RUNS];
    for(int r=0;r<RUNS;r++){
        double t0=now_us();
        for(int b=0;b<BLOCKS;b++)
            fn(h,(const float*const*)bufs,obufs,nin,nout,frame);
        runs[r]=(now_us()-t0)/BLOCKS;
    }
    return median(runs,RUNS);
}

int main(void){
    int orders[3]={SH_ORDER_FIRST,SH_ORDER_THIRD,SH_ORDER_FIFTH};
    int onum[3]={1,3,5};
    printf("SAF benchmarks - 48 kHz, median of %d runs x %d blocks\n",RUNS,BLOCKS);
    printf("frame sizes: enc=%d rot=%d dec=%d bin=%d\n\n",
        ambi_enc_getFrameSize(),rotator_getFrameSize(),
        ambi_dec_getFrameSize(),ambi_bin_getFrameSize());
    alloc_bufs(4096);

    for(int i=0;i<3;i++){
        int o=orders[i]; int nsh=(onum[i]+1)*(onum[i]+1);

        /* encoder: 1 mono source -> nsh */
        { void*h; ambi_enc_create(&h); ambi_enc_init(h,SR);
          ambi_enc_setOutputOrder(h,o); ambi_enc_setNumSources(h,1);
          int f=ambi_enc_getFrameSize();
          /* warmup */ for(int w=0;w<50;w++) ambi_enc_process(h,(const float*const*)bufs,obufs,1,nsh,f);
          double us=bench(h,ambi_enc_process,1,nsh,f);
          printf("encoder   order %d  frame %3d  %8.2f us/frame  %8.2f us per 64\n",onum[i],f,us,us*64.0/f);
          ambi_enc_destroy(&h); }

        /* rotator */
        { void*h; rotator_create(&h); rotator_init(h,SR);
          rotator_setOrder(h,o); rotator_setYaw(h,37.0f); rotator_setPitch(h,10.0f);
          int f=rotator_getFrameSize();
          for(int w=0;w<50;w++) rotator_process(h,(const float*const*)bufs,obufs,nsh,nsh,f);
          double us=bench(h,rotator_process,nsh,nsh,f);
          printf("rotator   order %d  frame %3d  %8.2f us/frame  %8.2f us per 64\n",onum[i],f,us,us*64.0/f);
          rotator_destroy(&h); }

        /* decoder: 8-speaker preset */
        { void*h; ambi_dec_create(&h); ambi_dec_init(h,SR);
          ambi_dec_setMasterDecOrder(h,o);
          ambi_dec_setOutputConfigPreset(h,LOUDSPEAKER_ARRAY_PRESET_8PX);
          ambi_dec_initCodec(h);
          while(ambi_dec_getCodecStatus(h)!=CODEC_STATUS_INITIALISED){
              ambi_dec_initCodec(h); }
          int f=ambi_dec_getFrameSize();
          for(int w=0;w<50;w++) ambi_dec_process(h,(const float*const*)bufs,obufs,nsh,8,f);
          double us=bench(h,ambi_dec_process,nsh,8,f);
          printf("decoder/8 order %d  frame %3d  %8.2f us/frame  %8.2f us per 64\n",onum[i],f,us,us*64.0/f);
          ambi_dec_destroy(&h); }

        /* binaural */
        { void*h; ambi_bin_create(&h); ambi_bin_init(h,SR);
          ambi_bin_setInputOrderPreset(h,o);
          ambi_bin_initCodec(h);
          while(ambi_bin_getCodecStatus(h)!=CODEC_STATUS_INITIALISED){
              ambi_bin_initCodec(h); }
          int f=ambi_bin_getFrameSize();
          for(int w=0;w<50;w++) ambi_bin_process(h,(const float*const*)bufs,obufs,nsh,2,f);
          double us=bench(h,ambi_bin_process,nsh,2,f);
          printf("binaural  order %d  frame %3d  %8.2f us/frame  %8.2f us per 64\n",onum[i],f,us,us*64.0/f);
          ambi_bin_destroy(&h); }
        printf("\n");
    }
    return 0;
}
