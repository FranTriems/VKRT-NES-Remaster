#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vkrt::nes {
namespace {
using RGB=std::array<float,3>;
int neighbor(int i,int dx,int dy){int x=i%width+dx,y=i/width+dy;return x<0||x>=width||y<0||y>=height?-1:y*width+x;}
double wave(double x){x-=std::floor(x);return 1-4*std::abs(x-.5);}
double pigment_field(const ModernCell& c){
    // Washes and directional strokes, at 16..64 source-pixel wavelengths. No
    // high-frequency noise, screen position, frame number or temporal inputs.
    const double x=c.world_x,y=c.world_y;
    switch(c.material){
    case ModernMaterial::ACTOR:return c.detail*.25;
    case ModernMaterial::FOLIAGE:return .55*c.detail+.17*wave((x+.35*y)/38.);
    case ModernMaterial::ORGANIC:return .60*c.detail+.15*wave((x-.6*y)/48.);
    case ModernMaterial::SOIL:return .35*c.detail+.23*wave((y+.12*x)/29.);
    case ModernMaterial::STONE:return .25*c.detail+.19*wave((x+y*.7)/54.);
    case ModernMaterial::WOOD:return .30*c.detail+.24*wave((x+.08*y)/19.);
    default:return .40*c.detail;
    }
}
void controls_valid(ModernControls c){
    for(double v:{c.structure_intensity,c.tone_intensity})
        if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("modern controls require finite 0..1 intensities");
}
}

RGB modern_illustrated_albedo(std::array<std::uint8_t,3> source,ModernMaterial m,double pattern){
    if(unsigned(m)>unsigned(ModernMaterial::UNKNOWN_SAFE)||!std::isfinite(pattern)||std::abs(pattern)>.5)
        throw std::invalid_argument("illustrated pigment bounds");
    RGB src{source[0]/255.f,source[1]/255.f,source[2]/255.f};
    const float lo=*std::min_element(src.begin(),src.end()),hi=*std::max_element(src.begin(),src.end());
    if(m==ModernMaterial::UI_FLAT||hi<.055f)return src;
    // Each medium has a pigment body and paper/fill endpoint. Interpolating on
    // the ORIGINAL hue ray preserves channel ordering and hue, including grays.
    // Matte mineral, translucent organic, dry soil and dense foliage do not
    // merely differ in specular response or share one multiplicative grade.
    struct Pigment {float low_gain,low_lift,high_gain,high_lift,variation;};
    constexpr Pigment palette[]{
        {1,0,1,0,0}, {.82f,.085f,.82f,.16f,.035f},
        {.80f,.025f,.82f,.04f,.045f}, {.65f,.055f,.86f,.10f,.13f},
        {.32f,.025f,.72f,.105f,.22f}, {.78f,.012f,.76f,.055f,.13f},
        {.86f,.022f,.82f,.025f,.10f}, {.66f,.045f,.80f,.115f,.15f},
        {.85f,.025f,.92f,.04f,.035f}, {.80f,.045f,.90f,.06f,.055f},
        {.90f,.03f,.95f,.04f,0}, {.88f,.02f,.90f,.03f,.04f}
    };
    const auto p=palette[unsigned(m)];
    const float shift=float(pattern)*p.variation;
    float a=std::clamp(lo*p.low_gain+p.low_lift+shift*.55f,0.f,1.f);
    float b=std::clamp(hi*p.high_gain+p.high_lift+shift,a,1.f);
    RGB out{};
    if(hi-lo<.0001f){out.fill((a+b)*.5f);return out;}
    for(int k=0;k<3;++k)out[k]=a+(b-a)*(src[k]-lo)/(hi-lo);
    return out;
}

