#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/hash.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/runtime.hpp"
#include "vkrt/nes_native_s0/semantic.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vkrt::nes;
using namespace vkrt::nes::runtime;
namespace {
Bytes read_rom(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot open ROM");
    auto n=f.tellg();if(n<16||n>std::streamoff(32*1024*1024+528))throw std::runtime_error("ROM size invalid");
    Bytes b(static_cast<std::size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);
    if(!f)throw std::runtime_error("ROM read failed");return b;
}
struct Witness{std::string state,frame,audio;};
std::vector<Witness> execute(const Bytes& rom,const ReplayTape& tape,bool modern,std::uint64_t& checks){
    Machine m(rom);ImmutableFrame previous;std::vector<Witness> out;out.reserve(tape.frames().size());
    for(std::size_t i=0;i<tape.frames().size();++i){
        const auto in=tape.frames()[i];
        if(in.commands&kReplayWarmReset){m.reset();previous.reset();m.apu.pcm.clear();}
        m.controllers[0].buttons=in.buttons;
        m.run_frames(1);
        Witness w{m.state_hash(),frame_hash(*m.frame()),m.audio_hash()};
        ++checks;
        if(modern){
            previous=observe(*m.frame(),previous.get());
            if(!previous)throw std::runtime_error("modern observe null");
            if((i%3)==0){
                const auto before_state=m.state_hash();
                const auto before_frame=frame_hash(*m.frame());
                const auto before_audio=m.audio_hash();
                const auto im=render(*previous,Mode::MODERN_REMASTER,512,480);
                if(im.width!=512||im.height!=480||im.rgb.size()!=512u*480u*3u)
                    throw std::runtime_error("modern render invalid");
                if(m.state_hash()!=before_state||frame_hash(*m.frame())!=before_frame||m.audio_hash()!=before_audio)
                    throw std::runtime_error("presentation mutated canonical machine");
                checks+=3;
            }
        }
        out.push_back(std::move(w));
        m.apu.pcm.clear(); // host-owned observation buffer consumption only
    }
    return out;
}
}
int main(int argc,char** argv){
    try{
        Bytes rom=(argc>=2)?read_rom(argv[1]):synthetic_nrom();
        const auto romsha=sha256(rom);
        ReplayTape t;
        bool external_replay=false;
        std::size_t n=900;
        if(argc>=3&&std::string(argv[2])=="--replay"){
            if(argc!=4)throw std::runtime_error("--replay requires path");
            t=ReplayTape::load(argv[3]);
            if(t.rom_sha256()!=romsha)throw std::runtime_error("external replay ROM SHA mismatch");
            n=t.frames().size();
            if(n<60||n>20000)throw std::runtime_error("external replay frame bound 60..20000");
            external_replay=true;
        }else{
            n=(argc>=3)?std::stoull(argv[2]):900;
            if(n<60||n>10000)throw std::runtime_error("frame bound 60..10000");
            t=ReplayTape(romsha);std::uint32_t x=0x9e3779b9u;
            for(std::size_t i=0;i<n;++i){
                x^=x<<13;x^=x>>17;x^=x<<5;
                std::uint8_t b=std::uint8_t(x);
                if((b&(1u<<4))&&(b&(1u<<5)))b&=~(1u<<5);
                if((b&(1u<<6))&&(b&(1u<<7)))b&=~(1u<<7);
                const std::uint8_t cmd=(i&&i%300==0)?kReplayWarmReset:0;
                t.append({b,cmd});
            }
        }
        const auto rp=std::filesystem::current_path()/"VKRT_RUNTIME_QUALIFICATION.vkrtrpl";
        t.save(rp);auto loaded=ReplayTape::load(rp);
        if(loaded.rom_sha256()!=romsha||loaded.frames()!=t.frames())throw std::runtime_error("replay roundtrip");
        std::uint64_t checks=2;
        auto a=execute(rom,loaded,false,checks);
        auto b=execute(rom,loaded,true,checks);
        if(a.size()!=b.size())throw std::runtime_error("witness size");
        for(std::size_t i=0;i<a.size();++i){
            if(a[i].state!=b[i].state||a[i].frame!=b[i].frame||a[i].audio!=b[i].audio)
                throw std::runtime_error("replay canonical mismatch frame "+std::to_string(i));
            checks+=3;
        }
        std::error_code ec;std::filesystem::remove(rp,ec);
        std::cout<<"QUALIFICATION=PASS\n";
        std::cout<<"REPLAY_SOURCE="<<(external_replay?"EXTERNAL_HUMAN":"GENERATED_DETERMINISTIC")<<"\n";
        std::cout<<"FRAMES="<<n<<"\n";
        std::cout<<"CHECKS="<<checks<<"\n";
        std::cout<<"ROM_SHA256="<<romsha<<"\n";
        std::cout<<"REPLAY_FINGERPRINT64="<<loaded.fingerprint64()<<"\n";
        std::cout<<"FINAL_STATE_HASH="<<a.back().state<<"\n";
        std::cout<<"FINAL_FRAME_HASH="<<a.back().frame<<"\n";
        std::cout<<"FINAL_AUDIO_HASH="<<a.back().audio<<"\n";
        std::cout<<"PRESENTATION_MUTATION=NONE\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"QUALIFICATION_FAIL: "<<e.what()<<"\n";return 1;}
}
