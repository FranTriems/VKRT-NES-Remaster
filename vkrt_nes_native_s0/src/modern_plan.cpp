#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vkrt::nes {
namespace {
constexpr int dx[4]={-1,1,0,0},dy[4]={0,0,-1,1};
int neighbor(int i,int x,int y){
    x+=i%width;y+=i/width;
    return x<0||x>=width||y<0||y>=height?-1:y*width+x;
}
bool soft(TopologyRelation r){
    return r==TopologyRelation::same_topology_region||r==TopologyRelation::same_surface_different_appearance;
}
std::uint64_t mix(std::uint64_t x){
    x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);
}
double noise(std::uint64_t seed,double x,double y,double scale){
    x/=scale;y/=scale;
    const auto ix=static_cast<std::int64_t>(std::floor(x)),iy=static_cast<std::int64_t>(std::floor(y));
    double fx=x-std::floor(x),fy=y-std::floor(y);fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    auto tap=[&](int a,int b){return double(mix(seed^mix(std::uint64_t(ix+a))^mix(std::uint64_t(iy+b)+0x614bULL))&65535)/65535-.5;};
    return std::lerp(std::lerp(tap(0,0),tap(1,0),fx),std::lerp(tap(0,1),tap(1,1),fx),fy);
}
// Intrinsic octile distance to the owned contour. Diagonal steps require BOTH
// cardinal paths, so neither a hole nor a corner contact admits a geometry tap.
std::vector<double> distance_field(const SemanticTopology&t,const std::vector<std::uint32_t>& labels,
                                  const std::vector<bool>* flat_owners=nullptr){
    std::vector<double> d(pixels,512);
    using Item=std::pair<double,int>;
    // Exact monotone buckets: each edge costs 1 or sqrt(2), so relaxing a
    // distance in [k,k+1) can only reach buckets k+1 and k+2. All predecessors
    // have finished before a bucket runs; no heap ordering within it is needed.
    // Distances and guarded cardinal/diagonal paths are unchanged.
    std::array<std::vector<Item>,3> buckets;
    for(auto& b:buckets)b.reserve(pixels/3);
    std::size_t pending=0;
    auto push=[&](double v,int i){buckets[unsigned(v)%3].emplace_back(v,i);++pending;};
    auto allowed=[&](int i,int e){int n=neighbor(i,dx[e],dy[e]);return n>=0&&labels[n]==labels[i]&&soft(t.adjacency[i][e]);};
    for(int i=0;i<pixels;++i){
        if(flat_owners&&(*flat_owners)[labels[i]]){d[i]=0;continue;}
        for(int e=0;e<4;++e)if(!allowed(i,e)){d[i]=.5;push(.5,i);break;}
    }
    for(unsigned band=0;pending;++band){auto& bucket=buckets[band%3];
      for(const auto [v,i]:bucket){--pending;if(v!=d[i])continue;
        for(int e=0;e<4;++e)if(allowed(i,e)){
            int n=neighbor(i,dx[e],dy[e]);double next=v+1;
            if(next<d[n]){d[n]=next;push(next,n);}
        }
        for(int ex=0;ex<2;++ex)for(int ey=2;ey<4;++ey){
            int a=neighbor(i,dx[ex],0),b=neighbor(i,0,dy[ey]);
            if(!allowed(i,ex)||!allowed(i,ey)||!allowed(a,ey)||!allowed(b,ex))continue;
            int n=neighbor(a,0,dy[ey]);double next=v+1.4142135623730951;
            if(next<d[n]){d[n]=next;push(next,n);}
        }
      }
      bucket.clear();
    }
    return d;
}
ModernContext context(const Frame&f,const Sample&s){
    const auto&v=s.layer==Layer::sprite?s.sprite:s.background;
    ModernContext c;c.asset_key=s.asset_key;c.surface_key=s.surface_key;c.mapper_domain=f.mapper_domain;
    c.layer=s.layer;c.chr_source=v.chr_source;c.chr_epoch=v.chr_epoch;c.high_chr_epoch=v.high_chr_epoch;
    c.pattern=v.pattern;c.palette=v.palette;c.flags=v.flags;c.behind=s.sprite_behind;
    c.bg_opaque=s.bg_opaque;c.sprite_opaque=s.sprite_opaque;
    if(s.layer==Layer::background){c.nametable=v.nametable;c.attribute=v.attribute;c.world_origin_x=v.origin_x;c.world_origin_y=v.origin_y;}
    if(s.layer==Layer::sprite)c.oam_slot=v.oam_slot;
    // A source-context motif, independent of screen placement, frame number,
    // region numbering, RGB micro-noise and temporal correspondence confidence.
    c.motif_key=mix(v.chr_source^mix(v.chr_epoch)^mix(v.high_chr_epoch)^std::uint64_t(v.palette)
        ^(std::uint64_t(v.flags)<<8)^(std::uint64_t(v.pattern&0x1000)<<16)
        ^(std::uint64_t(s.layer)<<40)^(std::uint64_t(s.sprite_behind)<<44));
    return c;
}
double dome(double d,double radius){double u=std::clamp(d/radius,0.,1.);return std::sqrt(u*(2-u));}
double form(double d,double radius){double u=std::clamp(d/radius,0.,1.);return u*(2-u);}
double dot(ModernNormal a,ModernNormal b){return a.x*b.x+a.y*b.y+a.z*b.z;}
ModernNormal normalized(ModernNormal n){double s=1/std::sqrt(dot(n,n));return {n.x*s,n.y*s,n.z*s};}
}

