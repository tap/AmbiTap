// libspatialaudio benchmark: encoder, rotator (AmbisonicProcessor),
// decoder (cube), binauralizer. 64-sample blocks @48kHz, median of 9x400.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include "Ambisonics.h"

using namespace spaudio;
static double now_us(){ timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1e6+ts.tv_nsec/1e3; }
#define RUNS 9
#define BLOCKS 400
#define NF 64
#define SR 48000

template<typename F> double bench(F f){
    std::vector<double> runs;
    for(int r=0;r<RUNS;r++){ double t0=now_us();
        for(int b=0;b<BLOCKS;b++) f();
        runs.push_back((now_us()-t0)/BLOCKS); }
    std::sort(runs.begin(),runs.end());
    return runs[RUNS/2];
}

int main(){
    printf("libspatialaudio benchmarks - %d-frame blocks, %d Hz, median of %dx%d\n\n",NF,SR,RUNS,BLOCKS);
    float src[NF]; for(int i=0;i<NF;i++) src[i]=sinf(0.01f*i);

    for(int order : {1,2,3}){
        BFormat bf; bf.Configure(order,true,NF);
        // fill with signal
        for(unsigned c=0;c<bf.GetChannelCount();c++){
            std::vector<float> tmp(NF);
            for(int i=0;i<NF;i++) tmp[i]=sinf(0.02f*i+c);
            bf.InsertStream(tmp.data(),c,NF);
        }

        { AmbisonicEncoder enc; enc.Configure(order,true,SR,10.f);
          PolarPosition<float> pos; pos.azimuth=0.6f; pos.elevation=0.3f; pos.distance=1.f;
          enc.SetPosition(pos); enc.Refresh();
          for(int w=0;w<50;w++) enc.Process(src,NF,&bf);
          double us=bench([&]{ enc.Process(src,NF,&bf); });
          printf("encoder    order %d   %8.2f us/block\n",order,us); }

        { AmbisonicRotator rot; rot.Configure(order,true,NF,SR,10.f);
          RotationOrientation o; o.yaw=0.6; o.pitch=0.2; o.roll=0.1; rot.SetOrientation(o);
          for(int w=0;w<50;w++) rot.Process(&bf,NF);
          double us=bench([&]{ rot.Process(&bf,NF); });
          printf("rotator    order %d   %8.2f us/block\n",order,us); }

        { AmbisonicDecoder dec;
          dec.Configure(order,true,NF,SR,Amblib_SpeakerSetUps::kAmblib_Cube);
          dec.Refresh();
          float** out=new float*[8]; for(int i=0;i<8;i++) out[i]=new float[NF];
          for(int w=0;w<50;w++) dec.Process(&bf,NF,out);
          double us=bench([&]{ dec.Process(&bf,NF,out); });
          printf("decoder/cube order %d %8.2f us/block\n",order,us); }

        { AmbisonicBinauralizer bin; unsigned tail=0;
          bool ok=bin.Configure(order,true,SR,NF,tail,"",true /*lowCpuMode*/);
          if(!ok){ printf("binaural   order %d   (built-in HRTF unsupported at this order)\n",order); }
          else {
            float** out=new float*[2]; for(int i=0;i<2;i++) out[i]=new float[NF];
            for(int w=0;w<50;w++) bin.Process(&bf,out);
            double us=bench([&]{ bin.Process(&bf,out); });
            printf("binaural   order %d   %8.2f us/block  (tail %u)\n",order,us,tail); }
        }
        printf("\n");
    }
    return 0;
}
