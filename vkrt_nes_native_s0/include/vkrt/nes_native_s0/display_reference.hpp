#pragma once
#include "vkrt/nes_native_s0/modern_color.hpp"

namespace vkrt::nes {
// S0 ORIGINAL_REFERENCE display convention, not a unique electrical RGB truth.
// Reuse the accepted source anchor without changing the modern pipeline.
inline std::array<std::uint8_t,3> reference_display_color(std::uint8_t code, std::uint8_t effects) {
    return modern_source_color(code, effects);
}
}
