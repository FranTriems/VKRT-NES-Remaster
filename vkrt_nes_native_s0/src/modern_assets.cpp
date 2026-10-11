#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vkrt::nes {
void ModernAssetRegistry::add(RemasterAsset a) {
    auto bounded=[](double v,double low,double high){return std::isfinite(v)&&v>=low&&v<=high;};
    if(assets_.size()>=256||!a.width||!a.height||a.width>64||a.height>64
       ||a.texels.size()!=std::size_t(a.width)*a.height)
        throw std::invalid_argument("modern asset bounds");
    for(const auto&t:a.texels){
        for(double c:t.albedo)if(!bounded(c,.65,1.35))throw std::invalid_argument("modern albedo palette bound");
        if(!bounded(t.normal.x,-1,1)||!bounded(t.normal.y,-1,1)||!bounded(t.normal.z,.25,1)
           ||!bounded(t.roughness,.35,1)||!bounded(t.height,0,1)
           ||!bounded(t.emissive,0,.08)||!bounded(t.appearance_mask,0,1))
            throw std::invalid_argument("modern asset channel bound");
    }
    assets_.push_back(std::move(a));
}
ModernAssetMatch ModernAssetRegistry::lookup(const ModernContext& context) const {
    const RemasterAsset* found=nullptr;
    for(const auto&asset:assets_)if(asset.context==context){
        if(found)return {ModernLookup::ambiguous,nullptr};
        found=&asset;
    }
    return {found?ModernLookup::found:ModernLookup::missing,found};
}
}
