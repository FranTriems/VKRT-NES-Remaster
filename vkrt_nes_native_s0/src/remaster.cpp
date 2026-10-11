#include "vkrt/nes_native_s0/display_reference.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
namespace vkrt::nes {
namespace {
std::uint8_t byte(int v){return static_cast<std::uint8_t>(std::clamp(v,0,255));}
std::ofstream output(const std::filesystem::path&p){std::ofstream f(p,std::ios::binary);f.exceptions(std::ios::badbit|std::ios::failbit);return f;}
}
std::array<std::uint8_t,3> digital_color(std::uint8_t palette,std::uint8_t effects){
    // First-party fixed integer display palette, not measured console voltages.
    constexpr int cosine[12]={256,222,128,0,-128,-222,-256,-222,-128,0,128,222};
    constexpr int sine[12]={0,128,222,256,222,128,0,-128,-222,-256,-222,-128};
    constexpr int levels[4]={56,104,168,220};
    if(effects&1)palette&=0x30;
    unsigned hue=palette&15,level=(palette>>4)&3;int y=levels[level],r=y,g=y,b=y;
    if(hue==13){r=g=b=level<2?0:levels[level-2];}
    else if(hue>=14){r=g=b=0;}
    else if(hue){int c=cosine[(hue+7)%12],s=sine[(hue+7)%12],sat=level==3?32:64;
        r=y+c*sat/256;g=y-(c*85+s*147)*sat/65536;b=y+s*sat/256;
    }
    if(effects&0x20){g=g*3/4;b=b*3/4;}
    if(effects&0x40){r=r*3/4;b=b*3/4;}
    if(effects&0x80){r=r*3/4;g=g*3/4;}
    return {byte(r),byte(g),byte(b)};
}
namespace {
using Color = std::array<std::uint8_t,3>;
using Signal = std::array<int,3>;
constexpr std::array<int,4> corner_x{-1,1,-1,1},corner_y{-1,-1,1,1};
int neighbor(int i,int x,int y){
    x+=i%width;y+=i/width;
    return x<0||x>=width||y<0||y>=height?-1:y*width+x;
}
void extent(int w,int h){
    if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("presentation extent outside bounded 1x..8x envelope");
}
// Geometry is built once, independent of output size. No color-distance, motion,
// history, hash-only identity, or machine interface participates in construction.
}
ContourIR semantic_contours(const Frame&f){
    ContourIR out;out.topology=semantic_topology(f);out.cells.resize(pixels);
    out.diagonals.resize(pixels,DiagonalResolution::none);
    const auto&regions=out.topology.components;const auto&labels=regions.labels;
    std::vector<bool> core(regions.regions.size());
    std::vector<int> first(regions.regions.size(),-1);
    std::vector<bool> material_domain(regions.regions.size(),true);
    std::vector<bool> material_guard(pixels,true);
    std::vector<bool> material_locked(pixels);
    for(int i=0;i<pixels;++i){
        auto region=labels[i]-1;
        if(first[region]<0)first[region]=i;
        const auto&a=f.samples[first[region]];const auto&b=f.samples[i];
        bool same=a.asset_key==b.asset_key&&a.surface_key==b.surface_key;
        if(a.layer==Layer::background)same&=a.background.origin_x==b.background.origin_x
            &&a.background.origin_y==b.background.origin_y&&a.background.fine_x==b.background.fine_x;
        if(a.layer==Layer::sprite)same&=a.sprite.oam_slot==b.sprite.oam_slot;
        material_domain[region]=material_domain[region]&&same;
        bool solid=true;
        for(int y=-1;y<=1&&solid;++y)for(int x=-1;x<=1;++x){int n=neighbor(i,x,y);
            if(n<0||labels[n]!=labels[i]){solid=false;break;}}
        if(solid)core[labels[i]-1]=true;
    }
    // Preserve the incoming material domain verbatim. These historical whole-
    // cell guards confer no contour authority and cannot gain new eligibility.
    auto material_supported_corner=[&](int i,int c){
        const auto&r=regions.regions[labels[i]-1];
        if(!r.protected_fine||core[labels[i]-1])return true;
        // A compact lobe can have no complete 3x3 core. Certify this receiving
        // corner locally: an inward 2x2 and >=8/9 samples including the far
        // diagonal. A two-pixel outline fails that diagonal, a neck fails 2x2.
        int dx=-corner_x[c],dy=-corner_y[c],count=0;
        auto own=[&](int x,int y){int n=neighbor(i,x,y);return n>=0&&labels[n]==labels[i];};
        if(!own(dx,0)||!own(0,dy)||!own(dx,dy)||!own(2*dx,2*dy))return false;
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)count+=own(x*dx,y*dy);
        return count>=8;
    };
    for(int i=0;i<pixels;++i){
        const auto&r=regions.regions[labels[i]-1];
        // Incoming structural vetoes are retained for material modulation only.
        bool supported=false;for(int c=0;c<4;++c)supported|=material_supported_corner(i,c);
        bool locked=r.layer==Layer::backdrop||r.area<=6||!supported;
        auto same=[&](int x,int y){int n=neighbor(i,x,y);return n>=0&&labels[n]==labels[i];};
        bool solid=false;
        for(int c=0;c<4;++c)solid|=same(corner_x[c],0)&&same(0,corner_y[c])&&same(corner_x[c],corner_y[c]);
        if(!solid||(!same(-1,0)&&!same(1,0))||(!same(0,-1)&&!same(0,1)))locked=true;
        // Material retains the incoming full-neighborhood owner guard.
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){int n=neighbor(i,x,y);
            if(n<0||f.effects[i]!=f.effects[n]||!same_semantic_owner(f.samples[i],f.samples[n])){
                material_guard[i]=false;if(!x||!y)locked=true;
            }}
        material_locked[i]=locked;
        // The retained cross protects cardinal connectivity. Deforming this
        // corner additionally needs an inward block continuing to its far
        // diagonal: no sole terminal/neck support or thin outline corner.
        // This 3x3 inward stencil is independent of region size or a remote core.
        if(r.layer==Layer::backdrop)continue;
        for(int c=0;c<4;++c){int dx=corner_x[c],dy=corner_y[c];
            if(!same(-dx,0)||!same(0,-dy)||!same(-dx,-dy)||!same(-2*dx,-2*dy))continue;
            if(!same(-2*dx,-dy)&&!same(-dx,-2*dy))continue;
            bool safe=true;
            for(auto step:std::array<std::array<int,2>,3>{{{dx,0},{0,dy},{dx,dy}}}){
                int n=neighbor(i,step[0],step[1]);
                safe&=n>=0&&f.effects[i]==f.effects[n]&&same_semantic_owner(f.samples[i],f.samples[n]);
            }
            if(safe)out.cells[i].eligible_corners|=std::uint8_t(1<<c);
        }
    }
    auto appearance=[&](int a,int b){return f.indices[a]==f.indices[b]&&f.effects[a]==f.effects[b];};
    auto local_path=[&](int start,int finish,int nw){
        // A global label alone is insufficient: a remote bridge must not decide
        // a local crossing. Search one source-tile halo around this 2x2 only.
        constexpr int halo=8,side=2*halo+2;
        std::array<bool,side*side> seen{};
        std::array<int,side*side> queue{};int head=0,tail=0;
        const int left=nw%width-halo,top=nw/width-halo;
        auto index=[&](int n){return (n/width-top)*side+n%width-left;};
        queue[tail++]=start;seen[index(start)]=true;
        while(head<tail){int i=queue[head++];if(i==finish)return true;
            for(int e=0;e<4;++e){int n=neighbor(i,e==0?-1:e==1?1:0,e==2?-1:e==3?1:0);
                if(n<0||n%width<left||n%width>=left+side||n/width<top||n/width>=top+side
                   ||labels[n]!=labels[start]||out.topology.adjacency[i][e]!=TopologyRelation::same_topology_region)continue;
                auto k=index(n);if(!seen[k]){seen[k]=true;queue[tail++]=n;}
            }
        }
        return false;
    };
    // Decide all saddles before emitting cuts. Only an ALREADY four-connected
    // diagonal may continue, and the opposing diagonal must be two components.
    // Thus neither matching appearance nor matching keys can join objects.
    for(int y=0;y<height-1;++y)for(int x=0;x<width-1;++x){int a=y*width+x,b=a+1,c=a+width,d=c+1;
        if(!appearance(a,d)||!appearance(b,c)||appearance(a,b))continue;
        auto&decision=out.diagonals[a];decision=DiagonalResolution::ambiguous_diagonal;
        bool safe=true;for(int i:{a,b,c,d})safe&=f.effects[a]==f.effects[i]&&same_semantic_owner(f.samples[a],f.samples[i]);
        if(safe&&labels[a]==labels[d]&&labels[b]!=labels[c]
           &&(out.cells[b].eligible_corners&4)&&(out.cells[c].eligible_corners&2)&&local_path(a,d,a))
            decision=DiagonalResolution::connected_ad;
        if(safe&&labels[b]==labels[c]&&labels[a]!=labels[d]
           &&(out.cells[a].eligible_corners&8)&&(out.cells[d].eligible_corners&1)&&local_path(b,c,a))
            decision=DiagonalResolution::connected_bc;
    }
    // An unresolved crossing protects its four incident corners, not unrelated
    // corners of those cells. Each corner belongs to exactly one 2x2 decision.
    for(int i=0;i<pixels;++i)if(out.diagonals[i]==DiagonalResolution::ambiguous_diagonal){
        int c=3;for(int n:{i,i+1,i+width,i+width+1}){
            out.cells[n].eligible_corners&=std::uint8_t(~(1<<c--));material_locked[n]=true;
        }
    }
    for(int i=0;i<pixels;++i){auto&cell=out.cells[i];
        for(int c=0;c<4;++c){int a=neighbor(i,corner_x[c],0),b=neighbor(i,0,corner_y[c]);
            int d=neighbor(i,corner_x[c],corner_y[c]);
            if(a<0||b<0||d<0||!(cell.eligible_corners&(1<<c)))continue;
            // All four visible supports of this cut must agree spatially.
            // Diagonal support cannot bypass a hard/occlusion/effect boundary.
            if(f.effects[i]!=f.effects[d]||!same_semantic_owner(f.samples[i],f.samples[d])
               ||!same_semantic_owner(f.samples[a],f.samples[d])||!same_semantic_owner(f.samples[b],f.samples[d]))continue;
            // A donor need not be deformable: its exact observed color and
            // component remain authoritative even when its own cell is locked.
            // The receiving corner's local ownership guard covers all three.
            if(out.topology.adjacency[i][corner_x[c]<0?0:1]!=TopologyRelation::same_surface_different_appearance
               ||out.topology.adjacency[i][corner_y[c]<0?2:3]!=TopologyRelation::same_surface_different_appearance
               ||labels[a]!=labels[b])continue;
            int nw=std::min({i,a,b,d});auto saddle=out.diagonals[nw];
            bool ordinary=labels[a]==labels[d];
            bool resolved=(saddle==DiagonalResolution::connected_ad||saddle==DiagonalResolution::connected_bc)
                &&labels[i]!=labels[d];
            if(ordinary||resolved)cell.cuts[c]={a,63}; // <1/4 cell; disjoint, center/edge corridors survive.
        }
        const auto&r=regions.regions[labels[i]-1];
        // A newly joined spatial domain earns contour authority only. Retain
        // material eligibility/bounds solely for unchanged source-instance regions.
        cell.material=!material_locked[i]&&material_domain[labels[i]-1]&&r.area>=32&&r.max_x-r.min_x>=4&&r.max_y-r.min_y>=4;
        for(int y=-2;y<=2&&cell.material;++y)for(int x=-2;x<=2;++x){int n=neighbor(i,x,y);
            if(n<0||labels[n]!=labels[i]||material_locked[n]||!material_guard[n]){cell.material=false;break;}}
        // Preserve fine categorical interiors. Their outward silhouette may
        // cover a narrow, central edge strip in an independently supported
        // receiver. No hidden/foreign donor, narrow gap, or checkerboard may
        // supply it. At the maximum 8x extent strip coverage is <=1/4 sample;
        // it cannot create a categorical bridge or erase any receiver support.
        for(int e=0;e<4;++e){int dx=e==0?-1:e==1?1:0,dy=e==2?-1:e==3?1:0;
            int n=neighbor(i,dx,dy);
            if(n<0||r.layer==Layer::backdrop||out.topology.adjacency[i][e]!=TopologyRelation::same_surface_different_appearance)continue;
            auto donor_own=[&](int x,int y){int k=neighbor(n,x,y);return k>=0&&labels[k]==labels[n];};
            bool thin=(!donor_own(-1,0)&&!donor_own(1,0))||(!donor_own(0,-1)&&!donor_own(0,1));
            if(!thin&&!regions.regions[labels[n]-1].protected_fine)continue;
            bool safe=true;
            for(int depth=0;depth<3;++depth)for(int t=-1;t<=1;++t){
                int k=neighbor(i,-dx*depth+dy*t,-dy*depth+dx*t);
                safe&=k>=0&&labels[k]==labels[i];
            }
            // Tangential donor endpoints must also agree in visible ownership.
            for(int t:{-1,1}){int k=neighbor(n,dy*t,dx*t);
                safe&=k>=0&&f.effects[i]==f.effects[k]&&same_semantic_owner(f.samples[n],f.samples[k]);
            }
            if(safe)cell.edge_donors[e]=n;
        }
        bool active=cell.material;
        for(auto cut:cell.cuts)active|=cut.donor>=0;
        for(int donor:cell.edge_donors)active|=donor>=0;
        cell.locked=!active;
    }
    return out;
}
namespace {
// Twice the EXACT area of x+y<reach intersected with a rectangle, in Q32.
// Inclusion/exclusion integrates a half-plane via its positive quadratic.
// Q16 endpoints and int64 intermediates cover the entire supported extent.
std::int64_t triangle_area2(std::int64_t x0,std::int64_t y0,std::int64_t x1,std::int64_t y1,int reach){
    auto square=[](std::int64_t v){v=std::max<std::int64_t>(0,v);return v*v;};
    return square(reach-x0-y0)-square(reach-x1-y0)-square(reach-x0-y1)+square(reach-x1-y1);
}
Color semantic_color(const ContourIR&plan,const std::vector<Color>&colors,
                     int i,int x,int y,int w,int h){
    const auto&cell=plan.cells[i];auto color=colors[i];
    bool active=false;for(const auto&cut:cell.cuts)active|=cut.donor>=0;
    for(int donor:cell.edge_donors)active|=donor>=0;
    if(active){
        constexpr std::int64_t unit=65536;
        auto x0=std::int64_t(x)*width*unit/w-(i%width)*unit;
        auto x1=std::int64_t(x+1)*width*unit/w-(i%width)*unit;
        auto y0=std::int64_t(y)*height*unit/h-(i/width)*unit;
        auto y1=std::int64_t(y+1)*height*unit/h-(i/width)*unit;
        const auto total=2*(x1-x0)*(y1-y0);
        // Owner-conditioned boundary extension: translate a straddling footprint
        // inward, retaining its area. Simply clipping loses coverage nearest the
        // corner and can create isolated islands at noninteger output phases.
        // Translation makes coverage monotone towards the corner and never reads
        // outside the canonical cell's authorized donor set.
        std::array<std::int64_t,3> sums{};for(int k=0;k<3;++k)sums[k]=color[k]*total;
        for(int c=0;c<4;++c){const auto&cut=cell.cuts[c];if(cut.donor<0)continue;
            auto a=std::max<std::int64_t>(0,corner_x[c]<0?x0:unit-x1),b=a+x1-x0;
            auto d=std::max<std::int64_t>(0,corner_y[c]<0?y0:unit-y1),e=d+y1-y0;
            auto area=triangle_area2(a,d,b,e,cut.reach_q8*256);
            for(int k=0;k<3;++k)sums[k]+=(int(colors[cut.donor][k])-color[k])*area;
        }
        for(int e=0;e<4;++e){int donor=cell.edge_donors[e];if(donor<0)continue;
            auto normal0=e<2?x0:y0,normal1=e<2?x1:y1;
            auto tangent0=e<2?y0:x0,tangent1=e<2?y1:x1;
            auto start=std::max<std::int64_t>(0,(e&1)?unit-normal1:normal0);
            auto depth=std::max<std::int64_t>(0,std::min(start+normal1-normal0,unit/32)-start);
            auto length=std::max<std::int64_t>(0,std::min(tangent1,3*unit/4)-std::max(tangent0,unit/4));
            auto area=2*depth*length;
            for(int k=0;k<3;++k)sums[k]+=(int(colors[donor][k])-color[k])*area;
        }
        // Nonnegative disjoint areas form a convex combination: no ringing,
        // extrapolation, or RGB-dependent geometry; only final integer rounding.
        for(int k=0;k<3;++k)color[k]=byte(int((sums[k]+total/2)/total));
    }else if(cell.material){
        const auto&r=plan.topology.components.regions[plan.topology.components.labels[i]-1];
        int fx=int((std::int64_t(2*x+1)*width*256/(2*w))%256);
        int fy=int((std::int64_t(2*y+1)*height*256/(2*h))%256);
        int gx=((i%width-r.min_x)*256+fx)*4/((r.max_x-r.min_x+1)*256)-2;
        int gy=((i/width-r.min_y)*256+fy)*4/((r.max_y-r.min_y+1)*256)-2;
        int gain=gx+gy;
        for(auto&c:color)c=byte(int(c)+int(c)*gain/512);
    }
    return color;
}
std::vector<Color> signal_colors(const Frame&f,const std::vector<Color>&colors){
    std::vector<Signal> yiq(pixels);std::vector<Color> result(pixels);
    for(int i=0;i<pixels;++i){const auto&c=colors[i];
        yiq[i]={(77*c[0]+150*c[1]+29*c[2])/256,(152*c[0]-70*c[1]-82*c[2])/256,(54*c[0]-134*c[1]+80*c[2])/256};
    }
    constexpr int cosine[12]={256,222,128,0,-128,-222,-256,-222,-128,0,128,222};
    constexpr int sine[12]={0,128,222,256,222,128,0,-128,-222,-256,-222,-128};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x;
        auto at=[&](int offset)->const Signal&{return yiq[y*width+std::clamp(x+offset,0,width-1)];};
        int l=(at(-1)[0]+6*at(0)[0]+at(1)[0])/8,ic=0,q=0;
        for(int k=-3;k<=3;++k){int weight=4-std::abs(k);ic+=at(k)[1]*weight;q+=at(k)[2]*weight;}
        ic/=16;q/=16;
        int phase=(x*8+y*4+int(f.number%3)*4)%12;
        int chroma=(at(1)[1]-at(-1)[1])*cosine[phase]+(at(1)[2]-at(-1)[2])*sine[phase];
        int detail=at(0)[0]-(at(-1)[0]+at(1)[0])/2;
        l+=std::clamp(chroma/8192,-8,8);
        ic+=std::clamp(detail*cosine[phase]/4096,-8,8);
        q+=std::clamp(detail*sine[phase]/4096,-8,8);
        result[i]={byte(l+(245*ic+159*q)/256),byte(l-(70*ic+166*q)/256),byte(l+(-283*ic+436*q)/256)};
    }
    return result;
}
}
// Product Semantic HD uses observed visible ownership, including silhouettes.
// The historical contour reference below intentionally retains its old contract.
// A cut needs an inward 2x2 of one owner and an outward corner of one observed
// donor. No hidden candidate, temporal guess, texture, or scene extrusion enters.
static Image render_semantic_2d(const Frame& f,int w,int h){
    validate_frame(f);extent(w,h);
    std::vector<Color> colors(pixels);
    std::vector<std::array<int,4>> donors(pixels,{-1,-1,-1,-1});
    auto same=[&](int a,int b){return a>=0&&b>=0&&f.indices[a]==f.indices[b]
        &&f.effects[a]==f.effects[b]&&same_semantic_owner(f.samples[a],f.samples[b]);};
    for(int i=0;i<pixels;++i){
        colors[i]=reference_display_color(f.indices[i],f.effects[i]);
        if(f.samples[i].layer==Layer::backdrop)continue;
        for(int c=0;c<4;++c){
            const int dx=corner_x[c],dy=corner_y[c];
            const int a=neighbor(i,dx,0),b=neighbor(i,0,dy),d=neighbor(i,dx,dy);
            if(!same(i,neighbor(i,-dx,0))||!same(i,neighbor(i,0,-dy))
               ||!same(i,neighbor(i,-dx,-dy))||!same(a,b)||!same(a,d)||!same(b,d))continue;
            if(f.indices[i]==f.indices[a]||f.effects[i]!=f.effects[a])continue;
            // Foreign sprites are never donors. Background/backdrop may supply
            // only their already visible color at a foreground silhouette.
            if(f.samples[a].layer==Layer::sprite&&!same_semantic_owner(f.samples[i],f.samples[a]))continue;
            donors[i][c]=a;
        }
    }
    Image image{w,h,Bytes(std::size_t(w)*h*3)};
    constexpr std::int64_t unit=65536;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const int sx=int(std::int64_t(2*x+1)*width/(2*w)),sy=int(std::int64_t(2*y+1)*height/(2*h)),i=sy*width+sx;
        const auto x0=std::int64_t(x)*width*unit/w-sx*unit,x1=std::int64_t(x+1)*width*unit/w-sx*unit;
        const auto y0=std::int64_t(y)*height*unit/h-sy*unit,y1=std::int64_t(y+1)*height*unit/h-sy*unit;
        const auto total=2*(x1-x0)*(y1-y0);
        std::array<std::int64_t,3> sum{};for(int k=0;k<3;++k)sum[k]=colors[i][k]*total;
        for(int c=0;c<4;++c){const int donor=donors[i][c];if(donor<0)continue;
            const auto a=std::max<std::int64_t>(0,corner_x[c]<0?x0:unit-x1);
            const auto b=std::max<std::int64_t>(0,corner_y[c]<0?y0:unit-y1);
            const auto area=triangle_area2(a,b,a+x1-x0,b+y1-y0,int(unit/2));
            for(int k=0;k<3;++k)sum[k]+=(int(colors[donor][k])-colors[i][k])*area;
        }
        for(int k=0;k<3;++k)image.rgb[std::size_t(y*w+x)*3+k]=byte(int((sum[k]+total/2)/total));
    }
    return image;
}
static Image render_display(const Frame&f,Mode mode,int w,int h,bool reference){
    if(mode==Mode::MODERN_REMASTER)return render_modern(f,{},w,h);
    if(reference&&mode==Mode::SEMANTIC_HD)return render_semantic_2d(f,w,h);
    validate_frame(f);
    extent(w,h);
    if(mode==Mode::TEMPORAL_RECONSTRUCTION||mode==Mode::NEURAL_RESIDUAL_EXPERIMENTAL)throw std::invalid_argument("presentation rung is a disabled M0 seam");
    if(mode!=Mode::EXACT_DIGITAL&&mode!=Mode::AUTHENTIC_SIGNAL&&mode!=Mode::SEMANTIC_HD)throw std::invalid_argument("unknown presentation mode");
    if(mode==Mode::EXACT_DIGITAL&&(w%256||h%240||w/256!=h/240))throw std::invalid_argument("EXACT_DIGITAL requires uniform integer scaling");
    Image image{w,h,Bytes(static_cast<std::size_t>(w)*h*3)};
    std::vector<std::array<std::uint8_t,3>> colors(pixels);
    for(int i=0;i<pixels;++i)colors[i]=!reference||mode==Mode::AUTHENTIC_SIGNAL
        ?digital_color(f.indices[i],f.effects[i]):reference_display_color(f.indices[i],f.effects[i]);
    if(mode==Mode::AUTHENTIC_SIGNAL)colors=signal_colors(f,colors);
    ContourIR plan;if(mode==Mode::SEMANTIC_HD)plan=semantic_contours(f);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int sx=static_cast<int>((std::int64_t(2*x+1)*width)/(2*w)),sy=static_cast<int>((std::int64_t(2*y+1)*height)/(2*h));
        int i=sy*width+sx;auto color=colors[i];
        int fx=static_cast<int>((std::int64_t(2*x+1)*width*256/(2*w))%256);
        int fy=static_cast<int>((std::int64_t(2*y+1)*height*256/(2*h))%256);
        if(mode==Mode::SEMANTIC_HD)color=semantic_color(plan,colors,i,x,y,w,h);
        if(mode==Mode::AUTHENTIC_SIGNAL){
            int n=neighbor(i,fx<128?-1:1,0),mix=std::abs(fx-128);
            int response=256-(fy-128)*(fy-128)*40/16384;
            for(int c=0;c<3;++c){int beam=n<0?color[c]:(int(color[c])*(256-mix)+int(colors[n][c])*mix+128)/256;
                color[c]=byte(beam*response/256);}
        }
        auto dst=static_cast<std::size_t>(y*w+x)*3;for(int c=0;c<3;++c)image.rgb[dst+c]=color[c];
    }
    return image;
}
Image render(const Frame&f,Mode mode,int w,int h){return render_display(f,mode,w,h,false);}
Image render_reference(const Frame&f,Mode mode,int w,int h){return render_display(f,mode,w,h,true);}
Image render_diagnostic(const Frame&f,Diagnostic mode,int w,int h){
    validate_frame(f);extent(w,h);
    if(static_cast<unsigned>(mode)>static_cast<unsigned>(Diagnostic::reactive))throw std::invalid_argument("unknown presentation diagnostic");
    ContourIR plan;if(mode==Diagnostic::protected_structure)plan=semantic_contours(f);
    Image image{w,h,Bytes(std::size_t(w)*h*3)};
    constexpr std::array<Color,5> reactive{{{40,180,80},{240,80,40},{240,200,40},{200,60,220},{60,100,240}}};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int i=int(std::int64_t(2*y+1)*height/(2*h))*width+int(std::int64_t(2*x+1)*width/(2*w));
        const auto&s=f.samples[i];Color c{};
        switch(mode){
        case Diagnostic::layer:c=s.layer==Layer::sprite?Color{240,100,50}:s.layer==Layer::background?Color{50,180,240}:Color{30,30,30};break;
        case Diagnostic::asset:{auto key=s.asset_key;c={byte(48+int(key&127)),byte(48+int((key>>16)&127)),byte(48+int((key>>32)&127))};break;}
        case Diagnostic::protected_structure:c=plan.cells[i].locked?Color{255,220,60}:Color{24,32,40};break;
        case Diagnostic::motion:c={byte(128+int(s.motion_x)*4),byte(128+int(s.motion_y)*4),s.confidence};break;
        case Diagnostic::reactive:c=reactive[std::min<unsigned>(static_cast<unsigned>(s.reactive),4)];break;
        }
        auto dst=std::size_t(y*w+x)*3;for(int k=0;k<3;++k)image.rgb[dst+k]=c[k];
    }
    return image;
}
void write_ppm(const std::filesystem::path&p,const Image&i){
    if(i.width<=0||i.height<=0||i.width>2048||i.height>1920||i.rgb.size()!=std::size_t(i.width)*i.height*3)throw std::invalid_argument("invalid PPM image");
    auto f=output(p);f<<"P6\n"<<i.width<<' '<<i.height<<"\n255\n";f.write(reinterpret_cast<const char*>(i.rgb.data()),static_cast<std::streamsize>(i.rgb.size()));f.close();
}
void write_wav(const std::filesystem::path&p,std::span<const std::int16_t> samples){
    if(samples.size()>441000)throw std::invalid_argument("WAV capture bound");
    Encoder e;auto tag=[&](const char*s){for(int i=0;i<4;++i)e.u8(s[i]);};
    tag("RIFF");e.u32(36+static_cast<std::uint32_t>(samples.size()*2));tag("WAVE");tag("fmt ");e.u32(16);e.u16(1);e.u16(1);e.u32(44100);e.u32(88200);e.u16(2);e.u16(16);tag("data");e.u32(static_cast<std::uint32_t>(samples.size()*2));for(auto s:samples)e.u16(static_cast<std::uint16_t>(s));
    auto f=output(p);f.write(reinterpret_cast<const char*>(e.bytes.data()),static_cast<std::streamsize>(e.bytes.size()));f.close();
}
void write_semantics(const std::filesystem::path&p,const Frame&frame){
    auto regions=connected_regions(frame);auto f=output(p);
    f<<"{\"type\":\"frame\",\"schema\":1,\"frame\":"<<frame.number<<",\"width\":256,\"height\":240,\"sample_count\":61440,\"mapper_domain\":"<<frame.mapper_domain<<"}\n";
    for(const auto&r:regions.regions)f<<"{\"type\":\"region\",\"id\":"<<r.id<<",\"area\":"<<r.area<<",\"perimeter\":"<<r.perimeter<<",\"asset_key\":\""<<r.asset_key<<"\",\"layer\":"<<int(r.layer)<<",\"color\":"<<int(r.color)<<",\"protected_fine\":"<<(r.protected_fine?"true":"false")<<",\"bounds\":["<<r.min_x<<','<<r.min_y<<','<<r.max_x<<','<<r.max_y<<"]}\n";
    auto source=[&](const Source&s){
        f<<"{\"nametable\":"<<s.nametable<<",\"attribute\":"<<s.attribute<<",\"pattern\":"<<s.pattern
         <<",\"tile\":"<<int(s.tile)<<",\"palette\":"<<int(s.palette)<<",\"local\":["<<int(s.local_x)<<','<<int(s.local_y)
         <<"],\"oam_slot\":"<<int(s.oam_slot)<<",\"flags\":"<<int(s.flags)<<",\"origin\":["<<s.origin_x<<','<<s.origin_y
         <<"],\"scroll_v\":"<<s.scroll_v<<",\"fine_x\":"<<int(s.fine_x)<<",\"chr_source\":\""<<s.chr_source
         <<"\",\"chr_epoch\":"<<s.chr_epoch<<",\"high_chr_epoch\":"<<s.high_chr_epoch
         <<",\"fetch_epoch\":"<<s.fetch_epoch<<",\"high_fetch_epoch\":"<<s.high_fetch_epoch<<'}';
    };
    for(unsigned i=0;i<pixels;++i){const auto&s=frame.samples[i];
        f<<"{\"type\":\"sample\",\"frame\":"<<frame.number<<",\"x\":"<<s.x<<",\"scanline\":"<<s.y<<",\"dot\":"<<s.dot<<",\"palette_index\":"<<int(s.color)<<",\"emphasis\":"<<int(s.emphasis)<<",\"grayscale\":"<<(s.grayscale?"true":"false")<<",\"winner\":"<<int(s.layer)<<",\"bg_opaque\":"<<s.bg_opaque<<",\"sprite_opaque\":"<<s.sprite_opaque<<",\"sprite_zero\":"<<s.sprite_zero<<",\"sprite_behind\":"<<s.sprite_behind<<",\"region\":"<<regions.labels[i]<<",\"asset_key\":\""<<s.asset_key<<"\",\"surface_key\":\""<<s.surface_key<<"\",\"previous_surface\":\""<<s.previous_surface<<"\",\"motion\":["<<s.motion_x<<','<<s.motion_y<<"],\"confidence\":"<<int(s.confidence)<<",\"reactive\":"<<int(s.reactive)<<",\"background\":";
        source(s.background);f<<",\"sprite\":";source(s.sprite);f<<"}\n";
    }
    f.close();
}
}
