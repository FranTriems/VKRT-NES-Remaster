// Local measurement tool; checksums and witnesses are outside timed intervals.
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_prepared.hpp"
#ifdef VKRT_M4R4
#include "vkrt/nes_native_s0/modern_scene.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace vkrt::nes;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
void stats(const char* name,std::vector<double> v){std::sort(v.begin(),v.end());std::cout<<name<<"_median_ms="<<v[v.size()/2]<<" "<<name<<"_p95_ms="<<v[(v.size()*95-1)/100]<<" ";}
int main(int argc,char**argv){
    if(argc!=3)return 2;
    std::ifstream input(argv[1],std::ios::binary);Bytes rom((std::istreambuf_iterator<char>(input)),{});
    const std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    Machine witness(rom);
    for(int n=1;n<=600;++n){witness.run_frames(1);if(n!=180&&n!=360&&n!=540&&n!=600)continue;
        const auto f=observe(*witness.frame());auto dest=dir/std::to_string(n);std::filesystem::create_directories(dest);
        std::ofstream receipt(dest/"witness.txt");receipt<<witness.state_hash()<<'\n'<<frame_hash(*witness.frame())<<'\n'<<frame_hash(*f)<<'\n'<<witness.audio_hash()<<'\n';
        for(int mode=0;mode<3;++mode){auto out=render(*f,Mode(mode));receipt<<sha256(out.rgb)<<'\n';write_ppm(dest/(std::to_string(mode)+".ppm"),out);}
        write_ppm(dest/"modern.ppm",render_modern(*f));
    }
    for(int w:{512,768}){
        Machine m(rom);for(int n=0;n<550;++n)m.run_frames(1);auto prev=observe(*m.frame());std::vector<double> emu,obs,rend,full;
#ifdef VKRT_M4R4
        ModernScene scene;
#endif
        std::uint64_t checksum=0;
        for(int n=0;n<50;++n){auto a=Clock::now();m.run_frames(1);auto b=Clock::now();
#ifdef VKRT_M4R4
            observe_modern(scene,m.frame());auto c=Clock::now();auto out=render_modern_scene(scene,{},w,w*240/256);
#else
            auto f=observe(*m.frame(),prev.get());auto c=Clock::now();auto out=render_modern(*f,{},w,w*240/256);prev=f;
#endif
            auto d=Clock::now();if(n>=10){emu.push_back(elapsed(a,b));obs.push_back(elapsed(b,c));rend.push_back(elapsed(c,d));full.push_back(elapsed(a,d));}
            for(auto byte:out.rgb)checksum=checksum*131+byte;
        }
        std::cout<<"width="<<w<<" ";stats("emulation",emu);stats("observe",obs);stats("render",rend);stats("full",full);std::cout<<"checksum="<<checksum<<'\n';
    }
    const auto f=observe(*witness.frame());std::vector<double> prep,raster,copy,validate,hash;
    auto p=prepare_modern(*f);Image image;
    for(int n=0;n<20;++n){auto a=Clock::now();prepare_modern_into(p,*f);auto b=Clock::now();raster_modern_into(image,p,512,480);auto c=Clock::now();
        auto owned=std::make_shared<Frame>(*f);auto d=Clock::now();validate_frame(*owned);auto e=Clock::now();auto h=frame_hash(*owned);auto end=Clock::now();
        if(n>1){prep.push_back(elapsed(a,b));raster.push_back(elapsed(b,c));copy.push_back(elapsed(c,d));validate.push_back(elapsed(d,e));hash.push_back(elapsed(e,end));}
    }
    stats("legacy_prepare",prep);stats("legacy_raster",raster);stats("frame_copy",copy);stats("validate",validate);stats("witness_hash",hash);std::cout<<'\n';
}
