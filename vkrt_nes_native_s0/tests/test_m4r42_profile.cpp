// Optional offline qualification. Supply a local ROM and an evidence directory;
// no ROM, commercial asset or title-specific behavior is part of the product.
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_scene.hpp"
#include "vkrt/nes_native_s0/modern_color.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>

using namespace vkrt::nes;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
void report(const char* name,std::vector<double> values){
    std::sort(values.begin(),values.end());
    std::cout<<name<<"_median_ms="<<values[values.size()/2]<<' '<<name<<"_p95_ms="<<values[(values.size()*95-1)/100]<<' ';
}
int main(int argc,char** argv){
    if(argc!=3&&argc!=4){std::cerr<<"usage: vkrt_nes_m4r42_profile LOCAL_ROM EVIDENCE_DIR [FRAME_BUTTONS_TEXT]\n";return 2;}
    try{
        std::map<int,std::uint8_t> input_events;
        if(argc==4){
            std::ifstream events(argv[3]);if(!events)throw std::runtime_error("input trace unavailable");
            int frame,buttons;
            while(events>>frame>>buttons){
                if(frame<1||frame>2500||buttons<0||buttons>255||input_events.contains(frame))throw std::invalid_argument("invalid input trace");
                input_events[frame]=std::uint8_t(buttons);
            }
            if(!events.eof())throw std::invalid_argument("malformed input trace");
        }
        std::ifstream input(argv[1],std::ios::binary);Bytes rom((std::istreambuf_iterator<char>(input)),{});
        auto machine=std::make_unique<Machine>(rom);auto scene=std::make_unique<ModernScene>();
        const std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
        std::ofstream manifest(dir/"input.txt");manifest<<"rom_sha256="<<sha256(rom)<<'\n';
        std::vector<double> emu,obs,rend,full,large;Image image;std::uint64_t checksum=0;Encoder pcm_history;
        for(int n=1;n<=2500;++n){
            if(auto event=input_events.find(n);event!=input_events.end())machine->controllers[0].buttons=event->second;
            // Drain the bounded observation buffer between timed intervals.
            // Preserve every sample for the cumulative audio witness.
            if(n%500==1){for(auto s:machine->apu.pcm)pcm_history.u16(static_cast<std::uint16_t>(s));machine->apu.pcm.clear();}
            auto a=Clock::now();machine->run_frames(1);auto b=Clock::now();
            const bool timed=(n>550&&n<=670)||(n>2380&&n<=2500);
            if(timed){
                observe_modern(*scene,machine->frame());auto c=Clock::now();
                raster_modern_scene_into(image,*scene,{},512,480);auto d=Clock::now();
                if((n>570&&n<=670)||n>2400){emu.push_back(ms(a,b));obs.push_back(ms(b,c));rend.push_back(ms(c,d));full.push_back(ms(a,d));}
                for(auto v:image.rgb)checksum=checksum*131+v;
                auto e=Clock::now();raster_modern_scene_into(image,*scene,{},768,720);auto end=Clock::now();
                if((n>570&&n<=670)||n>2400)large.push_back(ms(e,end));
            }
            if(n==670||n==2500){
                std::cout<<"phase="<<(n==670?"early":"late")<<" samples="<<full.size()<<' ';
                report("emulation",emu);report("observe",obs);report("render512",rend);report("full512",full);report("render768",large);
                std::cout<<"checksum="<<checksum<<'\n'<<std::flush;
                emu.clear();obs.clear();rend.clear();full.clear();large.clear();
            }
            if(n!=180&&n!=360&&n!=540&&n!=600&&n!=1200&&n!=1800&&n!=2400)continue;
            const auto f=machine->frame();observe_modern(*scene,f);
            const auto canonical_hash=frame_hash(*f),state_hash=machine->state_hash(),audio_hash=machine->audio_hash();
            auto dest=dir/std::to_string(n);std::filesystem::create_directories(dest);
            std::ofstream witness(dest/"witness.txt");
            Encoder pcm=pcm_history;for(auto s:machine->apu.pcm)pcm.u16(static_cast<std::uint16_t>(s));
            witness<<state_hash<<'\n'<<canonical_hash<<'\n'<<sha256(pcm.bytes)<<'\n';
            auto legacy=observe(*f);
            for(int mode=0;mode<3;++mode){auto out=render(*legacy,Mode(mode));witness<<sha256(out.rgb)<<'\n';}
            auto modern=render_modern_scene(*scene);
            if(modern!=render_modern_scene(*scene))throw std::runtime_error("repeat mismatch");
            write_ppm(dest/"modern.ppm",modern);
            Image source{256,240,Bytes(pixels*3)};
            for(int i=0;i<pixels;++i){auto c=modern_source_color(f->indices[i],f->effects[i]);for(int k=0;k<3;++k)source.rgb[i*3+k]=c[k];}
            write_ppm(dest/"source.ppm",source);
            auto frame_copy=std::make_shared<Frame>(*f);frame_copy->number+=10000;
            observe_modern(*scene,frame_copy);
            if(render_modern_scene(*scene)!=modern)throw std::runtime_error("static shimmer");
            if(machine->frame()!=f||frame_hash(*f)!=canonical_hash||machine->state_hash()!=state_hash||machine->audio_hash()!=audio_hash)
                throw std::runtime_error("canonical witness changed");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
