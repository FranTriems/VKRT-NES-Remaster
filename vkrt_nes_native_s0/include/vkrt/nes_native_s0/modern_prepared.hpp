#pragma once
#include "vkrt/nes_native_s0/modern_assets.hpp"

namespace vkrt::nes {
// Appearance coefficients, never geometry input. Linear reconstruction is
// restricted to the same region/owner; source palette edges remain discontinuous.
struct ModernPaintCell {
    std::array<float,3> albedo{}, delta{}, dx{}, dy{};
    ModernNormal form;
    float pattern{};
    bool contour{};
    bool operator==(const ModernPaintCell&) const = default;
};
// An owned snapshot, not a temporal cache or a pointer-keyed memoization. Reusing
// storage always replaces every field, including registry-derived coefficients.
class PreparedModernFrame {
public:
    const ModernPlan& plan() const { return plan_; }
    std::span<const ModernPaintCell> paint() const { return paint_; }
    std::span<const std::array<std::uint8_t,3>> colors() const { return colors_; }
private:
    ModernPlan plan_;
    std::vector<ModernPaintCell> paint_;
    std::vector<std::array<std::uint8_t,3>> colors_;
    friend void prepare_modern_into(PreparedModernFrame&, const Frame&, ModernControls, const ModernAssetRegistry*);
};
void prepare_modern_into(PreparedModernFrame&, const Frame&, ModernControls = {}, const ModernAssetRegistry* = nullptr);
PreparedModernFrame prepare_modern(const Frame&, ModernControls = {}, const ModernAssetRegistry* = nullptr);
void raster_modern_into(Image&, const PreparedModernFrame&, int output_width = 768, int output_height = 720);
Image raster_modern(const PreparedModernFrame&, int output_width = 768, int output_height = 720);
// Slow, independent raster oracle: frozen Semantic HD base plus scalar coverage
// for EVERY output sample, without the interior block/span shortcut. Exact bytes.
Image render_modern_reference(const Frame&, ModernControls = {}, int output_width = 768,
                              int output_height = 720, const ModernAssetRegistry* = nullptr);
// Pure pigment synthesis seam: hue-ray endpoints vary by material and source
// field, not a global RGB grade. Values are normalized 0..1; UI and ink are exact.
std::array<float,3> modern_illustrated_albedo(std::array<std::uint8_t,3>, ModernMaterial, double pattern);
}
