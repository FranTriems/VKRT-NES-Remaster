#pragma once
// VKRT public-experimental: CANONICAL_MACHINE -> IMMUTABLE_SEMANTIC_OBSERVATION -> PRESENTATION.
#include "vkrt/nes_native_s0/semantic.hpp"
#include <array>
#include <filesystem>
#include <span>
namespace vkrt::nes {
enum class Mode { EXACT_DIGITAL, AUTHENTIC_SIGNAL, SEMANTIC_HD,
                  TEMPORAL_RECONSTRUCTION, NEURAL_RESIDUAL_EXPERIMENTAL, MODERN_REMASTER };
struct Image {
    int width{}, height{};
    Bytes rgb;
    bool operator==(const Image&) const = default;
};
struct TemporalInput { ImmutableFrame current, previous; }; // No runtime in M0.
struct NeuralResidualPolicy {
    bool enabled{false};
    bool geometry_authority{false}, silhouette_authority{false}, hud_authority{false}, low_frequency_authority{false};
};
// A connected decision needs an existing component AND a bounded local cardinal
// path; neither equal appearance nor temporal correspondence resolves a saddle.
enum class DiagonalResolution : std::uint8_t { none, ambiguous_diagonal, connected_ad, connected_bc };
// Source fallback plus disjoint triangular corner cuts. In local Q8 coordinates
// each cut replaces x+y < reach_q8 by the witnessed donor region. The retained
// central cross preserves every original four-connected edge and cell center.
struct ContourCut {
    int donor{-1};
    std::uint16_t reach_q8{};
};
struct ContourCell {
    std::array<ContourCut,4> cuts{}; // NW, NE, SW, SE
    // Exterior coverage of a protected silhouette: cardinal L/R/U/D donors.
    // A 1/32-cell strip on the middle half of an edge, disjoint from all cuts.
    std::array<int,4> edge_donors{-1,-1,-1,-1};
    std::uint8_t eligible_corners{}; // NW/NE/SW/SE bits; affected support only.
    bool locked{},material{}; // Derived exact fallback, never a receiver veto.
};
struct ContourIR {
    SemanticTopology topology;
    std::vector<ContourCell> cells;
    // Indexed by the NW cell of each 2x2. Last row/column are always none.
    std::vector<DiagonalResolution> diagonals;
};
ContourIR semantic_contours(const Frame&);
std::array<std::uint8_t,3> digital_color(std::uint8_t palette, std::uint8_t effects);
Image render(const Frame&, Mode, int output_width = 768, int output_height = 720);
// Explicit S0 reference palette for frontend F1/F3; legacy render() stays frozen.
Image render_reference(const Frame&, Mode, int out_width = 768, int out_height = 720);
// Debug-only categorical views. They are not remaster modes or machine input.
enum class Diagnostic { layer, asset, protected_structure, motion, reactive };
Image render_diagnostic(const Frame&, Diagnostic, int output_width = 1024, int output_height = 960);
void write_ppm(const std::filesystem::path&, const Image&);
void write_wav(const std::filesystem::path&, std::span<const std::int16_t>);
void write_semantics(const std::filesystem::path&, const Frame&);
}
