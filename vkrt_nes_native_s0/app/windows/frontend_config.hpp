#pragma once
#include <frontend_input.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vkrt::nes::frontend {
inline constexpr std::size_t max_config_bytes=65536, max_recents=10, max_path_units=1024;
struct Config {
    unsigned visual_mode{3}; // Original/F1 = 0, Remaster/F4 = 3; not a core Mode ordinal.
    bool integer_scaling{true},fullscreen{},stats{},pause_on_focus_loss{true};
    unsigned volume{100},state_slot{1};
    bool mute{},gamepad_enabled{true};
    Bindings keys{default_bindings};
    PadBindings pad{default_pad_bindings};
    std::vector<std::wstring> recents;
    bool operator==(const Config&) const = default;
};
std::string utf8(std::wstring_view);
std::wstring from_utf8(std::string_view);
std::wstring normalized_rom_path(std::wstring_view);
void remember(Config&,std::wstring_view);
Config parse_config(std::string_view);
std::string serialize_config(const Config&);
std::filesystem::path config_path();
Config load_config(const std::filesystem::path&,std::wstring& warning);
bool save_config(const std::filesystem::path&,const Config&,std::wstring& error);
}
