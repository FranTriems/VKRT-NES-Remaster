#pragma once
// VKRT public-experimental: owned observational values; no machine pointers.
#include "vkrt/nes_native_s0/hash.hpp"
#include <array>
#include <memory>
#include <vector>
namespace vkrt::nes {
struct PpuProvenance;
inline constexpr int width = 256, height = 240, pixels = width * height;
enum class Layer : std::uint8_t { backdrop, background, sprite };
enum class Reactive : std::uint8_t { stable, disoccluded, appearance, ambiguous, history_gap };
struct Source {
    std::uint16_t nametable{}, attribute{}, pattern{}, scroll_v{};
    std::uint64_t chr_source{}, chr_epoch{}, high_chr_epoch{}, fetch_epoch{}, high_fetch_epoch{};
    std::uint8_t tile{}, palette{}, local_x{}, local_y{}, flags{}, oam_slot{255}, fine_x{};
    std::int16_t origin_x{}, origin_y{};
    bool operator==(const Source&) const = default;
};
struct Sample {
    std::uint16_t x{}, y{}, dot{};
    std::uint8_t color{}, emphasis{};
    bool grayscale{}, bg_opaque{}, sprite_opaque{}, sprite_zero{}, sprite_behind{};
    Layer layer{Layer::backdrop};
    Source background{}, sprite{}; // Both candidates survive occlusion.
    std::uint64_t asset_key{}, surface_key{}, previous_surface{};
    std::int16_t motion_x{}, motion_y{}; // Current -> previous, source pixels.
    std::uint8_t confidence{};
    Reactive reactive{Reactive::disoccluded};
};
struct Frame {
    std::uint64_t number{};
    std::uint16_t mapper_domain{};
    std::vector<std::uint8_t> indices = std::vector<std::uint8_t>(pixels);
    std::vector<std::uint8_t> effects = std::vector<std::uint8_t>(pixels);
    std::vector<Sample> samples = std::vector<Sample>(pixels);
    std::shared_ptr<const PpuProvenance> native;
};
using ImmutableFrame = std::shared_ptr<const Frame>;
struct Region {
    std::uint32_t id{}, area{}, perimeter{};
    std::uint16_t min_x{256}, min_y{240}, max_x{}, max_y{};
    std::uint64_t asset_key{}; // Representative first sample; spatial regions may span tiles.
    std::uint8_t color{};
    Layer layer{};
    bool protected_fine{}; // Structural evidence; the contour IR may certify local AA.
};
struct Regions { std::vector<std::uint32_t> labels; std::vector<Region> regions; };
// Frame-local spatial evidence. A topology region is neither an asset nor a
// temporal track. Relations are left/right/up/down; outside is a hard boundary.
enum class TopologyRelation : std::uint8_t {
    same_topology_region, same_surface_different_appearance,
    hard_owner_boundary, occlusion_boundary, palette_effect_boundary
};
struct SemanticTopology {
    Regions components;
    std::vector<std::array<TopologyRelation,4>> adjacency;
};
void validate_frame(const Frame&);
std::uint64_t asset_key(std::uint16_t mapper, Layer, const Source&);
std::uint64_t surface_key(std::uint64_t asset, Layer, const Source&);
ImmutableFrame observe(const Frame&, const Frame* previous = nullptr);
// Per-thread call counter for path auditing; excluded from all witness bytes.
std::uint64_t legacy_observation_calls();
Regions connected_regions(const Frame&);
SemanticTopology semantic_topology(const Frame&);
// Local spatial compatibility (cardinal or diagonal neighbors), not temporal
// identity or a global equivalence relation. Coherent background tile/local
// provenance can bridge source instances; connected_regions supplies locality.
bool same_semantic_owner(const Sample&, const Sample&);
std::string frame_hash(const Frame&);
}
