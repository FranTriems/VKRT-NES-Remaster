#pragma once
// Runtime-adjacent internal presentation policy; core renderer enums stay intact.
#include "vkrt/nes_native_s0/remaster.hpp"
#include <array>
#include <string_view>

namespace vkrt::nes {
enum class PresentationProfile { standard, metroid_usa_f1f4 };
inline constexpr std::string_view metroid_digest =
    "e6e6b7014685adae447ebb3833242815747bc1e5df83ade79f693fb67cf565b6";
// Resolve once on the loading lane from the already-computed full ROM digest.
constexpr PresentationProfile presentation_profile(std::string_view digest) noexcept {
    return digest==metroid_digest?PresentationProfile::metroid_usa_f1f4:PresentationProfile::standard;
}
inline constexpr std::array<unsigned,2> product_mode_slots{0,3};
constexpr bool profile_mode_allowed(PresentationProfile,unsigned slot) noexcept {
    return slot==0||slot==3;
}
constexpr unsigned profile_mode(PresentationProfile p,unsigned slot) noexcept {
    return profile_mode_allowed(p,slot)?slot:0;
}
constexpr unsigned cycle_profile_mode(PresentationProfile p,unsigned slot,int step) noexcept {
    slot=profile_mode(p,slot);
    return step?(slot==0?3:0):slot;
}
Image render_profile(const Frame&,Mode,PresentationProfile,int = 512,int = 480);
}
