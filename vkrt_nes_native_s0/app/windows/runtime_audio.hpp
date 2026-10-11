#pragma once
#include <runtime_windows.hpp>
#include <thread>
#include <string>

namespace vkrt::nes::host {
// Consumer-owned ramp; UI only publishes an atomic target. Five milliseconds
// at the current endpoint rate, independent of channels, cadence and DRC.
class OutputGain {
public:
    void set(unsigned percent,bool mute) noexcept { target_.store(mute?0:std::min(percent,100u)); }
    void apply(std::span<std::uint8_t>,unsigned frames,const runtime::EndpointFormat&) noexcept;
    double current() const noexcept { return current_; } // Consumer/test lane only.
private:
    std::atomic<unsigned> target_{100};
    double current_{1};
};
class EndpointAudio {
public:
    explicit EndpointAudio(double native_rate);
    ~EndpointAudio();
    EndpointAudio(const EndpointAudio&)=delete;
    EndpointAudio& operator=(const EndpointAudio&)=delete;
    void submit(std::span<const std::int16_t> pcm){stream.submit(pcm);}
    void flush(){stream.request_flush();}
    void set_volume(unsigned percent,bool mute) noexcept { gain_.set(percent,mute); }
    void stop();
    std::string backend() const;
    runtime::AudioStream stream;
    std::atomic<bool> paused{false};
    std::atomic<unsigned> rate{0},period{0},backend_id{0},failures{0};
private:
    OutputGain gain_;
    void service();
    bool wasapi();
    void waveout();
    Handle stop_,ready_;
    std::thread thread_;
};
}
