#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace vkrt::nes;
namespace {
void json(const std::filesystem::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f.exceptions(std::ios::failbit|std::ios::badbit);f<<s;f.close();}
struct Witness {std::string state,frame,audio;ImmutableFrame observation;std::vector<std::int16_t> pcm;std::array<std::string,3> rendered;Bytes state_bytes;};
Witness run(int presentation){
    Machine machine(synthetic_nrom());ImmutableFrame previous;
    for(unsigned frame=0;frame<8;++frame){machine.controllers[0].buttons=frame%3==0?1:0;machine.run_frames(1);
        auto state=machine.serialize();auto audio=machine.audio_hash();auto canonical=frame_hash(*machine.frame());
        auto observation=observe(*machine.frame(),previous.get());
        if(presentation>=0)(void)render(*observation,static_cast<Mode>(presentation));
        if(state!=machine.serialize()||audio!=machine.audio_hash()||canonical!=frame_hash(*machine.frame()))throw std::runtime_error("presentation mutated canonical machine");
        previous=observation;
    }
    Witness w;w.state=machine.state_hash();w.frame=frame_hash(*machine.frame());w.audio=machine.audio_hash();w.observation=previous;w.pcm=machine.apu.pcm;w.state_bytes=machine.serialize();
    for(int mode=0;mode<3;++mode)w.rendered[mode]=sha256(render(*previous,static_cast<Mode>(mode)).rgb);
    return w;
}
int self_test(const std::filesystem::path&dir){
    if(!dir.is_absolute())throw std::invalid_argument("--evidence-dir must be absolute");
    auto baseline=run(-1);bool core=true,frame=true,audio=true,repeat=true;
    for(int iteration=0;iteration<2;++iteration)for(int mode=0;mode<3;++mode){auto candidate=run(mode);
        core=core&&candidate.state==baseline.state&&candidate.state_bytes==baseline.state_bytes;
        frame=frame&&candidate.frame==baseline.frame;audio=audio&&candidate.audio==baseline.audio;
        repeat=repeat&&candidate.rendered==baseline.rendered;
    }
    auto regions=connected_regions(*baseline.observation);unsigned bg=0,sprites=0,tracked=0;
    for(const auto&s:baseline.observation->samples){bg+=s.layer==Layer::background;sprites+=s.layer==Layer::sprite;tracked+=s.confidence!=0;}
    auto range=std::minmax_element(baseline.pcm.begin(),baseline.pcm.end());
    bool meaningful=bg>1000&&sprites>20&&tracked>100&&regions.regions.size()>10&&!baseline.pcm.empty()&&*range.first!=*range.second
        &&baseline.rendered[0]!=baseline.rendered[1]&&baseline.rendered[0]!=baseline.rendered[2];
    bool pass=core&&frame&&audio&&repeat&&meaningful;
    std::filesystem::create_directories(dir);
    write_ppm(dir/"canonical.ppm",render(*baseline.observation,Mode::EXACT_DIGITAL,256,240));
    write_ppm(dir/"exact_digital.ppm",render(*baseline.observation,Mode::EXACT_DIGITAL));
    write_ppm(dir/"authentic_signal.ppm",render(*baseline.observation,Mode::AUTHENTIC_SIGNAL));
    write_ppm(dir/"semantic_hd.ppm",render(*baseline.observation,Mode::SEMANTIC_HD));
    write_modern_evidence(dir,*baseline.observation);
    write_semantics(dir/"semantic.jsonl",*baseline.observation);write_wav(dir/"audio.wav",baseline.pcm);
    auto boolean=[](bool b){return b?"true":"false";};
    json(dir/"invariance.json",std::string("{\n  \"core_state_hash_equal\": ")+boolean(core)+",\n  \"canonical_frame_hash_equal\": "+boolean(frame)+",\n  \"canonical_audio_hash_equal\": "+boolean(audio)+",\n  \"presentation_repeat_equal\": "+boolean(repeat)+",\n  \"iterations\": 2,\n  \"mode_runs\": 6,\n  \"frames_per_run\": 8,\n  \"core_state_sha256\": \""+baseline.state+"\",\n  \"canonical_frame_sha256\": \""+baseline.frame+"\",\n  \"canonical_audio_sha256\": \""+baseline.audio+"\"\n}\n");
    json(dir/"summary.json",std::string("{\n  \"result\": \"")+(pass?"PASS":"FAIL")+"\",\n  \"synthetic_rom_only\": true,\n  \"commercial_roms_used\": false,\n  \"witness_meaningful\": "+boolean(meaningful)+",\n  \"background_samples\": "+std::to_string(bg)+",\n  \"sprite_samples\": "+std::to_string(sprites)+",\n  \"tracked_samples\": "+std::to_string(tracked)+",\n  \"regions\": "+std::to_string(regions.regions.size())+",\n  \"pcm_samples\": "+std::to_string(baseline.pcm.size())+",\n  \"claim_scope\": \"synthetic NROM CPU reference; runner adjudication required\"\n}\n");
    std::cout<<(pass?"PASS":"FAIL")<<" synthetic NROM self-test\n";return pass?0:1;
}
int rom_run(const std::filesystem::path& path,unsigned frames,const std::filesystem::path& dir){
    if(!dir.is_absolute())throw std::invalid_argument("--evidence-dir must be absolute");
    if(frames<1||frames>600)throw std::invalid_argument("--frames must be 1..600");
    // Read once into bounded owned memory; neither path nor ROM payload is evidence.
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::invalid_argument("cannot open ROM");
    auto size=input.tellg();if(size<16||size>16+512+32*1024*1024)throw std::invalid_argument("ROM file size bound");
    Bytes bytes(static_cast<std::size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!input)throw std::runtime_error("ROM read incomplete");input.close();
    const auto info=inspect_ines(bytes);const auto digest=sha256(bytes);
    Machine machine(bytes);ImmutableFrame observation;std::set<std::string> unique;
    bool core=true,canonical=true,audio=true,repeat=true,completed=true;
    std::array<std::string,3> rendered{};
    try {
        for(unsigned f=0;f<frames;++f){machine.run_frames(1);unique.insert(frame_hash(*machine.frame()));
            if(f+2>=frames){
                auto state=machine.serialize();auto fh=frame_hash(*machine.frame()),ah=machine.audio_hash();
                observation=observe(*machine.frame(),observation.get());
                for(unsigned mode=0;mode<3;++mode){auto image=render(*observation,static_cast<Mode>(mode));
                    rendered[mode]=sha256(image.rgb);repeat=repeat&&image.rgb==render(*observation,static_cast<Mode>(mode)).rgb;}
                core=core&&state==machine.serialize();canonical=canonical&&fh==frame_hash(*machine.frame());audio=audio&&ah==machine.audio_hash();
            }
        }
        // Continued execution in a second canonical machine with no presentation.
        Machine baseline(bytes);for(unsigned f=0;f<frames;++f)baseline.run_frames(1);
        core=core&&baseline.serialize()==machine.serialize();canonical=canonical&&frame_hash(*baseline.frame())==frame_hash(*machine.frame());
        audio=audio&&baseline.audio_hash()==machine.audio_hash();
    }catch(const std::exception& e){completed=false;std::cerr<<"ROM execution stopped: "<<e.what()<<'\n';}
    if(!observation&&machine.frame())observation=observe(*machine.frame());
    unsigned non_backdrop=0,tracked=0;std::set<std::uint8_t> colors;
    if(observation)for(const auto& s:observation->samples){non_backdrop+=s.layer!=Layer::backdrop;tracked+=s.confidence!=0;colors.insert(s.color);}
    auto stats=machine.mapper->stats();
    const bool invariance=completed&&observation&&core&&canonical&&audio&&repeat;
    const bool mapper_active=info.mapper!=4||(stats.prg_bank_writes>0&&stats.chr_bank_writes>0&&stats.irq_assertions>0&&stats.irq_acknowledgements>0);
    const bool pass=invariance&&machine.ppu.frame_number==frames&&!machine.cpu.halted&&mapper_active&&unique.size()>=2&&non_backdrop>1000&&colors.size()>1&&!machine.apu.pcm.empty();
    std::filesystem::create_directories(dir);
    if(observation){write_ppm(dir/"canonical.ppm",render(*observation,Mode::EXACT_DIGITAL,256,240));
        write_ppm(dir/"exact_digital.ppm",render(*observation,Mode::EXACT_DIGITAL));
        write_ppm(dir/"authentic_signal.ppm",render(*observation,Mode::AUTHENTIC_SIGNAL));
        write_ppm(dir/"semantic_hd.ppm",render(*observation,Mode::SEMANTIC_HD));write_semantics(dir/"semantic.jsonl",*observation);
        write_modern_evidence(dir,*observation);}
    write_wav(dir/"audio.wav",machine.apu.pcm);
    auto boolean=[](bool b){return b?"true":"false";};
    json(dir/"invariance.json",std::string("{\n  \"core_state_hash_equal\": ")+boolean(completed&&core)+
        ",\n  \"canonical_frame_hash_equal\": "+boolean(completed&&canonical)+",\n  \"canonical_audio_hash_equal\": "+boolean(completed&&audio)+
        ",\n  \"presentation_repeat_equal\": "+boolean(completed&&repeat)+",\n  \"core_state_sha256\": \""+machine.state_hash()+
        "\",\n  \"canonical_frame_sha256\": \""+(machine.frame()?frame_hash(*machine.frame()):"")+"\",\n  \"canonical_audio_sha256\": \""+machine.audio_hash()+"\"\n}\n");
    json(dir/"run_summary.json",std::string("{\n  \"result\": \"")+(pass?"PASS":"FAIL")+"\",\n  \"commercial_rom_used\": true,\n  \"rom_sha256\": \""+digest+
        "\",\n  \"rom_bytes\": "+std::to_string(bytes.size())+",\n  \"mapper\": "+std::to_string(info.mapper)+
        ",\n  \"frames_requested\": "+std::to_string(frames)+",\n  \"frames_completed\": "+std::to_string(machine.ppu.frame_number)+
        ",\n  \"cpu_halted\": "+boolean(machine.cpu.halted)+",\n  \"mapper_irq_assertions\": "+std::to_string(stats.irq_assertions)+
        ",\n  \"mapper_irq_acknowledgements\": "+std::to_string(stats.irq_acknowledgements)+",\n  \"mapper_prg_bank_writes\": "+std::to_string(stats.prg_bank_writes)+
        ",\n  \"mapper_chr_bank_writes\": "+std::to_string(stats.chr_bank_writes)+",\n  \"unique_canonical_frames\": "+std::to_string(unique.size())+
        ",\n  \"non_backdrop_pixels\": "+std::to_string(non_backdrop)+",\n  \"semantic_samples\": "+std::to_string(observation?observation->samples.size():0)+
        ",\n  \"tracked_semantic_samples\": "+std::to_string(tracked)+",\n  \"pcm_samples\": "+std::to_string(machine.apu.pcm.size())+
        ",\n  \"claim_scope\": \"bounded external-ROM execution; identity and M1 runner adjudication separate; no full NES compatibility\"\n}\n");
    std::cout<<(pass?"PASS":"FAIL")<<" bounded ROM witness, frames="<<machine.ppu.frame_number<<", mapper="<<info.mapper<<'\n';return pass?0:1;
}
}
int main(int argc,char**argv){
    try{if(argc==2&&std::string(argv[1])=="--help"){std::cout<<"vkrt_nes_m0_demo --self-test --evidence-dir <ABSOLUTE_DIR>\nvkrt_nes_m0_demo --rom <ROM_PATH> --frames <1..600> --evidence-dir <ABSOLUTE_DIR>\n";return 0;}
        if(argc==4&&std::string(argv[1])=="--self-test"&&std::string(argv[2])=="--evidence-dir")return self_test(std::filesystem::path(argv[3]));
        if(argc==7&&std::string(argv[1])=="--rom"&&std::string(argv[3])=="--frames"&&std::string(argv[5])=="--evidence-dir"){
            unsigned frames{};std::string arg=argv[4];auto result=std::from_chars(arg.data(),arg.data()+arg.size(),frames);
            if(result.ec!=std::errc{}||result.ptr!=arg.data()+arg.size())throw std::invalid_argument("invalid frame count");
            return rom_run(argv[2],frames,argv[6]);
        }
        throw std::invalid_argument("usage: --self-test --evidence-dir <ABSOLUTE_DIR> OR --rom <ROM_PATH> --frames <1..600> --evidence-dir <ABSOLUTE_DIR>");
    }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
