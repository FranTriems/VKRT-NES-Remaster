#pragma once
#include "vkrt/nes_native_s0/modern_plan.hpp"
#include <optional>

namespace vkrt::nes {
struct RemasterTexel {
    std::array<double,3> albedo{1,1,1}; // bounded multiplier of source palette
    ModernNormal normal;
    double roughness{1}, height{}, emissive{}, appearance_mask{1};
};
struct RemasterAsset {
    ModernContext context;
    unsigned width{1}, height{1}; // source-local tile UV, up to 64x64
    bool albedo{}, normal{}, roughness{}, height_channel{}, emissive{};
    std::vector<RemasterTexel> texels{RemasterTexel{}};
    // appearance_mask controls replacement influence only. There is no alpha,
    // displacement, visibility, ownership, priority or silhouette write channel.
};
enum class ModernLookup { missing, found, ambiguous };
struct ModernAssetMatch { ModernLookup status{}; const RemasterAsset* asset{}; };
class ModernAssetRegistry {
public:
    void add(RemasterAsset);
    ModernAssetMatch lookup(const ModernContext&) const;
private:
    std::vector<RemasterAsset> assets_;
};
Image render_modern(const Frame&, ModernControls = {}, int output_width = 768,
                    int output_height = 720, const ModernAssetRegistry* = nullptr);
// Adds only M4-R2 files; callers retain the historical evidence transaction.
void write_modern_evidence(const std::filesystem::path&, const Frame&);
}
