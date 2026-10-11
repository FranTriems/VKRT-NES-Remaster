#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace vkrt::nes {
using ModernRgb = std::array<float,3>;

// Presentation-only NES display swatches. The canonical six-bit color and
// grayscale/emphasis bits remain authoritative; no game/asset identity enters
// this decoder. These deliberately rounded display values are not a claim of
// measured console voltages. The historical digital_color stays frozen.
inline std::array<std::uint8_t,3> modern_source_color(std::uint8_t code,std::uint8_t effects) {
    constexpr std::array<std::array<std::uint8_t,3>,64> swatches{{
        {88,88,88},{0,32,120},{24,16,144},{64,8,136},
        {104,8,104},{120,8,48},{112,24,0},{88,40,0},
        {56,56,0},{16,64,0},{0,72,8},{0,64,48},
        {0,56,88},{0,0,0},{0,0,0},{0,0,0},
        {160,160,160},{16,80,192},{56,56,216},{104,40,208},
        {160,32,168},{184,32,96},{184,56,32},{160,80,0},
        {120,104,0},{64,120,0},{24,128,24},{0,120,88},
        {0,112,144},{0,0,0},{0,0,0},{0,0,0},
        {248,248,248},{72,152,248},{112,120,248},{160,104,248},
        {216,96,224},{240,96,160},{240,120,88},{224,152,48},
        {192,176,24},{136,192,32},{80,200,72},{48,192,144},
        {48,176,208},{80,80,80},{0,0,0},{0,0,0},
        {248,248,248},{176,216,248},{192,200,248},{216,192,248},
        {240,184,240},{248,184,216},{248,200,184},{248,216,168},
        {232,232,152},{208,240,160},{184,240,184},{168,232,216},
        {168,224,240},{168,168,168},{0,0,0},{0,0,0}
    }};
    if(effects&1)code&=0x30;
    auto rgb=swatches[code&63];
    // Same bounded emphasis convention as the frozen renderer, applied before
    // albedo anchoring so a source-requested tint remains part of its identity.
    if(effects&0x20){rgb[1]=rgb[1]*3/4;rgb[2]=rgb[2]*3/4;}
    if(effects&0x40){rgb[0]=rgb[0]*3/4;rgb[2]=rgb[2]*3/4;}
    if(effects&0x80){rgb[0]=rgb[0]*3/4;rgb[1]=rgb[1]*3/4;}
    return rgb;
}

// Albedo is source-anchored, with modest chroma enrichment. Subtracting a
// common pedestal and scaling all channels equally preserves hue, including
// warm near-neutrals. No independent channel clipping can rotate that hue.
inline ModernRgb modern_albedo(const std::array<std::uint8_t,3>& source,float tone) {
    const float pedestal=float(std::min({source[0],source[1],source[2]}));
    ModernRgb out;
    for(int k=0;k<3;++k)out[k]=source[k]+.06f*tone*(source[k]-pedestal);
    const float peak=std::max({out[0],out[1],out[2]});
    if(peak>255.f)for(auto& c:out)c*=255.f/peak;
    return out;
}

// Achromatic diffuse light/AO and a neutral dielectric highlight change
// material response without changing the albedo's channel ordering or hue.
// Gamut compression scales the whole ray, rather than clipping its channels.
inline ModernRgb modern_light_response(const ModernRgb& albedo,float diffuse,float highlight) {
    ModernRgb out;
    for(int k=0;k<3;++k)out[k]=albedo[k]*diffuse+highlight;
    const float peak=std::max({out[0],out[1],out[2]});
    if(peak>255.f)for(auto& c:out)c*=255.f/peak;
    return out;
}
}
