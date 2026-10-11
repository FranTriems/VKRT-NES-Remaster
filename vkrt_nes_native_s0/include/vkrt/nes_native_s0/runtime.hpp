#pragma once
// VKRT NES M4-R4X: product-runtime primitives. Presentation/runtime only.
#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <utility>
#include <span>
#include <string>
#include <vector>

namespace vkrt::nes::runtime {

inline constexpr std::uint64_t kNtscFrameRateNumerator = 150247; // 60.0988 * 2500
inline constexpr std::uint64_t kNtscFrameRateDenominator = 2500;
inline constexpr std::uint64_t kFramePeriodNumeratorNs =
    1000000000ull * kNtscFrameRateDenominator;

class RationalFramePacer {
public:
    std::uint64_t next_period_ns() noexcept {
        const std::uint64_t total = kFramePeriodNumeratorNs + remainder_;
        const std::uint64_t delta = total / kNtscFrameRateNumerator;
        remainder_ = total % kNtscFrameRateNumerator;
        elapsed_ns_ += delta;
        ++frames_;
        return delta;
    }
    void reset() noexcept { remainder_ = elapsed_ns_ = frames_ = 0; }
    std::uint64_t elapsed_ns() const noexcept { return elapsed_ns_; }
    std::uint64_t frames() const noexcept { return frames_; }
private:
    std::uint64_t remainder_{};
    std::uint64_t elapsed_ns_{};
    std::uint64_t frames_{};
};

template<class T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2);
public:
    bool push(const T& value) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (h - t >= Capacity) return false;
        data_[static_cast<std::size_t>(h % Capacity)] = value;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        if (t == h) return false;
        value = std::move(data_[static_cast<std::size_t>(t % Capacity)]);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    std::size_t push_span(std::span<const T> values) noexcept {
        std::size_t n = 0;
        for (const auto& v : values) {
            if (!push(v)) break;
            ++n;
        }
        return n;
    }
    std::size_t pop_span(std::span<T> values) noexcept {
        std::size_t n = 0;
        for (auto& v : values) {
            if (!pop(v)) break;
            ++n;
        }
        return n;
    }
    std::size_t size_approx() const noexcept {
        const auto t = tail_.load(std::memory_order_acquire);
        const auto h = head_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(std::min<std::uint64_t>(h - t, Capacity));
    }
    std::uint64_t write_position() const noexcept { return head_.load(std::memory_order_acquire); }
    // Consumer only. A producer can request a flush through a published watermark
    // without racing cursor resets or discarding samples produced after the reset.
    void discard_before(std::uint64_t boundary) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        tail_.store(std::max(t, std::min(h, boundary)), std::memory_order_release);
    }
    constexpr std::size_t capacity() const noexcept { return Capacity; }
    void reset_quiescent() noexcept {
        tail_.store(0, std::memory_order_relaxed);
        head_.store(0, std::memory_order_relaxed);
    }
private:
    std::array<T, Capacity> data_{};
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
};


template<class T>
class LatestMailbox {
public:
    std::uint64_t publish(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = std::move(value);
        return ++sequence_;
    }
    bool consume_after(std::uint64_t& last_sequence, T& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sequence_ == 0 || sequence_ == last_sequence) return false;
        out = value_;
        last_sequence = sequence_;
        return true;
    }
    std::uint64_t sequence() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sequence_;
    }
private:
    mutable std::mutex mutex_;
    T value_{};
    std::uint64_t sequence_{};
};

struct ReplayFrame {
    std::uint8_t buttons{};
    std::uint8_t commands{}; // bit0 = warm reset before this frame.
    bool operator==(const ReplayFrame&) const = default;
};
inline constexpr std::uint8_t kReplayWarmReset = 1u << 0;

class ReplayTape {
public:
    ReplayTape() = default;
    explicit ReplayTape(std::string rom_sha256) : rom_sha256_(std::move(rom_sha256)) {}
    void append(ReplayFrame f) { frames_.push_back(f); }
    const std::vector<ReplayFrame>& frames() const noexcept { return frames_; }
    const std::string& rom_sha256() const noexcept { return rom_sha256_; }
    void set_rom_sha256(std::string v) { rom_sha256_ = std::move(v); }
    void save(const std::filesystem::path&) const;
    static ReplayTape load(const std::filesystem::path&);
    std::uint64_t fingerprint64() const noexcept;
private:
    std::string rom_sha256_;
    std::vector<ReplayFrame> frames_;
};

