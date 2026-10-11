#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vkrt::nes {
namespace {
using Color=std::array<std::uint8_t,3>;
struct Axis {int cell{},q8{},end{};float center{},fraction{};std::int64_t lo{},hi{};};
void extent(int w,int h){if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("modern raster extent");}
std::vector<Axis> axis(int output,int native){
    std::vector<Axis> out(output);
    for(int x=0;x<output;++x){auto& a=out[x];a.cell=int(std::int64_t(2*x+1)*native/(2*output));
        a.q8=int((std::int64_t(2*x+1)*native*256/(2*output))%256);
        a.center=float((x+.5)*native/output);a.fraction=a.center-a.cell-.5f;
        a.lo=std::int64_t(x)*native*65536/output-std::int64_t(a.cell)*65536;
        a.hi=std::int64_t(x+1)*native*65536/output-std::int64_t(a.cell)*65536;
    }
    for(int x=output-1;x>=0;--x)out[x].end=x+1<output&&out[x+1].cell==out[x].cell?out[x+1].end:x+1;
    return out;
}
struct Coverage {std::array<int,9> donor{};std::array<std::int64_t,9> area{};std::int64_t total{};int count{1};};
Coverage coverage(const ContourCell& c,int i,const Axis& x,const Axis& y){
    Coverage out;out.donor[0]=i;out.total=2*(x.hi-x.lo)*(y.hi-y.lo);out.area[0]=out.total;
    constexpr std::int64_t unit=65536;
    auto add=[&](int donor,std::int64_t area){if(area<=0)return;out.donor[out.count]=donor;out.area[out.count++]=area;out.area[0]-=area;};
    for(int k=0;k<4;++k){const auto& cut=c.cuts[k];if(cut.donor<0)continue;
        auto a=std::max<std::int64_t>(0,(k&1)?unit-x.hi:x.lo),b=a+x.hi-x.lo;
        auto d=std::max<std::int64_t>(0,(k&2)?unit-y.hi:y.lo),e=d+y.hi-y.lo;
        auto square=[](std::int64_t v){v=std::max<std::int64_t>(0,v);return v*v;};
        auto reach=cut.reach_q8*256;
        add(cut.donor,square(reach-a-d)-square(reach-b-d)-square(reach-a-e)+square(reach-b-e));
    }
    for(int e=0;e<4;++e){int donor=c.edge_donors[e];if(donor<0)continue;
        auto n0=e<2?x.lo:y.lo,n1=e<2?x.hi:y.hi,t0=e<2?y.lo:x.lo,t1=e<2?y.hi:x.hi;
        auto start=std::max<std::int64_t>(0,(e&1)?unit-n1:n0);
        auto depth=std::max<std::int64_t>(0,std::min(start+n1-n0,unit/32)-start);
        auto length=std::max<std::int64_t>(0,std::min(t1,3*unit/4)-std::max(t0,unit/4));
        add(donor,2*depth*length);
    }
    return out;
}
Color material_base(const PreparedModernFrame& p,int i,const Axis& x,const Axis& y){
    auto c=p.colors()[i];if(!p.plan().contours.cells[i].material)return c;
    const auto& r=p.plan().contours.topology.components.regions[p.plan().cells[i].region-1];
    const int gx=((x.cell-r.min_x)*256+x.q8)*4/((r.max_x-r.min_x+1)*256)-2;
    const int gy=((y.cell-r.min_y)*256+y.q8)*4/((r.max_y-r.min_y+1)*256)-2;
    for(auto& v:c)v=std::uint8_t(std::clamp(int(v)+int(v)*(gx+gy)/512,0,255));
    return c;
}
Color coverage_base(const PreparedModernFrame& p,const Coverage& cv){
    Color c{};
    for(int k=0;k<3;++k){std::int64_t sum=0;for(int n=0;n<cv.count;++n)sum+=p.colors()[cv.donor[n]][k]*cv.area[n];
        c[k]=std::uint8_t(std::clamp(int((sum+cv.total/2)/cv.total),0,255));}
    return c;
}
float delta(const ModernPaintCell& p,int k,float x,float y){return p.delta[k]+p.dx[k]*x+p.dy[k]*y;}
std::uint8_t byte(float value){return std::uint8_t(std::clamp(int(std::floor(value+.5f)),0,255));}
Color covered(const PreparedModernFrame& p,int i,const Axis& x,const Axis& y,Color base,const Coverage& cv){
    if(p.plan().cells[i].ui)return base;
    for(int k=0;k<3;++k){double sum=0;
        for(int n=0;n<cv.count;++n){const int d=cv.donor[n];
            float fx=std::clamp(x.center-d%width,0.f,1.f)-.5f,fy=std::clamp(y.center-d/width,0.f,1.f)-.5f;
            sum+=double(delta(p.paint()[d],k,fx,fy))*double(cv.area[n])/double(cv.total);}
        base[k]=byte(float(base[k]+sum));}
    return base;
}
void valid(const PreparedModernFrame& p){
    if(p.paint().size()!=pixels||p.colors().size()!=pixels||p.plan().cells.size()!=pixels)
        throw std::invalid_argument("unprepared modern frame");
}
}

