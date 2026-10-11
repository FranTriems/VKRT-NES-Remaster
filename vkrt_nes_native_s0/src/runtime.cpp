#include "vkrt/nes_native_s0/runtime.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace vkrt::nes::runtime {
namespace {
constexpr std::array<std::uint8_t,8> kMagic{'V','K','R','T','R','P','L','1'};
constexpr std::uint32_t kVersion = 1;

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i=0;i<4;++i) b.push_back(std::uint8_t(v>>(8*i)));
}
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i=0;i<8;++i) b.push_back(std::uint8_t(v>>(8*i)));
}
std::uint32_t get32(std::span<const std::uint8_t> b, std::size_t& p) {
    if (p+4>b.size()) throw std::runtime_error("replay truncated u32");
    std::uint32_t v=0; for(int i=0;i<4;++i)v|=std::uint32_t(b[p++])<<(8*i); return v;
}
std::uint64_t get64(std::span<const std::uint8_t> b, std::size_t& p) {
    if (p+8>b.size()) throw std::runtime_error("replay truncated u64");
    std::uint64_t v=0; for(int i=0;i<8;++i)v|=std::uint64_t(b[p++])<<(8*i); return v;
}
bool hex64(const std::string& s) {
    if(s.size()!=64) return false;
    return std::all_of(s.begin(),s.end(),[](unsigned char c){
        return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    });
}
std::vector<std::uint8_t> canonical(const ReplayTape& t) {
    if(!hex64(t.rom_sha256())) throw std::runtime_error("replay ROM SHA256 must be 64 hex chars");
    std::vector<std::uint8_t> b;
    b.reserve(96+t.frames().size()*2);
    b.insert(b.end(),kMagic.begin(),kMagic.end());
    put32(b,kVersion);
    put32(b,std::uint32_t(kNtscFrameRateNumerator));
    put32(b,std::uint32_t(kNtscFrameRateDenominator));
    put64(b,std::uint64_t(t.frames().size()));
    b.insert(b.end(),t.rom_sha256().begin(),t.rom_sha256().end());
    for(const auto& f:t.frames()){b.push_back(f.buttons);b.push_back(f.commands);}
    return b;
}
}

std::uint32_t crc32(std::span<const std::uint8_t> b) noexcept {
    std::uint32_t c=0xffffffffu;
    for(auto x:b){
        c^=x;
        for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));
    }
    return c^0xffffffffu;
}

void ReplayTape::save(const std::filesystem::path& path) const {
    auto b=canonical(*this);
    put32(b,crc32(b));
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if(!f) throw std::runtime_error("cannot create replay");
    f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    if(!f) throw std::runtime_error("replay write failed");
}

ReplayTape ReplayTape::load(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f) throw std::runtime_error("cannot open replay");
    const auto n=f.tellg();
    if(n<92 || n>std::streamoff(256*1024*1024)) throw std::runtime_error("replay size invalid");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);
    if(!f) throw std::runtime_error("replay read failed");
    if(b.size()<4) throw std::runtime_error("replay truncated");
    const std::size_t payload=b.size()-4;
    std::size_t cp=payload;
    const auto stored=get32(b,cp);
    if(crc32(std::span<const std::uint8_t>(b.data(),payload))!=stored)
        throw std::runtime_error("replay CRC mismatch");
    std::size_t p=0;
    for(auto x:kMagic) if(p>=payload||b[p++]!=x) throw std::runtime_error("replay magic mismatch");
    if(get32(b,p)!=kVersion) throw std::runtime_error("replay version unsupported");
    if(get32(b,p)!=kNtscFrameRateNumerator||get32(b,p)!=kNtscFrameRateDenominator)
        throw std::runtime_error("replay timing mismatch");
    const auto frames=get64(b,p);
    if(p+64>payload) throw std::runtime_error("replay ROM hash truncated");
    std::string rom(reinterpret_cast<const char*>(b.data()+p),64);p+=64;
    if(!hex64(rom)) throw std::runtime_error("replay ROM hash invalid");
    if(frames>(payload-p)/2 || p+frames*2!=payload)
        throw std::runtime_error("replay frame count mismatch");
    ReplayTape t(std::move(rom));
    t.frames_.reserve(static_cast<std::size_t>(frames));
    for(std::uint64_t i=0;i<frames;++i){
        ReplayFrame r{b[p],b[p+1]};p+=2;
        if(r.commands&~kReplayWarmReset) throw std::runtime_error("replay command bits unsupported");
        t.frames_.push_back(r);
    }
    return t;
}