std::uint32_t crc32(std::span<const std::uint8_t>) noexcept;

// Round relative timer waits UP to 100 ns, never request an absolute FILETIME.
std::int64_t relative_timer_due(std::uint64_t remaining_ns) noexcept;
std::uint64_t bounded_deadline(std::uint64_t deadline, std::uint64_t now) noexcept;

class DriftController {
public:
    static constexpr double limit = .002; // 2000 ppm, clock drift only
    static constexpr double slew_per_second = .0002;
    double update(double occupancy, double target, double native_rate, double seconds) noexcept;
    void reset() noexcept { filtered_=integral_=correction_=0; initialized_=false; }
    double correction() const noexcept { return correction_; }
    double integral() const noexcept { return integral_; }
private:
    double filtered_{}, integral_{}, correction_{};
    bool initialized_{};
};

using NativeAudioFifo = SpscRing<std::int16_t,16384>;
class CubicResampler {
public:
    bool next(NativeAudioFifo&, double step, float& out) noexcept;
    void reset() noexcept { phase_=0; loaded_=0; history_={}; }
private:
    std::array<double,4> history_{};
    double phase_{};
    unsigned loaded_{};
};

// Compact fractional-delay low-pass FIR. Configure on the endpoint consumer
// before rendering; no allocation or coefficient construction in next().
// The cubic primitive above remains available for historical callers/tests.
class BandLimitedResampler {
public:
    static constexpr unsigned taps = 24, phases = 256;
    explicit BandLimitedResampler(double max_step = 1.) { configure(max_step); }
    void configure(double max_step);
    bool next(NativeAudioFifo&, double step, float& out) noexcept;
    void reset() noexcept { phase_=0; loaded_=head_=0; history_={}; }
private:
    std::array<std::array<float,taps>,phases+1> kernel_{};
    std::array<float,taps*2> history_{}; // mirrored ring: contiguous FIR window
    double phase_{}, max_step_{1.};
    unsigned loaded_{}, head_{};
};

enum class EndpointEncoding { pcm, ieee_float };
struct EndpointFormat {
    unsigned rate{48000}, channels{2}, bits{32}, valid_bits{32};
    std::uint32_t channel_mask{};
    EndpointEncoding encoding{EndpointEncoding::ieee_float};
    bool supported() const noexcept;
    std::size_t frame_bytes() const noexcept { return channels*(bits/8); }
};
void encode_endpoint(float mono, const EndpointFormat&, std::span<std::uint8_t>) noexcept;

// One producer calls submit/request_flush; one endpoint consumer calls render.
// Configuration is consumer-owned and never influences canonical emulation.
class AudioStream {
public:
    explicit AudioStream(double native_rate);
    void configure(const EndpointFormat&, unsigned period_frames);
    void submit(std::span<const std::int16_t>) noexcept;
    void request_flush() noexcept;
    void render(std::span<std::uint8_t> interleaved, unsigned frames, bool paused=false) noexcept;
    std::size_t occupancy() const noexcept { return fifo_.size_approx(); }
    std::size_t target() const noexcept { return target_.load(std::memory_order_relaxed); }
    double correction() const noexcept { return correction_ppm.load()*1.e-6; }
    std::atomic<std::uint64_t> underruns{0}, dropped{0};
    std::atomic<int> correction_ppm{0};
private:
    NativeAudioFifo fifo_;
    BandLimitedResampler resampler_;
    DriftController drc_;
    EndpointFormat format_;
    double native_rate_;
    unsigned period_{};
    std::atomic<std::size_t> target_{0};
    bool started_{},starved_{};
    std::atomic<std::uint64_t> flush_boundary_{0}, flush_serial_{0};
    std::uint64_t seen_flush_{};
};

struct StressResult {
    std::uint64_t underruns{}, dropped{}, output_frames{}, checksum{};
    int max_correction_ppm{};
    double final_occupancy_ms{};
};
StressResult simulate_audio(double native_rate, unsigned endpoint_rate, unsigned period,
                            int clock_ppm, unsigned seconds, std::span<const std::int16_t> pcm = {});

} // namespace vkrt::nes::runtime
