#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include "vkrt/nes_native_s0/modern_scene.hpp"
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vkrt::nes {
Image render_modern(const Frame& f,ModernControls controls,int w,int h,const ModernAssetRegistry* registry){
    for(double c:{controls.structure_intensity,controls.tone_intensity})
        if(!std::isfinite(c)||c<0||c>1)throw std::invalid_argument("modern controls require finite 0..1 intensities");
    if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("modern extent");
    // Zero/zero remains literal delegation to the frozen public renderer.
    if(controls.structure_intensity==0&&controls.tone_intensity==0)return render(f,Mode::SEMANTIC_HD,w,h);
    if(f.native&&!registry)return render_native_modern(f,controls,w,h);
    const auto prepared=prepare_modern(f,controls,registry);
    return raster_modern(prepared,w,h);
}
void write_modern_evidence(const std::filesystem::path&dir,const Frame&f){
    const auto image=render_modern(f);
    if(image!=render_modern(f))throw std::runtime_error("modern repeat mismatch");
    write_ppm(dir/"modern_remaster.ppm",image);
    write_ppm(dir/"modern_material.ppm",render_modern_diagnostic(f,ModernDiagnostic::material));
    write_ppm(dir/"modern_height.ppm",render_modern_diagnostic(f,ModernDiagnostic::height));
    write_ppm(dir/"modern_normal.ppm",render_modern_diagnostic(f,ModernDiagnostic::normal));
    write_ppm(dir/"modern_masks.ppm",render_modern_diagnostic(f,ModernDiagnostic::masks));
    write_ppm(dir/"modern_shadow.ppm",render_modern_diagnostic(f,ModernDiagnostic::shadow));
    write_ppm(dir/"modern_roughness.ppm",render_modern_diagnostic(f,ModernDiagnostic::roughness));
    write_ppm(dir/"modern_light.ppm",render_modern_diagnostic(f,ModernDiagnostic::light));
    std::ofstream receipt(dir/"modern_repeat.json",std::ios::binary);receipt.exceptions(std::ios::badbit|std::ios::failbit);
    receipt<<"{\n  \"schema\": \"VKRT_M4R2_S0\",\n  \"repeat_equal\": true,\n  \"structure_intensity\": 1,\n  \"tone_intensity\": 1,\n  \"rgb_sha256\": \""<<sha256(image.rgb)<<"\"\n}\n";
}
}