std::uint64_t ReplayTape::fingerprint64() const noexcept {
    std::uint64_t h=1469598103934665603ull;
    auto mix=[&](std::uint8_t v){h^=v;h*=1099511628211ull;};
    for(unsigned char c:rom_sha256_)mix(c);
    for(const auto& f:frames_){mix(f.buttons);mix(f.commands);}
    return h;
}
} // namespace vkrt::nes::runtime

namespace vkrt::nes::runtime {
std::int64_t relative_timer_due(std::uint64_t ns) noexcept {
    return -static_cast<std::int64_t>(std::max<std::uint64_t>(1, ns/100+(ns%100!=0)));
}
std::uint64_t bounded_deadline(std::uint64_t deadline, std::uint64_t now) noexcept {
    // Allow at most two video periods of catch-up; rebase host time after a stall.
    // No frame, input, or PCM is skipped and the rational phase is retained.
    constexpr auto budget=2*kFramePeriodNumeratorNs/kNtscFrameRateNumerator;
    return now>deadline && now-deadline>budget ? now : deadline;
}
double DriftController::update(double occupancy,double target,double rate,double dt) noexcept {
    dt=std::clamp(dt,0.,.1);
    // Bound the filter state too: an impossible/stalled occupancy must not leave
    // a long-lived low-pass tail after the integral has correctly saturated.
    const double error=std::clamp((occupancy-target)/rate,-.1,.1);
    if(!initialized_){filtered_=0;initialized_=true;}
    filtered_+=(error-filtered_)*dt/(2.+dt);
    const double p=.14*filtered_;
    const double candidate=std::clamp(integral_+.006*filtered_*dt,-limit,limit);
    // Conditional integration: saturation can unwind, but cannot wind up.
    if(std::abs(p+candidate)<=limit || (p+candidate)*filtered_<0)integral_=candidate;
    const double wanted=std::clamp(p+integral_,-limit,limit);
    correction_+=std::clamp(wanted-correction_,-slew_per_second*dt,slew_per_second*dt);
    correction_=std::clamp(correction_,-limit,limit);
    return correction_;
}
bool CubicResampler::next(NativeAudioFifo& fifo,double step,float& out) noexcept {
    std::int16_t sample{};
    while(loaded_<4){
        if(!fifo.pop(sample))return false;
        history_[loaded_++]=sample;
    }
    while(phase_>=1.){
        if(!fifo.pop(sample))return false;
        history_[0]=history_[1];history_[1]=history_[2];history_[2]=history_[3];history_[3]=sample;
        phase_-=1.;
    }
    const auto a=history_[0],b=history_[1],c=history_[2],d=history_[3],t=phase_;
    const double v=b+.5*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)));
    out=static_cast<float>(std::clamp(v/32768.,-1.,32767./32768.));
    phase_+=step;
    return true;
}
void BandLimitedResampler::configure(double max_step) {
    if(!std::isfinite(max_step)||max_step<=0||max_step>4.)
        throw std::invalid_argument("FIR maximum step");
    max_step_=max_step;
    // Leave a small transition band below the lower Nyquist limit. The same
    // table covers the entire DRC range, avoiding coefficient/pitch switching.
    const double cutoff=.94/std::max(1.,max_step);
    constexpr double pi=3.14159265358979323846, radius=taps/2.;
    for(unsigned p=0;p<=phases;++p){
        double sum=0;
        for(unsigned k=0;k<taps;++k){
            const double x=double(k)-(taps/2-1)-double(p)/phases;
            const double window=.42+.5*std::cos(pi*x/radius)+.08*std::cos(2*pi*x/radius);
            const double sinc=std::abs(x)<1.e-12?cutoff:std::sin(pi*cutoff*x)/(pi*x);
            kernel_[p][k]=static_cast<float>(sinc*window);sum+=kernel_[p][k];
        }
        for(auto& weight:kernel_[p])weight=static_cast<float>(weight/sum);
    }
    reset();
}
bool BandLimitedResampler::next(NativeAudioFifo& fifo,double step,float& out) noexcept {
    if(!std::isfinite(step)||step<=0||step>max_step_)return false;
    std::int16_t sample{};
    if(!loaded_){
        if(!fifo.pop(sample))return false;
        // Extend the first sample to the left: DC is exact from the first
        // output, with a time origin of native sample zero (12-sample lookahead).
        for(;loaded_<taps/2;++loaded_)history_[loaded_]=history_[loaded_+taps]=float(sample);
    }
    while(loaded_<taps){
        if(!fifo.pop(sample))return false;
        history_[loaded_]=history_[loaded_+taps]=float(sample);++loaded_;
    }
    while(phase_>=1.){
        if(!fifo.pop(sample))return false;
        history_[head_]=history_[head_+taps]=float(sample);
        if(++head_==taps)head_=0;
        phase_-=1.;
    }
    const double position=phase_*phases;
    const auto p=static_cast<unsigned>(position);
    const double blend=position-p;
    double value=0;
    for(unsigned k=0;k<taps;++k)
        value+=history_[head_+k]*(kernel_[p][k]+blend*(kernel_[p+1][k]-kernel_[p][k]));
    out=static_cast<float>(std::clamp(value/32768.,-1.,32767./32768.));
    phase_+=step;
    return true;
}
bool EndpointFormat::supported() const noexcept {
    if(rate<32000||rate>192000||channels<1||channels>8)return false;
    if(encoding==EndpointEncoding::ieee_float)return bits==32&&valid_bits==32;
    return (bits==16||bits==24||bits==32)&&valid_bits>=16&&valid_bits<=bits;
}
void encode_endpoint(float mono,const EndpointFormat& f,std::span<std::uint8_t> out) noexcept {
    if(out.size()<f.frame_bytes())return;
    mono=std::clamp(mono,-1.f,32767.f/32768.f);
    std::uint32_t mask=f.channel_mask;
    for(unsigned ch=0;ch<f.channels;++ch){
        const auto speaker=mask&(~mask+1u);mask&=mask-1;
        const float v=speaker==8u?0.f:mono; // Never send full-band mono to LFE.
        auto* dst=out.data()+ch*(f.bits/8);
        if(f.encoding==EndpointEncoding::ieee_float){std::memcpy(dst,&v,4);continue;}
        const double scale=std::ldexp(1.,int(f.valid_bits)-1);
        const auto value=static_cast<std::int64_t>(std::llround(double(v)*scale));
        const auto packed=static_cast<std::uint32_t>(value)*(1u<<(f.bits-f.valid_bits));
        for(unsigned byte=0;byte<f.bits/8;++byte)dst[byte]=std::uint8_t(packed>>(byte*8));
    }
}
AudioStream::AudioStream(double rate):native_rate_(rate) {
    if(rate<20000||rate>96000)throw std::invalid_argument("native audio rate");
    configure({},480);
}
void AudioStream::configure(const EndpointFormat& f,unsigned period) {
    if(!f.supported()||!period||period>f.rate/10)throw std::invalid_argument("endpoint format/period");
    format_=f;period_=period;
    // Two producer periods plus two endpoint periods cover burst phase and a
    // bounded late wake. At 48 kHz/480 this is 53.28 ms, not a huge delay buffer.
    const double seconds=std::max(.040,2.*kNtscFrameRateDenominator/kNtscFrameRateNumerator+2.*period/f.rate);
    target_=static_cast<std::size_t>(std::ceil(native_rate_*seconds));
    if(target_+native_rate_*.020>=fifo_.capacity())throw std::invalid_argument("endpoint period exceeds FIFO budget");
    started_=false;resampler_.configure(native_rate_/f.rate*(1.+DriftController::limit));drc_.reset();
}
void AudioStream::submit(std::span<const std::int16_t> pcm) noexcept {
    dropped.fetch_add(pcm.size()-fifo_.push_span(pcm),std::memory_order_relaxed);
}
void AudioStream::request_flush() noexcept {
    flush_boundary_.store(fifo_.write_position(),std::memory_order_relaxed);
    flush_serial_.fetch_add(1,std::memory_order_release);
}
void AudioStream::render(std::span<std::uint8_t> out,unsigned frames,bool paused) noexcept {
    if(out.size()<frames*format_.frame_bytes())return;
    const auto serial=flush_serial_.load(std::memory_order_acquire);
    if(serial!=seen_flush_){
        fifo_.discard_before(flush_boundary_.load(std::memory_order_relaxed));seen_flush_=serial;
        started_=false;starved_=false;resampler_.reset();drc_.reset();correction_ppm.store(0);
    }
    std::fill(out.begin(),out.end(),0);
    if(paused)return; // Deliberate silence while paused/prebuffering is not starvation.
    if(!started_){
        if(fifo_.size_approx()<target_){
            if(starved_)underruns.fetch_add(frames,std::memory_order_relaxed);
            return;
        }
        started_=true;starved_=false;
    }
    const auto correction=drc_.update(double(fifo_.size_approx()),double(target_),native_rate_,double(frames)/format_.rate);
    correction_ppm.store(int(std::llround(correction*1.e6)),std::memory_order_relaxed);
    const double step=native_rate_/format_.rate*(1.+correction);
    for(unsigned i=0;i<frames;++i){
        float v{};
        if(!resampler_.next(fifo_,step,v)){
            underruns.fetch_add(frames-i,std::memory_order_relaxed);
            // Rebuffer after real starvation; retain an honest endpoint-sample count.
            started_=false;starved_=true;resampler_.reset();drc_.reset();break;
        }
        encode_endpoint(v,format_,out.subspan(i*format_.frame_bytes(),format_.frame_bytes()));
    }
}
StressResult simulate_audio(double rate,unsigned endpoint,unsigned period,int ppm,unsigned seconds,
                            std::span<const std::int16_t> pcm) {
    AudioStream stream(rate);EndpointFormat fmt;fmt.rate=endpoint;stream.configure(fmt,period);
    std::vector<std::uint8_t> output(period*fmt.frame_bytes());
    std::vector<std::int16_t> input;
    RationalFramePacer pacer;
    std::uint64_t producer_due=0,endpoint_due=0,produced=0,frame=0,tick=0;
    const auto end=std::uint64_t(seconds)*1000000000ull;
    StressResult result;result.checksum=1469598103934665603ull;
    const double endpoint_ns=1.e9*period/endpoint/(1.+ppm*1.e-6);
    // Fixed seed and bounded, non-accumulating producer/endpoint deadline jitter.
    std::uint32_t rng=0x31415926;
    auto jitter=[&](unsigned bound){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng%bound;};
    producer_due=pacer.next_period_ns()+jitter(4000000);
    endpoint_due=std::uint64_t(++tick*endpoint_ns)+jitter(1500000);
    while(std::min(producer_due,endpoint_due)<end){
        if(producer_due<=endpoint_due){
            ++frame;
            const auto count=std::uint64_t(std::floor(frame*rate*kNtscFrameRateDenominator/kNtscFrameRateNumerator));
            input.clear();
            while(produced<count){
                input.push_back(pcm.empty()?std::int16_t((produced*73)%40001-20000):pcm[produced%pcm.size()]);++produced;
            }
            stream.submit(input);
            pacer.next_period_ns();
            producer_due=pacer.elapsed_ns()+jitter(4000000)+(frame%173==0?6000000:0);
        }else{
            stream.render(output,period);
            result.output_frames+=period;
            for(auto b:output)result.checksum=(result.checksum^b)*1099511628211ull;
            result.max_correction_ppm=std::max(result.max_correction_ppm,std::abs(stream.correction_ppm.load()));
            endpoint_due=std::uint64_t(++tick*endpoint_ns)+jitter(1500000);
        }
    }
    result.underruns=stream.underruns.load();result.dropped=stream.dropped.load();
    result.final_occupancy_ms=1000.*stream.occupancy()/rate;
    return result;
}
}
