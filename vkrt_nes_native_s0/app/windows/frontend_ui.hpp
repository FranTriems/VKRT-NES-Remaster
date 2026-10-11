#pragma once
#include <frontend_config.hpp>
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/presentation_profile.hpp"
#include <array>
#include <memory>
#include <span>
#include <iosfwd>

namespace vkrt::nes::frontend {
inline constexpr std::array<Mode,2> visual_modes{Mode::EXACT_DIGITAL,Mode::MODERN_REMASTER};
inline constexpr std::array<const wchar_t*,2> visual_names{L"Original",L"Remaster"};
Mode visual_mode(unsigned) noexcept;
const wchar_t* visual_name(unsigned) noexcept;
int shortcut_mode(unsigned key) noexcept;
enum class Page { home,game,pause,video,audio,controls,advanced };
enum class Command : unsigned {
    none,resume,open,reset,video,settings,audio,controls,advanced,quit,back,
    integer_scale,fullscreen,stats,focus_pause,defaults,dismiss,
    save_state,load_state,slot_next,volume_down,volume_up,mute,gamepad,home,
    mode0=32,mode1,mode2,mode3,recent0=48,bind0=64,pad_bind0=80
};
struct Rect {
    int x{},y{},w{},h{};
    bool contains(int px,int py) const noexcept {return px>=x&&py>=y&&px<x+w&&py<y+h;}
    bool operator==(const Rect&) const = default;
};
Rect viewport(int width,int height,bool integer_scale) noexcept;
Rect game_viewport(int width,int height,bool integer_scale,double ui_scale) noexcept;
// Frontend-only 512x480 upload. Neutralize witnessed left-edge blanking, never
// fetched background or visible sprites. No crop, rescale or canonical write.
void compose_playfield(const Frame&,const Image&,std::span<std::uint8_t> bgra);
void paint_stats(void* hdc,int width,int height,std::wstring_view text);
struct FullscreenState {
    bool active{},restore_valid{};
    Rect restore;
    Rect transition(bool desired,Rect current,Rect monitor) noexcept;
};
struct State {
    PresentationProfile profile{PresentationProfile::standard};
    Page page{Page::home};
    Page settings_parent{Page::pause};
    bool session{},focused{true},minimized{},loading{},replay{};
    bool tape_locked{},gamepad_connected{};
    int selection{},binding{-1},pad_binding{-1};
    std::wstring visual_notice;
    std::wstring error,notice,audio_status{L"No active session"},presenter{L"COMPOSED (production)"},statistics;
};
bool should_pause(const State&,const Config&) noexcept;
void escape(State&) noexcept;
struct Effect { bool open{},reset{},quit{},save{},fullscreen{},save_state{},load_state{};int recent{-1}; };
Effect route(State&,Config&,Command);
struct Item {Rect rect;Command command{Command::none};std::wstring label;bool selected{},primary{},enabled{true};};
struct Layout {
    std::array<Item,32> items;
    unsigned count{};
    double scale{1};int ox{},oy{};
    Rect rect(int x,int y,int w,int h) const noexcept;
};
Layout layout(const State&,const Config&,int width,int height,unsigned dpi);
int hit_test(const Layout&,int x,int y) noexcept;
void move_selection(State&,const Layout&,int direction) noexcept;
void navigate(State&,const Layout&,Direction) noexcept;
struct ControllerResult {Command command{Command::none};bool changed{},save{};};
class ControllerUiModel {
public:
    ControllerResult update(State&,Config&,const Layout&,PadSnapshot,std::uint64_t milliseconds);
    void cancel(State&) noexcept;
private:
    NavigationModel navigation_;BindingCapture capture_;TriggerCycle triggers_;
    SystemMenuButton menu_;
    int listening_{-1};
};
void controller_diagnostics(std::ostream&);

// Reused top-down DIB, DC and font handles. GDI draws only into this CPU buffer.
// The production presenter never receives a UI pixel or a changed frame extent.
class Canvas {
public:
    Canvas();~Canvas();
    Canvas(const Canvas&)=delete;Canvas& operator=(const Canvas&)=delete;
    void draw(const State&,const Config&,const Layout&,int width,int height,
              std::span<const std::uint8_t> game_bgra={});
    void blit(void* hdc) const;
    std::span<const std::uint8_t> pixels() const;
    std::uint64_t hash() const;
    void write_bmp(const std::filesystem::path&) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Probe uses the exact production layout/compositor with a first-party abstract
// image. No config I/O, ROM file, Machine, audio endpoint or presenter is needed.
std::array<std::uint64_t,4> ui_probe(const std::filesystem::path& output);
}