void raster_modern_into(Image& out,const PreparedModernFrame& p,int w,int h){
    extent(w,h);valid(p);out.width=w;out.height=h;out.rgb.resize(std::size_t(w)*h*3);
    // Only extents are memoized. No frame, observation, pointer or registry key
    // survives preparation. Per-thread axis tables are bounded by the API.
    thread_local std::vector<Axis> xs,ys;
    if(xs.size()!=std::size_t(w))xs=axis(w,width);
    if(ys.size()!=std::size_t(h))ys=axis(h,height);
    for(int y=0;y<h;++y){const auto& ay=ys[y];
        for(int x=0;x<w;){const int i=ay.cell*width+xs[x].cell,end=xs[x].end;const auto& paint=p.paint()[i];
            const bool constant=!paint.contour&&!p.plan().contours.cells[i].material
                &&paint.dx==std::array<float,3>{}&&paint.dy==std::array<float,3>{};
            if(constant){Color c=p.colors()[i];for(int k=0;k<3;++k)c[k]=byte(c[k]+paint.delta[k]);
                auto at=(std::size_t(y)*w+x)*3;for(;x<end;++x)for(int k=0;k<3;++k)out.rgb[at++]=c[k];
            }else for(;x<end;++x){const auto& ax=xs[x];Color c;
                if(paint.contour){const auto cv=coverage(p.plan().contours.cells[i],i,ax,ay);c=covered(p,i,ax,ay,coverage_base(p,cv),cv);}
                else {c=material_base(p,i,ax,ay);
                    for(int k=0;k<3;++k)c[k]=byte(c[k]+delta(paint,k,ax.fraction,ay.fraction));}
                auto at=(std::size_t(y)*w+x)*3;for(int k=0;k<3;++k)out.rgb[at+k]=c[k];
            }
        }
    }
}
Image raster_modern(const PreparedModernFrame& p,int w,int h){Image out;raster_modern_into(out,p,w,h);return out;}
Image render_modern_reference(const Frame& f,ModernControls controls,int w,int h,const ModernAssetRegistry* r){
    // Calling the frozen renderer also independently validates the base raster.
    const auto p=prepare_modern(f,controls,r);auto out=render(f,Mode::SEMANTIC_HD,w,h);
    const auto xs=axis(w,width),ys=axis(h,height);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){int i=ys[y].cell*width+xs[x].cell;
        const auto cv=coverage(p.plan().contours.cells[i],i,xs[x],ys[y]);auto at=(std::size_t(y)*w+x)*3;
        Color base{out.rgb[at],out.rgb[at+1],out.rgb[at+2]};
        const auto c=covered(p,i,xs[x],ys[y],base,cv);for(int k=0;k<3;++k)out.rgb[at+k]=c[k];
    }
    return out;
}
}
