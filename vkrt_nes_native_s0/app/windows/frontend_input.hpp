#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace vkrt::nes::frontend {
// Array order is the existing NES serial order, not UI display order.
using Bindings = std::array<unsigned,8>;
inline constexpr Bindings default_bindings{'Z','X',8,13,38,40,37,39};
inline constexpr std::array<const wchar_t*,8> button_names{
    L"A",L"B",L"Select",L"Start",L"Up",L"Down",L"Left",L"Right"};
bool bindable(unsigned key) noexcept;
bool valid_bindings(const Bindings&) noexcept;
bool bind_key(Bindings&, unsigned button, unsigned key) noexcept;
std::wstring key_name(unsigned);
std::uint8_t keyboard_buttons(const Bindings&, const std::array<bool,256>&) noexcept;
std::uint8_t pad_buttons(unsigned buttons, int x, int y) noexcept;
std::uint8_t resolve_directions(std::uint8_t) noexcept;
std::uint8_t combine_buttons(std::uint8_t keyboard,std::uint8_t controller) noexcept;
inline constexpr unsigned pad_lt=1u<<16,pad_rt=1u<<17;
inline constexpr unsigned pad_a=0x1000,pad_b=0x2000;
inline constexpr unsigned pad_menu=0x80; // R3 / XINPUT_GAMEPAD_RIGHT_THUMB, system-only.
using PadBindings=std::array<unsigned,4>; // NES A, B, Select, Start
inline constexpr PadBindings default_pad_bindings{pad_a,0x6000,0x20,0x10};
inline constexpr std::array<unsigned,11> pad_choices{pad_a,pad_b,0x4000,0x8000,0x100,0x200,0x40,0x10,0x20,pad_lt,pad_rt};
struct PadSnapshot {
    bool connected{};unsigned buttons{};int x{},y{};unsigned lt{},rt{};
};
class SystemMenuButton {
public:
    bool update(const PadSnapshot&,bool active) noexcept;
private:
    bool armed_{};
};
bool valid_pad_bindings(const PadBindings&) noexcept;
bool bind_pad(PadBindings&,unsigned button,unsigned code) noexcept;
std::wstring pad_name(unsigned);
std::uint8_t mapped_pad_buttons(const PadSnapshot&,const PadBindings&) noexcept;
enum class Direction { none,up,down,left,right };
struct PadEvents {Direction move{};bool confirm{},back{},start{};};
class NavigationModel {
public:
    PadEvents update(const PadSnapshot&,std::uint64_t milliseconds,bool active) noexcept;
private:
    bool ready_{};unsigned previous_{};Direction direction_{};std::uint64_t next_{};
};
enum class CaptureResult { listening,captured,cancelled };
class BindingCapture {
public:
    void begin() noexcept {armed_=false;b_pending_=false;}
    CaptureResult update(const PadSnapshot&,std::uint64_t milliseconds,unsigned& code,bool escape=false) noexcept;
private:
    bool armed_{},b_pending_{};std::uint64_t b_since_{};
};
class TriggerCycle {
public:
    int update(const PadSnapshot&,bool playing,bool capture,const PadBindings&) noexcept;
private:
    bool left_{},right_{};
};
class PadModel {
public:
    void update(bool enabled,bool connected,unsigned buttons,int x,int y) noexcept;
    bool connected() const noexcept { return connected_; }
    std::uint8_t buttons() const noexcept { return buttons_; }
private:
    bool connected_{};
    std::uint8_t buttons_{};
};

// Reconfiguration occurs on the UI lane; frame-boundary sampling stays on the
// emulation lane. No UI callbacks, allocation or locks occur in sample().
class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    void configure(const Bindings&) noexcept;
    void configure_pad(const PadBindings&) noexcept;
    PadSnapshot snapshot() const noexcept;
    void enable_gamepad(bool enabled) noexcept { enabled_.store(enabled);if(!enabled)pad_.store(0); }
    std::uint8_t sample(void* foreground_window) const noexcept;
    bool controller_connected() const noexcept { return enabled_.load()&&connected_.load(); }
private:
    std::array<std::atomic<unsigned>,8> keys_{};
    void* module_{};
    void* get_state_{};
    mutable std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{true},stop_{false};
    std::atomic<std::uint8_t> pad_{0};
    std::atomic<std::uint64_t> raw_{0};
    std::array<std::atomic<unsigned>,4> bindings_{};
    std::thread polling_;
};
}
