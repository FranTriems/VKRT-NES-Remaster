#pragma once
#include "vkrt/nes_native_s0/semantic.hpp"
#include <compare>

namespace vkrt::nes {
// Value-only PPU tap; this header has no renderer or machine dependency.
// Content identity excludes palette, address, timing and OAM.
struct ShapeKey {
    std::array<std::uint8_t,16> bits{};
    auto operator<=>(const ShapeKey&) const = default;
};
struct AppearanceKey {
    ShapeKey shape;
    std::uint16_t palette_role{};
    std::uint32_t context{};
    auto operator<=>(const AppearanceKey&) const = default;
};
struct NativeSpan {
    std::uint32_t begin{}, count{};
    ShapeKey shape;
    Source source;
    Layer layer{};
    bool unstable{};
};
AppearanceKey native_appearance_key(const NativeSpan&);
// Actual fetched rows and final winner, never a replay of vblank registers.
// Immutable after publication; excluded from canonical serialization.
struct PpuProvenance {
    std::vector<NativeSpan> spans;
    std::array<std::uint8_t,32> palette{};
    std::array<std::uint8_t,256> oam{};
};
struct PatternObservation {
    ShapeKey shape;
    std::uint64_t source{}, epoch{};
    bool valid{};
};
}
