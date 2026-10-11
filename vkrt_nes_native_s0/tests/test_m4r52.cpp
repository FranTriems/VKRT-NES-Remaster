#include "vkrt/nes_native_s0/runtime.hpp"
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_scene.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>

// Internal numeric helper stays in the allowed scene translation unit.
namespace vkrt::nes::detail { float contact_falloff(float) noexcept; }
using namespace vkrt::nes;
using namespace vkrt::nes::runtime;
namespace {
constexpr double pi=3.14159265358979323846, native=44081.;
std::uint64_t checks{}, failures{};
std::map<std::string,std::uint64_t> failed;
void check(bool ok,const char* label){++checks;if(!ok){++failures;++failed[label];}}
std::int16_t signal(std::uint64_t i){
    // Stateless broadband sequence: packet producers see identical native PCM.
    auto x=std::uint32_t(i)*747796405u+2891336453u;
    x=((x>>((x>>28)+4))^x)*277803737u;x=(x>>22)^x;
    return std::int16_t(int(x%65536)-32768);
}
void packet_boundaries(){
    for(double rate:{32000.,44100.,48000.,96000.}){
        const double nominal=native/rate, maximum=nominal*1.002;
        NativeAudioFifo a,b;BandLimitedResampler ra(maximum),rb(maximum);
        std::uint64_t sent_a=0,sent_b=0;
        for(unsigned i=0;i<240000;++i){
            while(a.size_approx()<512){check(a.push(signal(sent_a++)),"large packet accepted");}
            // Smooth deterministic modulation as well as phase/table boundaries.
            const double step=nominal*(1.+.001*std::sin(i*.00017));
            float x{},y=2;
            check(ra.next(a,step,x),"large packet supply feasible");
            while(!rb.next(b,step,y)){
                check(y==2,"incomplete FIR leaves output untouched");
                const auto packet=1+(sent_b*17)%73;
                for(unsigned k=0;k<packet;++k)check(b.push(signal(sent_b++)),"fragment packet accepted");
            }
            check(x==y,"FIR bit exact across refill retries and packet boundaries");
            check(std::isfinite(x)&&x>=-1&&x<=32767.f/32768.f,"broadband FIR bounded finite");
            check(sent_a-a.size_approx()==sent_b-b.size_approx(),"fragment retries preserve consumption phase");
        }
        ra.reset();rb.reset();a.reset_quiescent();b.reset_quiescent();
        for(unsigned i=0;i<64;++i){a.push(12345);b.push(12345);}
        float x{},y{};check(ra.next(a,nominal,x)&&rb.next(b,nominal,y)&&x==y,"reset discards all history and phase");
    }
}
void dc_and_bounds(){
    for(double rate:{32000.,44100.,48000.,192000.})for(int value:{-32768,-24001,-1,0,1,12003,32767}){
        NativeAudioFifo fifo;BandLimitedResampler r(native/rate*1.002);
        for(int i=0;i<20000;++i){
            while(fifo.size_approx()<32)fifo.push(std::int16_t(value));
            float v{};check(r.next(fifo,native/rate*(1.+.001*std::sin(i*.001)),v),"DC supplied");
            check(std::abs(v-value/32768.)<2.e-7,"normalized DC including startup and changing phase");
        }
    }
    for(int kind=0;kind<4;++kind){
        NativeAudioFifo fifo;BandLimitedResampler r(1.5);std::uint64_t sent=0;
        float last=0;
        for(unsigned i=0;i<120000;++i){
            while(fifo.size_approx()<32){
                int sample=kind==0?(sent==100?32767:0):kind==1?(sent<100?32767:0):
                    kind==2?(sent%2?32767:-32768):signal(sent);
                fifo.push(std::int16_t(sample));++sent;
            }
            float v{};check(r.next(fifo,1.37,v),"extreme signal supplied");
            check(std::isfinite(v)&&v>=-1&&v<=32767.f/32768.f,"impulse step alternating noise bounded");
            if(kind<2&&i>100)check(v==0,"finite support has no ringing tail");
            last=v;
        }
        check(std::isfinite(last),"long stream no runaway");
        float sentinel=2;
        for(double step:{0.,-1.,1.6,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            const auto occupancy=fifo.size_approx();
            check(!r.next(fifo,step,sentinel)&&sentinel==2&&fifo.size_approx()==occupancy,"invalid step rejected without state consumption");
        }
    }
}
void sine_quality(){
    for(double rate:{44100.,48000.})for(double hz:{220.,1000.,8000.,15000.}){
        NativeAudioFifo fa,fb;BandLimitedResampler fir(native/rate*1.002);CubicResampler cubic;
        std::uint64_t sent_a=0,sent_b=0;double ef=0,ec=0;unsigned count=0;
        for(unsigned i=0;i<160000;++i){
            auto fill=[&](NativeAudioFifo& fifo,std::uint64_t& sent){
                while(fifo.size_approx()<64){fifo.push(std::int16_t(std::llround(24000*std::sin(sent*2*pi*hz/native))));++sent;}
            };
            fill(fa,sent_a);fill(fb,sent_b);
            float f{},c{};check(fir.next(fa,native/rate,f),"sine FIR supplied");check(cubic.next(fb,native/rate,c),"sine cubic supplied");
            check(std::isfinite(f)&&std::abs(f)<=1,"sine finite and bounded");
            if(i<32)continue; // Exclude the documented left-edge extension.
            const double expected_f=24000./32768*std::sin(i*2*pi*hz/rate);
            const double expected_c=24000./32768*std::sin((1+i*native/rate)*2*pi*hz/native);
            ef+=(f-expected_f)*(f-expected_f);ec+=(c-expected_c)*(c-expected_c);++count;
        }
        ef=std::sqrt(ef/count);ec=std::sqrt(ec/count);
        std::cout<<"TONE rate="<<rate<<" hz="<<hz<<" FIR_RMS="<<ef<<" CUBIC_RMS="<<ec<<'\n';
        check(ef<(hz<2000?.00003:hz<10000?.0004:.003),"FIR absolute sine reconstruction quality");
        // Near 1 kHz both methods approach the int16 quantization floor. At
        // higher frequencies demand two orders of magnitude of improvement.
        check(hz<1000||ef<ec*(hz<2000?.85:.01),"FIR improves cubic reconstruction RMS");
    }
    // Downsampling needs actual low-pass filtering, not just interpolation.
    NativeAudioFifo fifo;BandLimitedResampler r(native/32000*1.002);
    std::uint64_t sent=0;double energy=0;
    for(unsigned i=0;i<64000;++i){
        while(fifo.size_approx()<64){fifo.push(std::int16_t(std::llround(24000*std::sin(sent*2*pi*21000/native))));++sent;}
        float v{};check(r.next(fifo,native/32000,v),"downsample supplied");if(i>32)energy+=v*v;
    }
    const double rms=std::sqrt(energy/63967);
    std::cout<<"STOPBAND_21KHZ_TO_32K_RMS="<<rms<<'\n';
    check(rms<.002,"downsample stopband rejection");
}
void drift(){
    for(int ppm:{-1500,-1000,0,1000,1500}){
        DriftController c;double occupancy=2200,prior=0,low=1.e9,high=-1.e9;
        for(unsigned i=0;i<60000;++i){
            const double measured=occupancy+100*std::sin(i*.17)+40*std::sin(i*.93);
            const auto correction=c.update(measured,2200,native,.01);
            occupancy+=native*.01*(ppm*1.e-6-correction);
            check(std::isfinite(correction)&&std::abs(correction)<=.002,"DRC 2000ppm hard bound");
            check(std::abs(c.integral())<=.002,"DRC integral bounded");
            check(std::abs(correction-prior)<=DriftController::slew_per_second*.01+1.e-15,"DRC 200ppm per second slew");
            check(occupancy>1500&&occupancy<2900,"DRC occupancy stays feasible");prior=correction;
            if(i>=50000){low=std::min(low,occupancy);high=std::max(high,occupancy);}
        }
        check(std::abs(occupancy-2200)<2&&high-low<2,"DRC target convergence without oscillation");
        check(std::abs(c.correction()*1.e6-ppm)<25,"DRC steady clock tracking");
    }
    for(double extreme:{0.,100000.}){
        DriftController c;double prior=0;
        for(int i=0;i<100000;++i){
            auto v=c.update(extreme,2200,native,.01);
            check(std::abs(v)<=.002&&std::abs(c.integral())<=.002,"stalled producer cannot wind up");
            check(std::abs(v-prior)<=.000002+1.e-15,"saturation preserves slew");prior=v;
        }
        double occupancy=2200;
        for(int i=0;i<12000;++i)occupancy-=native*.01*c.update(occupancy,2200,native,.01);
        check(std::abs(occupancy-2200)<3&&std::abs(c.correction())<.00005,"anti-windup recovers feasible target");
        c.reset();check(c.correction()==0&&c.integral()==0,"DRC reset");
    }
}
void jitter(){
    for(unsigned rate:{44100u,48000u})for(unsigned divisor:{100u,200u})for(int ppm:{-1000,1000}){
        const auto a=simulate_audio(native,rate,rate/divisor,ppm,600);
        const auto b=simulate_audio(native,rate,rate/divisor,ppm,600);
        check(a.underruns==0&&b.underruns==0,"ten minute jitter zero underrun");
        check(a.dropped==0&&b.dropped==0,"ten minute jitter zero drop");
        check(a.max_correction_ppm<=2000,"ten minute jitter correction bound");
        check(a.checksum==b.checksum&&a.output_frames==b.output_frames&&a.final_occupancy_ms==b.final_occupancy_ms,"ten minute jitter deterministic repeat");
        check(a.final_occupancy_ms>20&&a.final_occupancy_ms<90,"ten minute jitter bounded FIFO target");
        std::cout<<"JITTER rate="<<rate<<" period="<<rate/divisor<<" ppm="<<ppm<<" seconds=600 underruns="<<a.underruns
                 <<" drops="<<a.dropped<<" max_ppm="<<a.max_correction_ppm<<" final_ms="<<a.final_occupancy_ms<<'\n';
    }
}
void contact_and_source(){
    float prior=1;
    for(int i=-1000;i<=11000;++i){
        const float x=i/10000.f,v=detail::contact_falloff(x);
        check(std::isfinite(v)&&v>=0&&v<=1,"contact kernel bounded finite");
        check(v<=prior+1.e-7,"contact kernel monotone");prior=v;
        check(x>0||v==1,"contact peak attached");check(x<1||v==0,"contact support compact");
    }
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
    for(const char* path:{"src/modern_scene.cpp","src/runtime.cpp","include/vkrt/nes_native_s0/runtime.hpp"}){
        std::ifstream in(root/path);std::string s((std::istreambuf_iterator<char>(in)),{});
        check(!s.empty(),"production source audit available");
        for(const char* forbidden:{"Super " "Mario","SM" "B3","4377a7f5","title_screen",".tile ==",".tile==","screen_x ==","screen_y =="})
            check(s.find(forbidden)==std::string::npos,"no title tile ROM or screen-specific literals");
        if(std::string(path)=="src/modern_scene.cpp"){
            check(s.find("connected&=paint_join(f,previous,next)")!=std::string::npos,"receiver checks entire path not only matching endpoints");
            check(s.find("shadow[n]=std::max(shadow[n],")!=std::string::npos,"overlapping contacts cannot add opacity");
            check(s.find("if(ink)for(int k=0;k<3;++k)p.color[k]=base[k]")!=std::string::npos,"categorical linework stays source exact");
            check(s.find("occlusion+=")==std::string::npos,"backdrop cannot accumulate detached occluders");
        }
    }
}
void benchmark(){
    NativeAudioFifo fifo;BandLimitedResampler r(1.002);std::uint64_t sent=0;double checksum=0;
    const auto start=std::chrono::steady_clock::now();
    constexpr unsigned outputs=2000000;
    for(unsigned i=0;i<outputs;++i){
        while(fifo.size_approx()<32)fifo.push(signal(sent++));
        float v{};r.next(fifo,native/48000,v);checksum+=v;
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"FIR_MS_PER_480="<<elapsed*480/outputs<<" CHECKSUM="<<checksum<<'\n';
    check(std::isfinite(checksum),"benchmark output consumed");
}
}
int main(){
    try{packet_boundaries();dc_and_bounds();sine_quality();drift();contact_and_source();benchmark();jitter();}
    catch(const std::exception& e){check(false,e.what());}
    check(checks>=8000000,"minimum meaningful executed assertions");
    for(const auto& [name,count]:failed)std::cerr<<"FAIL "<<name<<" COUNT="<<count<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
