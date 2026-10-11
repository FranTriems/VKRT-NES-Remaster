#pragma once
#include "vkrt/nes_native_s0/modern_scene.hpp"

namespace vkrt::nes {
enum class ReliefFamily : std::uint8_t { flat, bevel, round, porous };
struct ReliefTexel {
    float height{},nx{},ny{},nz{1},ao{};
    ReliefFamily family{};
};
struct SmrcForm { std::array<ReliefTexel,64> texels; };
// Complete exact content + bounded 3x3 seam context. Palette, frame, CHR epoch,
// screen position and OAM slot cannot change compiled geometry.
SmrcForm compile_smrc_form(const FormKey&);
struct SmrcLight {
    float diffuse{1},specular{},ao{},contact{},emission{};
};
std::array<float,3> smrc_response(std::array<std::uint8_t,3>,SmrcLight);
enum class SmrcView { height,normal,ao,contact,light,emissive,material,owner,identity,churn,coverage,delta,albedo };
struct SmrcTexel {
    ReliefTexel relief;
    SmrcLight light;
    std::uint64_t identity{};
    std::uint32_t owner{};
    bool protected_flat{},changed{},fallback{};
};
struct SmrcDiagnostics {
    std::vector<SmrcTexel> texels;
    // Written by the actual output sampler, not inferred from output RGB.
    std::vector<std::uint32_t> source_cells;
    std::vector<NativeStructure> owners;
    std::uint64_t coverage_xor_pixels{},owner_mismatch_pixels{};
};
void raster_metroid_smrc_into(Image&,ModernScene&,ModernControls,int,int,SmrcDiagnostics* = nullptr);
Image render_smrc_diagnostic(const Frame&,const Image& f1,const Image& f4,const SmrcDiagnostics&,SmrcView);
}