const ModernLightingRig& modern_lighting_rig(){
    static const ModernLightingRig rig=[] {
        const auto key=normalized({-.52,-.66,.82});
        return ModernLightingRig{key,normalized({.62,.24,.92}),1.22,.18,.76,
            -key.x/std::abs(key.y),-key.y/std::abs(key.y),
            {1.018,1.004,.978},{.981,.997,1.018},{.90,.79,.64}};
    }();
    return rig;
}
ModernMaterialProfile modern_material_profile(ModernMaterial m,double confidence){
    if(unsigned(m)>unsigned(ModernMaterial::UNKNOWN_SAFE)||!std::isfinite(confidence)||confidence<0||confidence>1)
        throw std::invalid_argument("modern material profile bounds");
    // Roughness, wrap, specular, rim, relief, detail, AO, contact, contrast.
    // A material hypothesis never grants a special shiny/emissive response.
    if((m==ModernMaterial::METAL||m==ModernMaterial::WATER||m==ModernMaterial::EMISSIVE)&&confidence<.9)
        m=ModernMaterial::OBJECT_FLAT;
    constexpr ModernMaterialProfile profiles[]{
        {1,   .50,0,    0,    0,   0,    0,   0,   0},
        {.96, .32,.002, .007, .90, .012, .25, .70, 1.14},
        {1,   .50,0,    0,    .16, .012, .15, .60, .30},
        {.94, .35,.006, .010, .76, .060, .35, .70, 1.02},
        {.97, .40,.004, .006, .74, .065, .35, .65, 1.02},
        {1,   .40,0,    .004, .66, .055, .30, .70, .95},
        {.97, .30,.003, .006, .76, .045, .32, .70, 1.04},
        {.94, .30,.004, .006, .66, .040, .28, .70, .90},
        {.45, .08,.120, .025, .70, .020, .35, .70, 1.00},
        {.58, .20,.060, .015, .45, .020, .18, .70, .70},
        {1,   .40,0,    0,    .10, 0,    0,   .50, .20},
        {1,   .50,0,    0,    .16, .008, .10, .60, .25}
    };
    return profiles[unsigned(m)];
}
ModernLightResponse modern_light(ModernNormal n,ModernMaterial material,double confidence,double authored_roughness){
    if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z)||dot(n,n)<.01
       ||!std::isfinite(authored_roughness)||(authored_roughness!=-1&&(authored_roughness<.35||authored_roughness>1)))
        throw std::invalid_argument("modern lighting bounds");
    n=normalized(n);const auto&rig=modern_lighting_rig();auto p=modern_material_profile(material,confidence);
    const double rough=authored_roughness<0?p.roughness:authored_roughness;
    ModernLightResponse out;out.key=dot(n,rig.key);out.fill=dot(n,rig.fill);
    auto diffuse=[&](double light,double view){
        // Cheap rough diffuse: wrapped cosine plus Oren-Nayar-like grazing
        // compensation. Both lights use the SAME kernel and directions.
        const double wrapped=std::clamp((light+p.diffuse_wrap)/(1+p.diffuse_wrap),0.,1.);
        return wrapped*(1-.18*rough)+.10*rough*(1-view)*(1-wrapped);
    };
    const double view=std::clamp(n.z,0.,1.);
    const double key_delta=diffuse(out.key,view)-diffuse(rig.key.z,1);
    const double fill_delta=diffuse(out.fill,view)-diffuse(rig.fill.z,1);
    out.diffuse=std::clamp(1+p.tone_contrast*(rig.key_gain*key_delta+rig.fill_gain*fill_delta),rig.ambient_floor,1.30);
    auto half=normalized({rig.key.x,rig.key.y,rig.key.z+1});
    out.specular=p.specular_budget*std::pow(std::max(0.,dot(n,half)),6+24*(1-rough))*(1-.65*rough);
    out.rim=p.rim_budget*(1-view)*(1-view);
    return out;
}
double modern_detail(std::uint64_t asset,std::uint64_t motif,double x,double y){
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>65536||std::abs(y)>65536)
        throw std::invalid_argument("modern source coordinate bound");
    // Broad continuous washes dominate. Asset-level variation is only 5%;
    // adjacent tiles in a coherent motif no longer restart a grain/bevel stamp.
    // This appearance field never supplies relief or normals.
    return .65*noise(mix(motif),x,y,23.)+.30*noise(mix(motif^0x931ULL),x,y,53.)
        +.05*noise(mix(asset^motif),x,y,11.);
}
ModernClassification modern_classify(std::array<std::uint8_t,3> c,const Region&r,bool actor,bool ui){
    using M=ModernMaterial;
    if(ui)return {M::UI_FLAT,1,1,0};
    if(actor)return {M::ACTOR,1,.96,.002};
    if(r.layer==Layer::backdrop||!r.area)return {M::UNKNOWN_SAFE,0,1,0};
    double red=c[0]/255.,green=c[1]/255.,blue=c[2]/255.;
    const double hi=std::max({red,green,blue}),lo=std::min({red,green,blue});
    const double l=.2126*red+.7152*green+.0722*blue,chroma=hi-lo;
    const double edge=double(r.perimeter)/r.area;
    const double aspect=double(r.max_x-r.min_x+1)/(r.max_y-r.min_y+1);
    if(l<.055)return {M::OBJECT_FLAT,.92,1,0};
    if(green>red*1.12&&green>blue*1.06&&r.area>12)
        return {chroma>.18?M::FOLIAGE:M::ORGANIC,.78,.97,.004};
    if(blue>red*1.18&&blue>green*1.08&&l>.18){
        return {M::OBJECT_FLAT,.6,1,0}; // Color/aspect cannot authorize water.
    }
    if(red>blue*1.32&&green>blue*1.12){
        if(aspect<.65&&edge<.8)return {M::WOOD,.67,.94,.004};
        return {l<.49?M::SOIL:M::ORGANIC,.7,1,0};
    }
    if(chroma<.09&&l>.22)return {M::STONE,.72,.97,.003};
    // No color-only metal or emissive inference: authored confidence is required.
    return {M::UNKNOWN_SAFE,.25,1,0};
}
std::vector<double> modern_local_tone(const SemanticTopology&t,std::span<const double> luminance,double amount){
    if(luminance.size()!=pixels||t.adjacency.size()!=pixels||t.components.labels.size()!=pixels
       ||!std::isfinite(amount)||amount<0||amount>1)throw std::invalid_argument("modern local tone bounds");
    for(double v:luminance)if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("modern luminance bound");
    std::vector<double> out(luminance.begin(),luminance.end());
    for(int i=0;i<pixels;++i){
        auto label=t.components.labels[i];
        if(!label||label>t.components.regions.size())throw std::invalid_argument("modern tone region bound");
        if(t.components.regions[label-1].protected_fine)continue;
        const double center=luminance[i];double lo=0,hi=1,sum=0,weight=0;bool edge=false;
        for(int e=0;e<4;++e){int n=neighbor(i,dx[e],dy[e]);
            if(n<0||!soft(t.adjacency[i][e])||std::abs(luminance[n]-center)>=.08){edge=true;break;}
            // Adjacent outputs cannot exchange order, even if their windows
            // differ: both are bounded by the same ORIGINAL midpoint.
            double mid=(center+luminance[n])*.5;
            if(luminance[n]<=center)lo=std::max(lo,mid);
            if(luminance[n]>=center)hi=std::min(hi,mid);
        }
        if(edge)continue; // Exact strong silhouettes, occlusion/effect seams.
        for(int e=0;e<4;++e){int n=i;
            for(int radius=1;radius<=4;++radius){int next=neighbor(n,dx[e],dy[e]);
                if(next<0||!soft(t.adjacency[n][e])||std::abs(luminance[next]-center)>=.08)break;
                n=next;double w=radius==1?2.:1.;sum+=(luminance[n]-center)*w;weight+=w;
            }
        }
        if(weight)out[i]=std::clamp(center+std::clamp(amount*sum/weight,-.012,.012),lo,hi);
    }
    return out;
}
std::vector<ModernNormal> modern_normals(const SemanticTopology&t,std::span<const double> h){
    if(h.size()!=pixels||t.adjacency.size()!=pixels)throw std::invalid_argument("modern normal extent");
    for(double v:h)if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("modern relief bound");
    std::vector<ModernNormal> out(pixels);
    for(int i=0;i<pixels;++i){
        std::array<int,4> taps{i,i,i,i};
        double gx=0,gy=0;
        for(int r=1;r<=4;++r){
            for(int e=0;e<4;++e){int n=taps[e];if(!soft(t.adjacency[n][e]))continue;
                int next=neighbor(n,dx[e],dy[e]);if(next>=0)taps[e]=next;}
            gx+=(h[taps[0]]-h[taps[1]])/(2*r);gy+=(h[taps[2]]-h[taps[3]])/(2*r);
        }
        gx*=3.;gy*=3.;double inv=1/std::sqrt(gx*gx+gy*gy+1);
        out[i]={gx*inv,gy*inv,inv};
    }
    return out;
}
static ModernPlan build_plan(const Frame&f,bool illustrated){
    ModernPlan p;p.contours=semantic_contours(f);p.cells.resize(pixels);
    const auto&t=p.contours.topology;const auto&rs=t.components;
    std::vector<std::uint32_t> owners(pixels);std::vector<int> queue;
    std::uint32_t next=0;
    // Geometry domains are only the connected closure of existing soft edges.
    // This labels support for appearance; it never merges categorical regions.
    for(int start=0;start<pixels;++start)if(!owners[start]){
        ++next;queue.clear();queue.push_back(start);owners[start]=next;
        for(std::size_t j=0;j<queue.size();++j){int i=queue[j];
            for(int e=0;e<4;++e){int n=neighbor(i,dx[e],dy[e]);
                if(n>=0&&!owners[n]&&soft(t.adjacency[i][e])){owners[n]=next;queue.push_back(n);}
            }
        }
    }
    const auto region_distance=illustrated?std::vector<double>(pixels):distance_field(t,rs.labels);
    std::vector<bool> flat_owners(next+1);
    if(illustrated)for(int i=0;i<pixels;++i)if(f.samples[i].layer==Layer::backdrop)flat_owners[owners[i]]=true;
    const auto owner_distance=distance_field(t,owners,illustrated?&flat_owners:nullptr);
    std::vector<double> owner_radius(next+1,.5),relief(pixels),luminance(pixels);
    std::vector<unsigned> owner_area(next+1);
    std::vector<int> owner_bottom(next+1,-1),owner_top(next+1,height);
    for(int i=0;i<pixels;++i){auto owner=owners[i];
        owner_radius[owner]=std::max(owner_radius[owner],owner_distance[i]);++owner_area[owner];
        owner_bottom[owner]=std::max(owner_bottom[owner],i/width);owner_top[owner]=std::min(owner_top[owner],i/width);
        if(!illustrated){auto rgb=digital_color(f.indices[i],f.effects[i]);luminance[i]=(.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2])/255.;}
    }
    const auto tone=illustrated?std::vector<double>(pixels):modern_local_tone(t,luminance);
    std::vector<ModernClassification> classes(rs.regions.size());
    std::vector<bool> classified(rs.regions.size());
    for(int i=0;i<pixels;++i){auto&c=p.cells[i];const auto&s=f.samples[i];const auto&r=rs.regions[rs.labels[i]-1];
        c.region=rs.labels[i];c.owner=owners[i];c.context=context(f,s);
        c.actor=s.layer==Layer::sprite;c.world=s.layer==Layer::background;
        c.protected_fine=r.protected_fine;
        c.ui=r.protected_fine&&c.world; // Geometric protection, never a HUD band.
        if(!classified[c.region-1]){classes[c.region-1]=modern_classify(digital_color(f.indices[i],f.effects[i]),r,c.actor,c.ui);classified[c.region-1]=true;}
        const auto& cls=classes[c.region-1];
        c.material=cls.material;c.confidence=cls.confidence;
        auto profile=modern_material_profile(c.material,c.confidence);
        c.roughness=profile.roughness;c.specular=profile.specular_budget;
        c.local_tone=tone[i]-luminance[i];
        c.distance=region_distance[i];c.owner_distance=owner_distance[i];
        const auto&src=c.actor?s.sprite:s.background;
        c.world_x=(c.actor?0:src.origin_x)+src.local_x+.5;
        c.world_y=(c.actor?0:src.origin_y)+src.local_y+.5;
        c.detail_key=s.asset_key;c.detail_motif=c.context.motif_key;
        if(c.actor){auto unflipped=s;unflipped.sprite.flags&=63;
            c.detail_key=asset_key(f.mapper_domain,Layer::sprite,unflipped.sprite);
            c.detail_motif=context(f,unflipped).motif_key;
        }
        c.detail=s.layer==Layer::backdrop?0:modern_detail(c.detail_key,c.detail_motif,c.world_x,c.world_y);
        c.depth=s.layer==Layer::sprite?(s.sprite_behind?.15:.82):s.layer==Layer::background?.40:0;
        bool unknown=c.material==ModernMaterial::UNKNOWN_SAFE;
        c.structure_mask=c.ui?0:c.actor?(c.protected_fine?.18:.90):unknown?.32:1.;
        c.tone_mask=c.ui?0:c.actor?(c.protected_fine?(illustrated?.68:.34):.96):unknown?.4:1.;
        if(s.layer==Layer::backdrop){c.structure_mask=0;c.tone_mask=.12;c.detail=0;}
        if(illustrated)continue;
        double radius=std::clamp(2.*r.area/std::max(1u,r.perimeter),2.,20.);
        double owner_r=std::clamp(owner_radius[c.owner],2.,24.);
        double large=form(c.owner_distance,owner_r);
        double regional=c.protected_fine?0:dome(c.distance,radius);
        if(c.actor){
            // Owner form survives tiny palette islands exactly. Internal form
            // requires a substantial, non-fine semantic region; a uniform fill
            // or checker cannot create embossed color normals.
            bool internal=!c.protected_fine&&r.area>=24&&r.area<owner_area[c.owner]*.85
                &&double(r.perimeter)<r.area*.75;
            regional=internal?form(std::max(0.,c.distance-1.),std::max(3.,radius)):0;
            relief[i]=profile.relief_gain*(.62*large+.16*form(c.owner_distance,owner_r*1.8)+.16*regional);
        }else{
            const bool giant_owner=owner_radius[c.owner]>48. && owner_area[c.owner]>=4096;
            const bool tile_scale_region=radius<=6.;
            const bool plane_like=giant_owner&&tile_scale_region;
            if(!plane_like){
                // Literal qualified M4-R2 geometry for bounded/significant regions,
                // even when a soft owner also spans a much larger world domain.
                const double legacy_large=dome(c.owner_distance,std::clamp(owner_radius[c.owner],2.,20.));
                double legacy_regional=c.protected_fine?0:dome(c.distance,radius);
                if(c.material==ModernMaterial::STONE||c.material==ModernMaterial::WOOD)
                    legacy_regional=dome(c.distance,std::min(radius,3.5));
                relief[i]=c.ui||!c.world?0:std::clamp(.08*legacy_large+.58*legacy_regional,0.,1.);
            }else{
                // Giant owner + tile-scale semantic regions describe a plane-like
                // tiled world. No owner dome; local relief vanishes in deep interior.
                const double plane_regional=form(c.distance,std::max(4.,radius));
                const double regional_support=std::clamp((16.-c.owner_distance)/8.,0.,1.);
                const double local_material_shape=.60*profile.relief_gain*plane_regional*regional_support;
                relief[i]=c.ui||!c.world?0:std::clamp(local_material_shape,0.,1.);
            }
        }
        c.macro_ink=c.ui||c.protected_fine||c.actor?0:std::clamp((1.5-c.distance)/1.5,0.,1.)*.018;
    }
    // Symmetric owner-constrained low pass removes distance-lattice faceting.
    // Four Jacobi iterations are deterministic and cannot jump an occluder.
    if(!illustrated){
    for(int pass=0;pass<4;++pass){auto smooth=relief;
        for(int i=0;i<pixels;++i){if(p.cells[i].ui)continue;double sum=relief[i]*4,weight=4;
            for(int e=0;e<4;++e){int n=neighbor(i,dx[e],dy[e]);
                if(n>=0&&soft(t.adjacency[i][e])&&!p.cells[n].ui){sum+=relief[n];++weight;}}
            smooth[i]=sum/weight;
        }relief.swap(smooth);
    }
    auto normals=modern_normals(t,relief);
    for(int i=0;i<pixels;++i){auto&c=p.cells[i];c.relief=relief[i];c.normal=c.ui?ModernNormal{}:normals[i];
        if(c.ui)continue;
        double lap=0;
        for(int e=0;e<4;++e){int n=neighbor(i,dx[e],dy[e]);if(n<0)continue;
            if(soft(t.adjacency[i][e]))lap+=std::max(0.,relief[n]-relief[i]);
        }
        c.ao=std::min(.06,lap*.30)*modern_material_profile(c.material,c.confidence).ao_gain;
    }
    }
    // Grounding starts at a LOWER support contour with a visible world receiver.
    // No silhouette dilation, side ring, airborne backdrop or behind-world cast.
    // The entire short penumbra walks the receiver's soft adjacency, so it can
    // neither leap across another owner nor tunnel through an occluder.
    const auto&rig=modern_lighting_rig();
    for(int i=0;i<pixels;++i){const auto&a=p.cells[i];if(!a.actor||owner_area[a.owner]<8)continue;
        int support_band=std::max(1,(owner_bottom[a.owner]-owner_top[a.owner]+1)/6);
        if(i/width<owner_bottom[a.owner]-support_band)continue;
        int root=neighbor(i,0,1);if(root<0)continue;auto&r=p.cells[root];
        if(!r.world||r.ui||r.depth>=a.depth||r.owner==a.owner)continue;
        // Require a real vertical interior immediately above the support point.
        int above=neighbor(i,0,-1);if(above<0||p.cells[above].owner!=a.owner)continue;
        r.contact=std::max(r.contact,.035);
        for(int d=1;d<=3;++d)for(int pen=-1;pen<=1;++pen){
            int xx=int(std::lround(d*rig.cast_x))+pen,yy=int(std::lround(d*rig.cast_y));
            if(xx<0||xx>3)continue;
            int n=root;bool valid=true;
            auto step=[&](int e){int next_cell=neighbor(n,dx[e],dy[e]);
                if(next_cell<0||!soft(t.adjacency[n][e])||p.cells[next_cell].owner!=r.owner||p.cells[next_cell].ui)valid=false;
                else n=next_cell;
            };
            for(int k=1;k<yy&&valid;++k)step(3);
            for(int k=0;k<xx&&valid;++k)step(1);
            if(valid){const double falloff[3]={.048,.027,.010};
                p.cells[n].cast_shadow=std::max(p.cells[n].cast_shadow,falloff[d-1]*(pen==0?1.:.28));}
        }
    }
    return p;
}
ModernPlan modern_plan(const Frame& f){return build_plan(f,false);}
ModernPlan modern_illustrated_plan(const Frame& f){return build_plan(f,true);}

