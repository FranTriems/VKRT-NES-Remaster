#pragma once
// VKRT public-experimental: bounded presentation values, no machine authority.
#include "vkrt/nes_native_s0/remaster.hpp"

namespace vkrt::nes {
enum class ModernMaterial : std::uint8_t {
    UI_FLAT, ACTOR, OBJECT_FLAT, ORGANIC, FOLIAGE, SOIL, STONE, WOOD,
    METAL, WATER, EMISSIVE, UNKNOWN_SAFE
};
struct ModernControls {
    double structure_intensity{1.0};
    double tone_intensity{1.0};
};
struct ModernNormal {
    double x{}, y{}, z{1};
    bool operator==(const ModernNormal&) const = default;
};
struct ModernLightingRig {
    ModernNormal key, fill;
    double key_gain{}, fill_gain{}, ambient_floor{};
    double cast_x{}, cast_y{}; // Screen projection AWAY from the key, unit Y.
    std::array<double,3> warm, cool, shadow_tint;
};
const ModernLightingRig& modern_lighting_rig();
struct ModernMaterialProfile {
    double roughness{}, diffuse_wrap{}, specular_budget{}, rim_budget{};
    double relief_gain{}, detail_gain{}, ao_gain{}, contact_gain{}, tone_contrast{};
};
ModernMaterialProfile modern_material_profile(ModernMaterial, double confidence = 1);
struct ModernLightResponse {
    double key{}, fill{}, diffuse{1}, specular{}, rim{};
    bool operator==(const ModernLightResponse&) const = default;
};
ModernLightResponse modern_light(ModernNormal, ModernMaterial, double confidence = 1,
                                double authored_roughness = -1);
// Full equality is required, not a hash-only or AssetKey-only authorization.
// BG origins are PPU world coordinates. Sprite placement is deliberately absent;
// OAM provenance disambiguates repeated visible instances without screen rules.
struct ModernContext {
    std::uint64_t asset_key{}, surface_key{}, motif_key{};
    std::uint64_t chr_source{}, chr_epoch{}, high_chr_epoch{};
    std::uint16_t mapper_domain{}, nametable{}, attribute{}, pattern{};
    std::int16_t world_origin_x{}, world_origin_y{};
    Layer layer{};
    std::uint8_t palette{}, flags{}, oam_slot{255};
    bool behind{}, bg_opaque{}, sprite_opaque{};
    bool operator==(const ModernContext&) const = default;
};
struct ModernCell {
    std::uint32_t region{}, owner{};
    ModernContext context;
    ModernMaterial material{ModernMaterial::UNKNOWN_SAFE};
    double confidence{}, relief{}, roughness{1}, specular{}, ao{}, contact{}, cast_shadow{};
    double structure_mask{}, tone_mask{}, emissive_confidence{}, depth{};
    double distance{}, owner_distance{}, detail{}, macro_ink{}, local_tone{};
    std::uint64_t detail_key{}, detail_motif{}; // Appearance only; never registry lookup.
    double world_x{}, world_y{};
    ModernNormal normal;
    bool protected_fine{}, ui{}, actor{}, world{};
    bool operator==(const ModernCell&) const = default;
};
struct ModernPlan {
    ContourIR contours;
    std::vector<ModernCell> cells;
};
struct ModernClassification {
    ModernMaterial material{ModernMaterial::UNKNOWN_SAFE};
    double confidence{}, roughness{1}, specular{};
    bool operator==(const ModernClassification&) const = default;
};
ModernClassification modern_classify(std::array<std::uint8_t,3>, const Region&, bool actor, bool ui);
ModernPlan modern_plan(const Frame&);
// Same immutable contour/owner/classification/support authority; omits the
// superseded regional bevel and tone kernels. Preparation supplies owner form.
ModernPlan modern_illustrated_plan(const Frame&);
// Reusable bounded geometry kernel. Every tap walks cardinal semantic adjacency;
// even a remote cell in the same component cannot bypass an intervening boundary.
std::vector<ModernNormal> modern_normals(const SemanticTopology&, std::span<const double> relief);
// Convex luminance refinement with local ordering envelopes. Every tap stops at
// hard adjacency or a strong luminance edge; protected regions are exact.
std::vector<double> modern_local_tone(const SemanticTopology&, std::span<const double>, double amount = .24);
double modern_detail(std::uint64_t asset, std::uint64_t motif, double source_x, double source_y);
// 4..7 remain invalid, preserving the M4-R2 public bounds test and ordinals.
enum class ModernDiagnostic { material, height, normal, masks, shadow = 8, roughness, light };
Image render_modern_diagnostic(const Frame&, ModernDiagnostic, int output_width = 768, int output_height = 720);
}
