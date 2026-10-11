#include <runtime_audio.hpp>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>

namespace vkrt::nes::host {
void OutputGain::apply(std::span<std::uint8_t> bytes,unsigned frames,const runtime::EndpointFormat& f) noexcept {
    if(!f.supported()||frames>bytes.size()/f.frame_bytes())return;
    const double target=std::min(target_.load(),100u)/100.;
    const double step=1./std::max(1.,double(f.rate)*.005);
    const auto size=f.bits/8;
    for(unsigned frame=0;frame<frames;++frame){
        if(current_<target)current_=std::min(target,current_+step);
        else if(current_>target)current_=std::max(target,current_-step);
        for(unsigned channel=0;channel<f.channels;++channel){
            auto* out=bytes.data()+(std::size_t(frame)*f.channels+channel)*size;
            if(f.encoding==runtime::EndpointEncoding::ieee_float){
                float sample{};std::memcpy(&sample,out,sizeof(sample));
                sample=current_>0&&std::isfinite(sample)?float(std::clamp(double(sample),-1.,1.)*current_):0.f;
                std::memcpy(out,&sample,sizeof(sample));
            }else{
                std::uint32_t bits=0;for(unsigned i=0;i<size;++i)bits|=std::uint32_t(out[i])<<(8*i);
                std::int64_t sample=bits;
                if(bits&(std::uint32_t(1)<<(f.bits-1)))sample-=std::int64_t(1)<<f.bits;
                auto scaled=static_cast<std::int64_t>(double(sample)*current_);
                // Endpoint PCM valid bits are left aligned; keep padding zero.
                const auto shift=f.bits-f.valid_bits;
                auto result=static_cast<std::uint32_t>(scaled)&(~std::uint32_t(0)<<shift);
                for(unsigned i=0;i<size;++i)out[i]=std::uint8_t(result>>(8*i));
            }
        }
    }
}
using Microsoft::WRL::ComPtr;
namespace {
void require(HRESULT hr){if(FAILED(hr))throw std::runtime_error("audio endpoint operation failed");}
struct MixFormat {WAVEFORMATEX* value{};~MixFormat(){CoTaskMemFree(value);}};
struct ClientStop {IAudioClient* value;~ClientStop(){if(value)value->Stop();}};
runtime::EndpointFormat describe(const WAVEFORMATEX& w) {
    runtime::EndpointFormat f;
    f.rate=w.nSamplesPerSec;f.channels=w.nChannels;f.bits=w.wBitsPerSample;f.valid_bits=f.bits;
    WORD tag=w.wFormatTag;
    if(tag==WAVE_FORMAT_EXTENSIBLE&&w.cbSize>=22){
        const auto& ex=reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(w);
        f.channel_mask=ex.dwChannelMask;f.valid_bits=ex.Samples.wValidBitsPerSample;
        if(IsEqualGUID(ex.SubFormat,KSDATAFORMAT_SUBTYPE_PCM))tag=WAVE_FORMAT_PCM;
        else if(IsEqualGUID(ex.SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))tag=WAVE_FORMAT_IEEE_FLOAT;
    }
    if(tag!=WAVE_FORMAT_PCM&&tag!=WAVE_FORMAT_IEEE_FLOAT)throw std::runtime_error("unsupported endpoint encoding");
    f.encoding=tag==WAVE_FORMAT_PCM?runtime::EndpointEncoding::pcm:runtime::EndpointEncoding::ieee_float;
    if(!f.supported()||w.nBlockAlign!=f.frame_bytes())throw std::runtime_error("unsupported endpoint layout");
    return f;
}
}
EndpointAudio::EndpointAudio(double native_rate):stream(native_rate),
    stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),ready_(CreateEventW(nullptr,TRUE,FALSE,nullptr)) {
    if(!stop_.value||!ready_.value)throw std::runtime_error("audio event creation failed");
    thread_=std::thread([this]{service();});
    // Startup only: producer has not started. Endpoint waits never enter its lane.
    if(WaitForSingleObject(ready_.value,10000)!=WAIT_OBJECT_0){stop();throw std::runtime_error("audio initialization timeout");}
    if(!backend_id.load()){stop();throw std::runtime_error("no usable audio endpoint");}
}
EndpointAudio::~EndpointAudio(){stop();}
void EndpointAudio::stop(){SetEvent(stop_.value);if(thread_.joinable())thread_.join();}
std::string EndpointAudio::backend() const {
    switch(backend_id.load()){
    case 3:return "WASAPI3_EVENT";case 2:return "WASAPI_EVENT";case 1:return "WAVEOUT_EVENT";default:return "FAILED";}
}
void EndpointAudio::service(){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    {
        Mmcss mmcss(L"Pro Audio");
        bool served=false;
        if(SUCCEEDED(com))try{served=wasapi();}catch(...){++failures;}
        if(!served&&WaitForSingleObject(stop_.value,0)!=WAIT_OBJECT_0){
            try{waveout();}catch(...){++failures;backend_id.store(0);}
        }
        SetEvent(ready_.value);
    }
    if(SUCCEEDED(com))CoUninitialize();
}
bool EndpointAudio::wasapi(){
    ComPtr<IMMDeviceEnumerator> enumerator;
    require(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
    ComPtr<IMMDevice> device;require(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device));
    ComPtr<IAudioClient> client;require(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
    MixFormat mix;require(client->GetMixFormat(&mix.value));
    const auto format=describe(*mix.value);
    ComPtr<IAudioClient3> client3;
    unsigned chosen=0;HRESULT initialized=E_FAIL;unsigned backend=2;
    if(SUCCEEDED(client.As(&client3))){
        UINT32 normal{},fundamental{},minimum{},maximum{};
        if(SUCCEEDED(client3->GetSharedModeEnginePeriod(mix.value,&normal,&fundamental,&minimum,&maximum))&&fundamental){
            // Default shared period is a defensible CPU/latency tradeoff under capture.
            chosen=std::clamp(normal,minimum,maximum);
            initialized=client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,chosen,mix.value,nullptr);
            if(SUCCEEDED(initialized))backend=3;
        }
    }
    if(FAILED(initialized)){
        client3.Reset();client.Reset();
        require(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
        REFERENCE_TIME duration{},minimum{};require(client->GetDevicePeriod(&duration,&minimum));
        chosen=std::max(1u,unsigned((duration*format.rate+9999999)/10000000));
        require(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,0,0,mix.value,nullptr));
    }
    Handle event(CreateEventW(nullptr,FALSE,FALSE,nullptr));if(!event.value)throw std::runtime_error("audio event");
    require(client->SetEventHandle(event.value));
    UINT32 capacity{};require(client->GetBufferSize(&capacity));
    ComPtr<IAudioRenderClient> render;require(client->GetService(IID_PPV_ARGS(&render)));
    stream.configure(format,chosen);rate.store(format.rate);period.store(chosen);
    BYTE* data{};require(render->GetBuffer(capacity,&data));
    require(render->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT));
    require(client->Start());ClientStop running{client.Get()};
    backend_id.store(backend);SetEvent(ready_.value);
    HANDLE events[]{stop_.value,event.value};
    for(;;){
        const auto wait=WaitForMultipleObjects(2,events,FALSE,2000);
        if(wait==WAIT_OBJECT_0)return true;
        if(wait!=WAIT_OBJECT_0+1)throw std::runtime_error("audio endpoint event timeout/failure");
        UINT32 padding{};require(client->GetCurrentPadding(&padding));
        const UINT32 frames=capacity-padding;if(!frames)continue;
        require(render->GetBuffer(frames,&data));
        stream.render({data,frames*format.frame_bytes()},frames,paused.load());
        gain_.apply({data,frames*format.frame_bytes()},frames,format);
        require(render->ReleaseBuffer(frames,0));
    }
}
void EndpointAudio::waveout(){
    constexpr unsigned frames=480;
    struct Block {WAVEHDR header{};std::array<std::uint8_t,frames*4> data{};bool prepared{},queued{};};
    struct Device {
        HWAVEOUT value{};std::array<Block,3> blocks;
        ~Device(){if(value){waveOutReset(value);for(auto& b:blocks)if(b.prepared)waveOutUnprepareHeader(value,&b.header,sizeof(WAVEHDR));waveOutClose(value);}}
    } device;
    Handle event(CreateEventW(nullptr,FALSE,FALSE,nullptr));if(!event.value)throw std::runtime_error("wave event");
    WAVEFORMATEX w{WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    if(waveOutOpen(&device.value,WAVE_MAPPER,&w,reinterpret_cast<DWORD_PTR>(event.value),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
        throw std::runtime_error("waveOut fallback open failed");
    runtime::EndpointFormat f;f.bits=f.valid_bits=16;f.encoding=runtime::EndpointEncoding::pcm;
    stream.configure(f,frames);rate.store(f.rate);period.store(frames);
    for(auto& b:device.blocks){
        b.header.lpData=reinterpret_cast<LPSTR>(b.data.data());b.header.dwBufferLength=DWORD(b.data.size());
        if(waveOutPrepareHeader(device.value,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)throw std::runtime_error("wave header failed");
        b.prepared=true;
    }
    backend_id.store(1);SetEvent(ready_.value);
    HANDLE events[]{stop_.value,event.value};
    for(;;){
        for(auto& b:device.blocks)if(!b.queued||(b.header.dwFlags&WHDR_DONE)){
            stream.render(b.data,frames,paused.load());
            gain_.apply(b.data,frames,f);
            if(waveOutWrite(device.value,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)throw std::runtime_error("wave write failed");
            b.queued=true;
        }
        const auto wait=WaitForMultipleObjects(2,events,FALSE,2000);
        if(wait==WAIT_OBJECT_0)return;
        if(wait!=WAIT_OBJECT_0+1)throw std::runtime_error("wave event failure");
    }
}
}