Image render_modern_diagnostic(const Frame&f,ModernDiagnostic diagnostic,int w,int h){
    const auto value=unsigned(diagnostic);
    if(w<256||h<240||w>2048||h>1920||(value>3&&(value<8||value>10)))
        throw std::invalid_argument("modern diagnostic bounds");
    const auto prepared=prepare_modern(f);const auto& p=prepared.plan();Image out{w,h,Bytes(std::size_t(w)*h*3)};
    constexpr std::array<std::array<int,3>,12> palette{{{245,245,245},{242,95,68},{112,132,158},
        {180,174,92},{60,168,80},{155,104,67},{145,143,155},{172,122,74},{166,208,227},
        {63,119,228},{255,221,99},{83,77,99}}};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){int i=int(std::int64_t(2*y+1)*height/(2*h))*width+int(std::int64_t(2*x+1)*width/(2*w));
        const auto&c=p.cells[i];std::array<double,3> rgb{};
        switch(diagnostic){
        case ModernDiagnostic::material:for(int k=0;k<3;++k)rgb[k]=palette[unsigned(c.material)][k];break;
        case ModernDiagnostic::height:rgb.fill(c.relief*255);break;
        case ModernDiagnostic::normal:rgb={(c.normal.x*.5+.5)*255,(c.normal.y*.5+.5)*255,(c.normal.z*.5+.5)*255};break;
        case ModernDiagnostic::masks:rgb={c.ui||c.protected_fine?255.:0.,c.actor?255.:c.structure_mask*128,c.world?255.:c.tone_mask*128};break;
        case ModernDiagnostic::shadow:rgb={c.contact/.035*255,c.cast_shadow/.048*255,(c.contact+c.cast_shadow)/.083*255};break;
        case ModernDiagnostic::roughness:rgb.fill(c.roughness*255);break;
        case ModernDiagnostic::light:{auto light=modern_light(c.normal,c.material,c.confidence);
            rgb.fill(std::clamp(light.diffuse*.70,0.,1.)*255);break;}
        }
        for(int k=0;k<3;++k)out.rgb[(std::size_t(y)*w+x)*3+k]=std::uint8_t(std::clamp(std::lround(rgb[k]),0L,255L));
    }
    return out;
}
}
