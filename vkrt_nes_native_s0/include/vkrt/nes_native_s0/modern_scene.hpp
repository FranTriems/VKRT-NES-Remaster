#pragma once
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include "vkrt/nes_native_s0/modern_provenance.hpp"
#include "vkrt/nes_native_s0/presentation_profile.hpp"
#include <map>

namespace vkrt::nes {
struct SmrcForm;
struct SmrcDiagnostics;
struct RetainedInstance {
    ShapeKey shape;
    std::uint64_t generation{};
    std::uint64_t last_change{};
    std::uint16_t palette_role{};
    bool valid{};
};
struct RetainedStats {
    std::uint32_t spans{}, shape_builds{}, form_builds{}, background_updates{}, sprite_updates{}, palette_updates{}, unstable_spans{};
    std::uint64_t generation{};
    // The native observer never calls observe/connected_regions/semantic_contours.
    std::uint32_t legacy_observer_calls{};
};
struct ShapeAppearance {
    std::array<float,64> pigment{};
    std::array<std::uint8_t,64> role{};
};
struct FormKey {
    std::array<ShapeKey,9> neighbors{};
    auto operator<=>(const FormKey&) const = default;
};
struct FormAppearance {
    std::array<float,64> light{}, contact{};
    std::shared_ptr<const SmrcForm> smrc;
};
struct SceneTexel {
    std::array<float,3> color{}, dx{}, dy{};
    bool locked{};
};
struct NativeStructure {
    std::uint8_t color{},effects{},oam_slot{255};
    Layer layer{};
    bool operator==(const NativeStructure&) const = default;
};
class ModernScene {
public:
    explicit ModernScene(PresentationProfile profile = PresentationProfile::standard) : profile_(profile) {}
    const RetainedStats& stats() const { return stats_; }
    const auto& backgrounds() const { return background_; }
    const auto& sprites() const { return sprites_; }
    const Frame& canonical() const;
    std::size_t shape_count() const { return shapes_.size(); }
private:
    const PresentationProfile profile_;
    ImmutableFrame frame_;
    RetainedStats stats_;
    std::array<RetainedInstance,4096> background_{};
    std::array<RetainedInstance,64> sprites_{};
    std::vector<std::array<ShapeKey,8>> background_rows_=std::vector<std::array<ShapeKey,8>>(4096);
    std::array<std::array<ShapeKey,16>,64> sprite_rows_{};
    std::array<std::uint8_t,32> palette_{};
    std::array<std::uint8_t,256> oam_{};
    std::map<ShapeKey,ShapeAppearance> shapes_;
    std::map<FormKey,FormAppearance> forms_;
    std::vector<const ShapeAppearance*> commands_;
    std::vector<const FormAppearance*> form_commands_;
    std::array<const FormAppearance*,4096> background_forms_{};
    std::vector<SceneTexel> paint_;
    std::vector<float> shadow_;
    friend void observe_modern(ModernScene&, ImmutableFrame);
    friend void raster_modern_scene_into(Image&,ModernScene&,ModernControls,int,int);
    friend Image render_native_modern(const Frame&,ModernControls,int,int);
    friend void raster_metroid_smrc_into(Image&,ModernScene&,ModernControls,int,int,SmrcDiagnostics*);
    friend Image render_profile(const Frame&,Mode,PresentationProfile,int,int);
};
// The shared canonical snapshot is retained without copying Sample/Frame arrays.
void observe_modern(ModernScene&, ImmutableFrame);
void raster_modern_scene_into(Image&,ModernScene&,ModernControls = {},int = 768,int = 720);
Image render_modern_scene(ModernScene&,ModernControls = {},int = 768,int = 720);
std::vector<NativeStructure> modern_structure(const ModernScene&,int = 256,int = 240);
// Convenience native route; explicit scene ownership is preferred for hot loops.
Image render_native_modern(const Frame&,ModernControls,int,int);
}