void prepare_modern_into(PreparedModernFrame& out,const Frame& f,ModernControls controls,const ModernAssetRegistry* registry){
    controls_valid(controls);
    // Rebuild unconditionally: same address/number/hash is NOT cache authority.
    // The Frame and registry can be destroyed or modified after this call.
    out.plan_=modern_illustrated_plan(f);
    out.colors_.resize(pixels);out.paint_.assign(pixels,{});
    auto& p=out.plan_;const auto& t=p.contours.topology;
    std::uint32_t owners=0;for(const auto& c:p.cells)owners=std::max(owners,c.owner);
    std::vector<double> radius(owners+1,.5),form(pixels);
    std::vector<unsigned> area(owners+1);
    std::vector<const RemasterAsset*> bindings(registry?pixels:0);
    for(int i=0;i<pixels;++i){const auto& c=p.cells[i];radius[c.owner]=std::max(radius[c.owner],c.owner_distance);++area[c.owner];
        out.colors_[i]=digital_color(f.indices[i],f.effects[i]);}
    for(int i=0;i<pixels;++i){const auto& c=p.cells[i];
        // One broad owner envelope. No regional bevel, tile boundary or palette
        // microdetail feeds this geometry. Giant world planes have no dome.
        if((!c.world&&!c.actor)||(c.world&&radius[c.owner]>48&&area[c.owner]>=4096))continue;
        const double u=std::clamp(c.owner_distance/std::max(2.,radius[c.owner]),0.,1.);
        form[i]=.72*u*(2-u);
    }
    const auto normals=modern_normals(t,form);
    for(int i=0;i<pixels;++i){auto& c=p.cells[i];c.normal=c.ui?ModernNormal{}:normals[i];c.relief=c.ui?0:form[i];
        if(c.ui)continue;
        double lap=0;
        for(auto [dx,dy]:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}}){
            int n=neighbor(i,dx,dy);if(n>=0&&p.cells[n].owner==c.owner)lap+=std::max(0.,form[n]-form[i]);}
        c.ao=std::min(.06,lap*.30)*modern_material_profile(c.material,c.confidence).ao_gain;
    }
    for(int i=0;i<pixels;++i){const auto& c=p.cells[i];auto& dest=out.paint_[i];
        for(const auto& cut:p.contours.cells[i].cuts)dest.contour|=cut.donor>=0;
        for(int donor:p.contours.cells[i].edge_donors)dest.contour|=donor>=0;
        dest.form=c.ui?ModernNormal{}:normals[i];
        const auto color=out.colors_[i];RGB base{float(color[0]),float(color[1]),float(color[2])};
        dest.albedo={base[0]/255,base[1]/255,base[2]/255};
        if(c.ui||(controls.structure_intensity==0&&controls.tone_intensity==0))continue;
        const double st=controls.structure_intensity*c.structure_mask,tn=controls.tone_intensity*c.tone_mask;
        if(!c.actor&&!c.world){
            // Frozen R2.1 <=2-byte ambient wash; no inferred sky or geometry.
            const auto& rig=modern_lighting_rig();
            for(int k=0;k<3;++k)dest.delta[k]=float(base[k]*tn*(-.05+.04*(rig.cool[k]-1)));
            continue;
        }
        dest.pattern=float(pigment_field(c));
        const auto pigment=modern_illustrated_albedo(color,c.material,dest.pattern);
        dest.albedo=pigment;
        auto normal=dest.form;double rough=c.roughness,emit=0,authored_height=0;
        RGB authored{1,1,1};
        if(registry){auto match=registry->lookup(c.context);
            if(match.status==ModernLookup::found){const auto& a=*match.asset;
                const auto& src=c.actor?f.samples[i].sprite:f.samples[i].background;
                const unsigned x=std::min(a.width-1,unsigned((src.local_x+.5)*a.width/8));
                const unsigned y=std::min(a.height-1,unsigned((src.local_y+.5)*a.height/8));
                const auto& tex=a.texels[y*a.width+x];const double mask=tex.appearance_mask;
                if(mask>0&&((a.albedo&&tn>0)||((a.normal||a.roughness||a.height_channel||a.emissive)&&st>0)))bindings[i]=match.asset;
                if(a.albedo)for(int k=0;k<3;++k)authored[k]=float(std::lerp(1.,tex.albedo[k],mask*tn));
                if(a.normal&&st>0){const double amount=mask*.4*st;
                    normal.x=std::lerp(normal.x,(c.actor&&(src.flags&64))?-tex.normal.x:tex.normal.x,amount);
                    normal.y=std::lerp(normal.y,(c.actor&&(src.flags&128))?-tex.normal.y:tex.normal.y,amount);
                    normal.z=std::lerp(normal.z,tex.normal.z,amount);}
                if(a.roughness)rough=std::lerp(rough,tex.roughness,mask*st);
                if(a.height_channel)authored_height=(tex.height-form[i])*mask*.035*st;
                if(a.emissive)emit=tex.emissive*mask*st;
            }
        }
        const auto light=modern_light(normal,c.material,c.confidence,rough);
        // Large matte shape plus material pigment body. AO is receiver-bounded;
        // contact attenuates only its existing lower support, never an outline.
        const double body=.94+.035*form[i]+.88*(light.diffuse-1)+authored_height;
        const double occlusion=1-st*(c.ao+std::min(.083,c.contact+c.cast_shadow)*.70);
        const double dry=1+st*dest.pattern*(c.actor?.018:.065);
        const double hi=*std::max_element(base.begin(),base.end())/255.;
        const double lift=(light.rim+light.specular)*st+emit;
        for(int k=0;k<3;++k){
            const double painted=std::lerp(double(base[k]),double(pigment[k])*255,tn)*authored[k];
            const double target=painted*std::lerp(1.,body,tn)*occlusion*dry+255*lift*std::min(1.,hi*4);
            // Preserve categorical ink/white and the frozen channel floor.
            const double bounded=std::clamp(target,base[k]*.60,std::min(255.,base[k]*1.4+8.925));
            dest.delta[k]=float(hi<.055?0:bounded-base[k]);
        }
    }
    // Native shading derivatives. No RGB-neighbor mixing, hard-boundary taps,
    // registry search or procedural evaluation is left for output resolution.
    for(int i=0;i<pixels;++i){const auto& c=p.cells[i];auto& d=out.paint_[i];if(c.ui)continue;
        auto valid=[&](int n){return n>=0&&!p.cells[n].ui&&p.cells[n].region==c.region&&p.cells[n].owner==c.owner
            &&(!registry||bindings[n]==bindings[i]);};
        int l=neighbor(i,-1,0),r=neighbor(i,1,0),u=neighbor(i,0,-1),b=neighbor(i,0,1);
        for(int k=0;k<3;++k){
            if(valid(l)&&valid(r))d.dx[k]=(out.paint_[r].delta[k]-out.paint_[l].delta[k])*.5f;
            if(valid(u)&&valid(b))d.dy[k]=(out.paint_[b].delta[k]-out.paint_[u].delta[k])*.5f;
        }
    }
}
PreparedModernFrame prepare_modern(const Frame& f,ModernControls c,const ModernAssetRegistry* r){
    PreparedModernFrame out;prepare_modern_into(out,f,c,r);return out;
}
}
