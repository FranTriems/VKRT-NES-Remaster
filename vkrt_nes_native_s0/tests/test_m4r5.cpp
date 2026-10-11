#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/runtime.hpp"
#include "vkrt/nes_native_s0/display_reference.hpp"
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <type_traits>

using namespace vkrt::nes;
using namespace vkrt::nes::runtime;
namespace {
std::uint64_t checks=0,failures=0;
std::map<std::string,std::uint64_t> failed;
void check(bool ok,const char* label){++checks;if(!ok){++failures;++failed[label];}}
int capture(const char* rompath,const char* folder){
    std::ifstream input(rompath,std::ios::binary);if(!input)throw std::runtime_error("ROM unavailable");
    Bytes rom((std::istreambuf_iterator<char>(input)),{});Machine m(rom);ImmutableFrame previous;
    const std::filesystem::path dir(folder);std::filesystem::create_directories(dir);
    std::ofstream manifest(dir/"hashes.txt");manifest<<"ROM "<<sha256(rom)<<'\n';
    for(int frame=1;frame<=600;++frame){
        m.run_frames(1);previous=observe(*m.frame(),previous.get());
        if(frame==180||frame==360||frame==540||frame==600){
            const auto state=m.state_hash(),canonical=frame_hash(*m.frame()),audio=m.audio_hash();
            manifest<<frame<<" STATE "<<state<<" FRAME "<<canonical<<" AUDIO "<<audio<<'\n';
            for(int mode=0;mode<4;++mode){
                #ifdef VKRT_M4R5_CAPTURE_ONLY
                auto out=render(*previous,mode==3?Mode::MODERN_REMASTER:Mode(mode),512,480);
#else
                auto out=render_reference(*previous,mode==3?Mode::MODERN_REMASTER:Mode(mode),512,480);
#endif
                manifest<<frame<<" MODE "<<mode<<' '<<sha256(out.rgb)<<'\n';
                write_ppm(dir/(std::to_string(frame)+"_"+std::to_string(mode)+".ppm"),out);
            }
            if(state!=m.state_hash()||canonical!=frame_hash(*m.frame())||audio!=m.audio_hash())throw std::runtime_error("capture mutated Machine");
        }
        m.apu.pcm.clear();
    }
    std::cout<<"CAPTURE=PASS\n";return 0;
}
#ifndef VKRT_M4R5_CAPTURE_ONLY
void queues(){
    struct Value {std::uint64_t index,complement,hash;};
    SpscRing<Value,31> ring;
    constexpr std::uint64_t n=2200000;
    std::atomic<bool> begin{false};std::uint64_t wrong=0,attempts=0;
    std::thread producer([&]{
        while(!begin.load())std::this_thread::yield();
        for(std::uint64_t i=0;i<n;++i){
            Value value{i,~i,i*0x9e3779b97f4a7c15ull};
            while(!ring.push(value)){++attempts;std::this_thread::yield();}
        }
    });
    begin.store(true);
    for(std::uint64_t i=0;i<n;++i){
        Value value{};while(!ring.pop(value))std::this_thread::yield();
        check(value.index==i,"concurrent ordered delivery");check(value.complement==~i,"concurrent complete value");
        check(value.hash==i*0x9e3779b97f4a7c15ull,"concurrent release/acquire payload");
        if(i%4096==0)std::this_thread::yield();
    }
    producer.join();check(ring.size_approx()==0&&wrong==0,"queue empty after pressure");
    for(unsigned i=0;i<31;++i)check(ring.push({i,0,0}),"bounded queue fill");
    check(!ring.push({31,0,0}),"bounded queue rejects overflow");
    const auto boundary=ring.write_position();Value v;ring.pop(v);ring.push({99,0,0});
    ring.discard_before(boundary);check(ring.pop(v)&&v.index==99,"flush preserves post-watermark data");
    check(!ring.pop(v),"flush cursor ordering");
}
void pacing(){
    RationalFramePacer pacer;
    for(std::uint64_t i=1;i<=2100000;++i){
        const auto prior=pacer.elapsed_ns(),step=pacer.next_period_ns();
        const auto expected=i*kFramePeriodNumeratorNs/kNtscFrameRateNumerator;
        check(pacer.elapsed_ns()==expected,"rational cumulative exactness");
        check(step==expected-prior,"rational period carry");
        const auto wait=-relative_timer_due(step+i%100);
        check(wait*100>=std::int64_t(step+i%100)&&wait*100<std::int64_t(step+i%100+100),"timer rounds up under 100ns");
    }
    check(relative_timer_due(0)==-1,"zero timer stays relative");
    check(bounded_deadline(100,1000000000)==1000000000,"stall rebases deadline");
    check(bounded_deadline(100,1000100)==100,"small lateness retains phase");
    pacer.reset();check(pacer.elapsed_ns()==0&&pacer.frames()==0,"pacer reset");
}
void resampling(){
    for(double endpoint:{32000.,44100.,48000.,96000.}){
        NativeAudioFifo a,b;CubicResampler ra,rb;std::uint64_t input=0;
        constexpr double native=44081.;const double step=native/endpoint;
        double squared_error=0;float prior=0;
        for(int i=0;i<120000;++i){
            while(a.size_approx()<64){
                const auto sample=std::int16_t(std::llround(24000*std::sin(input*2*3.141592653589793*1000/native)));
                check(a.push(sample)&&b.push(sample),"resampler input accepted");++input;
            }
            float x{},y{};check(ra.next(a,step,x)&&rb.next(b,step,y),"resampler feasible supply");
            check(x==y,"resampler deterministic across chunks");
            check(x>=-1&&x<=1&&std::isfinite(x),"resampler bounded finite");
            const auto expected=24000./32768*std::sin((1+i*step)*2*3.141592653589793*1000/native);
            squared_error+=(x-expected)*(x-expected);
            if(i)check(std::abs(x-prior)<.16,"resampler continuity at packet boundaries");prior=x;
        }
        check(std::sqrt(squared_error/120000)<.0002,"cubic 1kHz reconstruction RMS");
        check(std::abs(double(input-a.size_approx())-(120000*step+3))<4,"resampler rate conversion count");
    }
    NativeAudioFifo ring;CubicResampler r;float x{};
    check(!r.next(ring,1,x),"resampler starvation reported");
    for(int i=0;i<20000;++i){
        while(ring.size_approx()<8)ring.push(i%2?-32768:32767);
        check(r.next(ring,.9137,x)&&x>=-1&&x<=32767.f/32768.f,"cubic step/impulse overshoot bounded");
    }
    for(unsigned bits:{16u,24u,32u})for(unsigned valid:{16u,24u,32u}){
        EndpointFormat f;f.encoding=EndpointEncoding::pcm;f.bits=bits;f.valid_bits=valid;f.channels=2;
        check(f.supported()==(valid<=bits),"PCM valid bits validation");if(!f.supported())continue;
        std::array<std::uint8_t,64> bytes{};encode_endpoint(-1.f,f,bytes);
        check(bytes[bits/8-1]==128&&bytes[bits/4-1]==128,"PCM negative full scale aligned");
        encode_endpoint(.5f,f,bytes);check(bytes[bits/8-1]==64&&bytes[bits/4-1]==64,"PCM positive half scale aligned");
    }
    EndpointFormat f;f.channels=6;f.channel_mask=0x3f;std::array<std::uint8_t,24> bytes{};
    encode_endpoint(.25f,f,bytes);
    for(unsigned ch=0;ch<6;++ch){float value;std::memcpy(&value,bytes.data()+ch*4,4);check(value==(ch==3?0.f:.25f),"float multichannel layout excludes LFE");}
}
void drift(){
    for(int ppm:{-1500,-500,0,500,1500}){
        DriftController c;double occupancy=2200,rate=44081;double min=1e9,max=-1e9;
        for(int i=0;i<90000;++i){
            const double jitter=((i*17)%13-6)*8.;
            const double correction=c.update(occupancy+jitter,2200,rate,.01);
            occupancy+=rate*.01*(ppm*1.e-6-correction);
            check(std::abs(correction)<=DriftController::limit,"DRC hard correction bound");
            check(std::abs(c.integral())<=DriftController::limit,"DRC anti-windup bound");
            if(i>80000){min=std::min(min,occupancy);max=std::max(max,occupancy);}
        }
        check(std::abs(occupancy-2200)<3,"DRC target convergence");
        check(max-min<2,"DRC no sustained oscillation");
        check(std::abs(c.correction()*1.e6-ppm)<10,"DRC tracks small clock mismatch");
    }
    for(double extreme:{0.,100000.}){
        DriftController c;
        for(int i=0;i<100000;++i)c.update(extreme,2200,44081,.01);
        check(std::abs(c.correction())<=.003&&std::abs(c.integral())<=.003,"saturation cannot mask sustained slowness");
        double occupancy=2200;
        for(int i=0;i<12000;++i)occupancy-=44081*.01*c.update(occupancy,2200,44081,.01);
        check(std::abs(c.correction())<.00005&&std::abs(occupancy-2200)<3,"saturation unwinds with recovered feasible producer");
    }
}
void stress_tests(){
    for(unsigned rate:{44100u,48000u,96000u})for(unsigned divisor:{100u,200u})for(int ppm:{-1000,1000}){
        auto result=simulate_audio(44081,rate,rate/divisor,ppm,180);
        check(result.underruns==0,"jitter stress zero underruns");check(result.dropped==0,"jitter stress zero drops");
        check(result.max_correction_ppm<=3000,"jitter stress DRC bound");
        check(result.final_occupancy_ms>20&&result.final_occupancy_ms<90,"jitter stress occupancy bounded");
    }
    auto a=simulate_audio(44081,48000,480,700,10),b=simulate_audio(44081,48000,480,700,10);
    check(a.checksum==b.checksum&&a.output_frames==b.output_frames,"stress simulation deterministic");
    AudioStream s(44081);EndpointFormat fmt;s.configure(fmt,480);std::vector<std::int16_t> pcm(4096,12000);
    std::vector<std::uint8_t> output(480*8);
    s.submit(pcm);s.request_flush();s.submit(pcm);s.render(output,480);
    check(s.occupancy()>3000,"audio reset preserves new epoch PCM");
    auto before=s.occupancy();s.render(output,480,true);check(s.occupancy()==before&&s.underruns==0,"pause intentional silence");
    for(int i=0;i<30;++i)s.render(output,480);
    check(s.underruns>0,"infeasible supply reports starvation");
    s.submit(std::vector<std::int16_t>(20000));check(s.dropped>0,"overflow reported honestly");
}
void colors(){
    for(unsigned code=0;code<256;++code)for(unsigned effects=0;effects<256;++effects){
        const auto c=reference_display_color(std::uint8_t(code),std::uint8_t(effects));
        check(c==modern_source_color(std::uint8_t(code),std::uint8_t(effects)),"accepted source anchor all code/effect combinations");
        auto expected=modern_source_color(std::uint8_t(effects&1?code&0x30:code&63),0);
        if(effects&32){expected[1]=expected[1]*3/4;expected[2]=expected[2]*3/4;}
        if(effects&64){expected[0]=expected[0]*3/4;expected[2]=expected[2]*3/4;}
        if(effects&128){expected[0]=expected[0]*3/4;expected[1]=expected[1]*3/4;}
        check(c==expected,"grayscale before emphasis independent oracle");
    }
    for(unsigned level=0;level<4;++level){
        const auto blue=reference_display_color(level*16+1,0),green=reference_display_color(level*16+10,0),warm=reference_display_color(level*16+7,0);
        check(blue[2]>blue[1]&&blue[1]>blue[0],"blue family no global hue rotation");
        check(green[1]>green[0]&&green[1]>green[2],"green family");check(warm[0]>warm[1]&&warm[1]>warm[2],"warm family");
    }
    Frame raw;
    for(unsigned e:{0u,1u,32u,64u,128u,225u}){
        for(int i=0;i<pixels;++i){auto& s=raw.samples[i];s.x=i%width;s.y=i/width;s.color=(i/64)%64;s.emphasis=e>>5;s.grayscale=e&1;
            raw.indices[i]=s.color;raw.effects[i]=std::uint8_t(e);}
        auto f=observe(raw);
        auto original=render_reference(*f,Mode::EXACT_DIGITAL,256,240),semantic=render_reference(*f,Mode::SEMANTIC_HD,256,240);
        for(int i=0;i<pixels;++i){const auto c=reference_display_color(raw.indices[i],raw.effects[i]);
            for(int k=0;k<3;++k){
                check(original.rgb[i*3+k]==c[k],"F1 exact reference-family dispatch");
                // At native pixel centers with locked backdrop, Semantic HD has no material modulation.
                check(semantic.rgb[i*3+k]==c[k],"F3 reference-family base dispatch");
            }
        }
        check(render_reference(*f,Mode::MODERN_REMASTER,512,480)==render_modern(*f,{},512,480),"F4 dispatch bypasses reference transform");
    }
}
void immutable_replay(){
    static_assert(std::is_same_v<ImmutableFrame::element_type,const Frame>);
    const auto rom=synthetic_nrom();Machine direct(rom),async(rom);
    SpscRing<ImmutableFrame,4> queue;std::atomic<bool> done{false};
    std::atomic<std::uint64_t> observations{0},mutation{0};
    std::thread observer([&]{
        ImmutableFrame frame,previous;
        while(!done.load()||queue.size_approx()){
            if(!queue.pop(frame)){std::this_thread::yield();continue;}
            const auto before=frame_hash(*frame);previous=observe(*frame,previous.get());
            auto image=render_reference(*previous,(observations.load()%4==3?Mode::MODERN_REMASTER:Mode(observations.load()%4)),512,480);
            if(frame_hash(*frame)!=before||image.rgb.empty())++mutation;
            ++observations;
        }
    });
    ReplayTape tape(sha256(rom));
    for(unsigned i=0;i<72;++i){
        ReplayFrame input{std::uint8_t(i*17),std::uint8_t(i==35?kReplayWarmReset:0)};tape.append(input);
        if(input.commands){direct.reset();async.reset();direct.apu.pcm.clear();async.apu.pcm.clear();}
        direct.controllers[0].buttons=async.controllers[0].buttons=input.buttons;
        direct.run_frames(1);async.run_frames(1);
        queue.push(async.frame()); // Presentation may shed; truth/replay may not.
        check(direct.serialize()==async.serialize(),"async presentation cannot mutate Machine state");
        check(direct.apu.pcm==async.apu.pcm,"async presentation cannot mutate APU PCM");
        check(frame_hash(*direct.frame())==frame_hash(*async.frame()),"async presentation cannot mutate canonical framebuffer");
        direct.apu.pcm.clear();async.apu.pcm.clear();
    }
    done.store(true);observer.join();check(mutation==0&&observations>0,"retained immutable frames survive future Machine frames/reset");
    const auto temp=std::filesystem::temp_directory_path()/"vkrt_m4r5_test_replay.vkrtrpl";
    tape.save(temp);const auto loaded=ReplayTape::load(temp);std::filesystem::remove(temp);
    check(loaded.frames()==tape.frames()&&loaded.fingerprint64()==tape.fingerprint64(),"runtime replay deterministic roundtrip");
}
void source_contract(){
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
    for(const auto* name:{"src/runtime.cpp","src/remaster.cpp","include/vkrt/nes_native_s0/display_reference.hpp",
        "app/windows/runtime_main.cpp","app/windows/runtime_audio.cpp","app/windows/runtime_presenter.cpp"}){
        std::ifstream file(root/name);std::string s((std::istreambuf_iterator<char>(file)),{});
        check(!s.empty(),"runtime implementation available for audit");
        for(const auto* forbidden:{"tile_id ==","tile==","screen_x ==","screen_y ==","4377a7f5","Super " "Mario","SM" "B3"})
            check(s.find(forbidden)==std::string::npos,"no title/tile/screen-coordinate hacks");
    }
    std::ifstream file(root/"app/windows/runtime_main.cpp");std::string s((std::istreambuf_iterator<char>(file)),{});
    auto start=s.find("emulation=std::jthread");auto end=s.find("auto stat_start",start);
    auto producer=s.substr(start,end-start);
    check(producer.find("observe(")==std::string::npos&&producer.find("render(")==std::string::npos,"producer excludes observation/render");
    check(producer.find(".pump(")==std::string::npos&&producer.find("state_hash(")==std::string::npos,"producer excludes endpoint pump and validation");
}
#endif
}
int main(int argc,char** argv){
    try{
        if(argc==4&&std::string(argv[1])=="--capture")return capture(argv[2],argv[3]);
#ifndef VKRT_M4R5_CAPTURE_ONLY
        queues();pacing();resampling();drift();stress_tests();colors();immutable_replay();source_contract();
        for(const auto& [name,n]:failed)std::cerr<<"FAIL "<<name<<" COUNT="<<n<<'\n';
        check(checks>=12000000,"minimum meaningful checks");
        std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
#else
        return 2;
#endif
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
