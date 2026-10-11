#include "vkrt/nes_native_s0/metroid_smrc.hpp"
#include "vkrt/nes_native_s0/display_reference.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vkrt::nes {
namespace {
constexpr int side=24;
std::uint8_t byte(float v){return std::uint8_t(std::clamp(int(v+.5f),0,255));}
const Source& source(const Sample& s){return s.layer==Layer::sprite?s.sprite:s.background;}
bool valid_sample(const NativeSpan& c,const Sample& s,unsigned offset){
    const auto& a=c.source;const auto& b=source(s);
    return !c.unstable&&s.layer==c.layer&&a.nametable==b.nametable&&a.pattern==b.pattern&&a.palette==b.palette
        &&a.flags==b.flags&&a.oam_slot==b.oam_slot&&a.origin_x==b.origin_x&&a.origin_y==b.origin_y
        &&a.local_y==b.local_y&&a.chr_source==b.chr_source&&a.chr_epoch==b.chr_epoch&&a.high_chr_epoch==b.high_chr_epoch
        &&(s.layer==Layer::backdrop||int(b.local_x)==int(a.local_x)+(s.layer==Layer::sprite&&(a.flags&64)?-int(offset):int(offset)))
        &&b.local_x<8&&b.local_y<8;
}
// Receiver closure certifies each step. No ray can jump through backdrop, a
// different actor, palette effect, scroll plane or disconnected receiver.
bool join(const Frame& f,int a,int b){
    if(a<0||b<0||a>=pixels||b>=pixels)return false;
    const auto& x=f.samples[a];const auto& y=f.samples[b];
    if(x.layer!=y.layer||f.effects[a]!=f.effects[b])return false;
    if(x.layer==Layer::sprite)return x.sprite.oam_slot==y.sprite.oam_slot&&x.sprite.flags==y.sprite.flags;
    if(x.layer!=Layer::background||!x.bg_opaque||!y.bg_opaque)return false;
    return x.background.palette==y.background.palette
        &&x.background.origin_x+x.background.local_x-y.background.origin_x-y.background.local_x==int(x.x)-y.x
        &&x.background.origin_y+x.background.local_y-y.background.origin_y-y.background.local_y==int(x.y)-y.y;
}
NativeStructure structure(const Frame& f,int i){const auto& s=f.samples[i];
    return {f.indices[i],f.effects[i],s.layer==Layer::sprite?s.sprite.oam_slot:std::uint8_t(255),s.layer};}
}

SmrcForm compile_smrc_form(const FormKey& key){
    std::array<bool,side*side> mask{},glyph{},seen{};
    std::array<unsigned,side*side> roles{};
    std::array<float,side*side> distance{},height_field{};
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){const auto& bits=key.neighbors[(y/8)*3+x/8].bits;
        roles[y*side+x]=((bits[y%8]>>(7-x%8))&1)|(((bits[y%8+8]>>(7-x%8))&1)<<1);
        mask[y*side+x]=roles[y*side+x]!=0;}
    // Exact squared Euclidean distance on this bounded halo. Separable exhaustive
    // 24-element minima are simple and deterministic; paid only on cache miss.
    std::array<float,side*side> horizontal{};
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){float best=81;
        for(int xx=0;xx<side;++xx)if(!mask[y*side+xx])best=std::min(best,float((xx-x)*(xx-x)));
        horizontal[y*side+x]=best;}
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){float best=81;
        for(int yy=0;yy<side;++yy)best=std::min(best,horizontal[yy*side+x]+float((yy-y)*(yy-y)));
        distance[y*side+x]=std::sqrt(best);}
    // Small monochrome components are protected as possible glyphs. This is a
    // bounded asset compile, not framebuffer segmentation. Component decisions
    // avoid classifying different fragments of one glyph at tile seams.
    std::array<int,side*side> queue{};
    for(int start=0;start<side*side;++start)if(mask[start]&&!seen[start]){
        int count=1;queue[0]=start;seen[start]=true;unsigned role_mask=0;
        int minx=side,miny=side,maxx=0,maxy=0;float radius=0;
        for(int at=0;at<count;++at){int i=queue[at],x=i%side,y=i/side;
            minx=std::min(minx,x);maxx=std::max(maxx,x);miny=std::min(miny,y);maxy=std::max(maxy,y);
            radius=std::max(radius,distance[i]);role_mask|=1u<<roles[i];
            for(int n:{x>0?i-1:-1,x+1<side?i+1:-1,y>0?i-side:-1,y+1<side?i+side:-1})
                if(n>=0&&mask[n]&&!seen[n]){seen[n]=true;queue[count++]=n;}
        }
        const bool protect=(role_mask&(role_mask-1))==0&&radius<=2&&count<=48&&maxx-minx<8&&maxy-miny<8
            &&minx>0&&miny>0&&maxx<side-1&&maxy<side-1;
        if(protect)for(int at=0;at<count;++at)glyph[queue[at]]=true;
    }
    std::array<ReliefFamily,side*side> families{};
    // Per-point thickness, not a whole-tile scalar, controls the envelope. The
    // bounded support is smaller than the halo, so a tile seam is not a dome.
    for(int y=3;y<side-3;++y)for(int x=3;x<side-3;++x){int i=y*side+x;
        if(!mask[i]||glyph[i])continue;
        float medial=distance[i];unsigned occupied=0;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){medial=std::max(medial,distance[(y+dy)*side+x+dx]);occupied+=mask[(y+dy)*side+x+dx];}
        families[i]=occupied<14?ReliefFamily::porous:medial<3.1f?ReliefFamily::round:ReliefFamily::bevel;
        float d=std::max(0.f,distance[i]-.5f),u=std::min(d/4.5f,1.f),v=std::min(d/3.5f,1.f);
        float blend=std::clamp((medial-2)/2,0.f,1.f);blend=blend*blend*(3-2*blend);
        height_field[i]=blend*u*(2-u)+(1-blend)*std::sqrt(std::max(0.f,v*(2-v)));
    }
    SmrcForm result;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){int at=(y+8)*side+x+8;auto& r=result.texels[y*8+x];
        r.family=families[at];r.height=height_field[at];if(!mask[at]||glyph[at])continue;
        float dx=(height_field[at+1]-height_field[at-1])*1.8f,dy=(height_field[at+side]-height_field[at-side])*1.8f;
        const float inv=1/std::sqrt(1+dx*dx+dy*dy);r.nx=-dx*inv;r.ny=-dy*inv;r.nz=inv;
        // Concavity only: outside-mask samples cannot manufacture cavity AO.
        float cavity=0;unsigned n=0;for(int offset:{-1,1,-side,side})if(mask[at+offset]){
            cavity+=std::max(0.f,height_field[at+offset]-height_field[at]);++n;}
        r.ao=n?std::min(.14f,cavity*.22f/n):0;
    }
    return result;
}

std::array<float,3> smrc_response(std::array<std::uint8_t,3> base,SmrcLight light){
    const float gain=light.diffuse*(1-light.ao)*(1-light.contact)+light.emission;
    std::array<float,3> out{};
    // A flat unoccluded neutral surface is exactly F1. Highlights keep the
    // reference hue; no global pigment grade, dark wash or additive white fog.
    for(int k=0;k<3;++k)out[k]=std::clamp(base[k]*(gain+light.specular),0.f,255.f);
    return out;
}

void raster_metroid_smrc_into(Image& out,ModernScene& scene,ModernControls controls,int w,int h,SmrcDiagnostics* diag){
    if(!scene.frame_)throw std::invalid_argument("unprepared SMRC scene");
    if(scene.profile_!=PresentationProfile::metroid_usa_f1f4)throw std::invalid_argument("SMRC profile required");
    if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("SMRC extent");
    for(double v:{controls.structure_intensity,controls.tone_intensity})if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("SMRC controls");
    const auto& f=*scene.frame_;auto& paint=scene.paint_;paint.resize(pixels);
    std::array<std::array<std::array<std::uint8_t,3>,64>,16> palette;
    for(int e=0;e<16;++e)for(int c=0;c<64;++c)palette[e][c]=reference_display_color(std::uint8_t(c),std::uint8_t(((e>>1)<<5)|(e&1)));
    if(diag){diag->texels.assign(pixels,{});diag->coverage_xor_pixels=diag->owner_mismatch_pixels=0;
        diag->source_cells.resize(std::size_t(w)*h);diag->owners.resize(std::size_t(w)*h);}
    unsigned command=0;
    for(const auto& cmd:f.native->spans){
        const auto* form=scene.form_commands_[command];const bool stable=scene.commands_[command++]&&form&&form->smrc;
        const std::uint64_t identity=diag?identity_hash(cmd.shape.bits):0;
        for(unsigned offset=0;offset<cmd.count;++offset){int i=int(cmd.begin+offset);const auto& s=f.samples[i];const auto& src=source(s);
            const auto base=palette[((f.effects[i]>>5)<<1)|(f.effects[i]&1)][f.indices[i]&63];
            auto& p=paint[i];p.dx={};p.dy={};ReliefTexel r;SmrcLight light;
            const bool safe=stable&&valid_sample(cmd,s,offset);
            int x=src.local_x&7,y=src.local_y&7;if(s.layer==Layer::sprite){if(src.flags&64)x=7-x;if(src.flags&128)y=7-y;}
            if(safe)r=form->smrc->texels[y*8+x];
            // Conservative HUD band plus topology-thin glyphs. No OCR, title art
            // dictionary or CHR extraction is needed. Actors do not borrow HUD light.
            const bool flat=s.layer==Layer::backdrop||(s.layer==Layer::sprite&&i/width<48&&i%width<80)||r.family==ReliefFamily::flat||!safe;
            p.locked=flat;
            if(!flat){
                const float st=float(controls.structure_intensity),detail=float(controls.tone_intensity);
                // Single fixed upper-left key + ambient fill. Subtract the flat
                // normal response so the exact neutral point remains 1.
                float diffuse=1+.92f*(-.45f*r.nx-.55f*r.ny+.704f*r.nz-.704f);
                // Smooth, band-limited micro-normal response in CHR-local space;
                // no screen/frame seed or albedo noise. Boundary fade avoids seams.
                float u=(src.local_x+.5f)/8,v=(src.local_y+.5f)/8;
                float fade=256*u*u*(1-u)*(1-u)*v*v*(1-v)*(1-v);
                diffuse+=detail*.035f*fade*(u-v)*(r.family==ReliefFamily::porous?1.f:.4f);
                light.diffuse=1+st*(diffuse-1);light.ao=st*r.ao;
                float spec=std::max(0.f,-.25f*r.nx-.35f*r.ny+.902f*r.nz);
                spec*=spec;spec*=spec;spec*=spec;
                light.specular=st*std::max(0.f,spec-.438f)*(s.layer==Layer::sprite?.35f:r.family==ReliefFamily::porous?.08f:.22f);
                if(s.layer==Layer::background&&i>=width&&f.samples[i-width].layer==Layer::sprite&&!f.samples[i-width].sprite_behind)
                    light.contact=.09f*st;
                else if(s.layer==Layer::background&&i>=2*width&&join(f,i,i-width)&&f.samples[i-2*width].layer==Layer::sprite&&!f.samples[i-2*width].sprite_behind)
                    light.contact=.035f*st;
            }
            p.color=smrc_response(base,light);
            if(diag){auto& d=diag->texels[i];d.relief=flat?ReliefTexel{}:r;d.light=light;d.protected_flat=flat;d.fallback=!safe&&s.layer!=Layer::backdrop;
                d.identity=identity;d.owner=(unsigned(s.layer)<<24)|(s.layer==Layer::sprite?src.oam_slot:src.nametable);
                d.changed=s.layer==Layer::background?scene.background_[src.nametable&4095].last_change==scene.stats_.generation:
                    s.layer==Layer::sprite&&src.oam_slot<64&&scene.sprites_[src.oam_slot].last_change==scene.stats_.generation;}
        }
    }
    // Continuous shading inside identical canonical owner/color only. Categorical
    // edges, alpha, sprite priority and pixel coverage cannot move.
    auto compatible=[&](int a,int b){return !paint[a].locked&&!paint[b].locked&&join(f,a,b)&&f.indices[a]==f.indices[b];};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x;
        for(int k=0;k<3;++k){if(x>0&&x+1<width&&compatible(i,i-1)&&compatible(i,i+1))paint[i].dx[k]=(paint[i+1].color[k]-paint[i-1].color[k])*.5f;
            if(y>0&&y+1<height&&compatible(i,i-width)&&compatible(i,i+width))paint[i].dy[k]=(paint[i+width].color[k]-paint[i-width].color[k])*.5f;}}
    out.width=w;out.height=h;out.rgb.resize(std::size_t(w)*h*3);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int sx=int(std::int64_t(2*x+1)*width/(2*w)),sy=int(std::int64_t(2*y+1)*height/(2*h));
        const int i=sy*width+sx;const auto at=std::size_t(y)*w+x;const auto& p=paint[i];
        const float u=(x+.5f)*width/w-sx-.5f,v=(y+.5f)*height/h-sy-.5f;
        for(int k=0;k<3;++k)out.rgb[at*3+k]=byte(p.color[k]+p.dx[k]*u+p.dy[k]*v);
        if(diag){diag->source_cells[at]=i;diag->owners[at]=structure(f,i);}
    }
    if(diag)for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto at=std::size_t(y)*w+x;
        const int original=int((std::uint64_t(2*y+1)*height)/(2*h))*width+int((std::uint64_t(2*x+1)*width)/(2*w));
        diag->coverage_xor_pixels+=((f.samples[diag->source_cells[at]].layer!=Layer::backdrop)!=(f.samples[original].layer!=Layer::backdrop));
        diag->owner_mismatch_pixels+=diag->owners[at]!=structure(f,original);}
}

Image render_profile(const Frame& f,Mode mode,PresentationProfile profile,int w,int h){
    if(profile==PresentationProfile::standard)return render_reference(f,mode,w,h);
    if(mode!=Mode::MODERN_REMASTER||!f.native)return render_reference(f,Mode::EXACT_DIGITAL,w,h);
    // Same retained architecture as generic F4; only this thread's presentation
    // cache is title-specific. The ROM never enters the renderer.
    thread_local ModernScene scene(PresentationProfile::metroid_usa_f1f4);
    struct Release {ModernScene& s;~Release(){s.frame_.reset();}} release{scene};
    observe_modern(scene,ImmutableFrame(&f,[](const Frame*){}));return render_modern_scene(scene,{},w,h);
}

Image render_smrc_diagnostic(const Frame& f,const Image& f1,const Image& f4,const SmrcDiagnostics& d,SmrcView view){
    Image im{f4.width,f4.height,Bytes(f4.rgb.size())};
    for(std::size_t at=0;at<d.source_cells.size();++at){int i=int(d.source_cells[at]);const auto& t=d.texels[i];std::array<float,3> c{};
        switch(view){
        case SmrcView::height:c.fill(255*t.relief.height);break;
        case SmrcView::normal:c={127.5f*(1+t.relief.nx),127.5f*(1+t.relief.ny),255*t.relief.nz};break;
        case SmrcView::ao:c.fill(255*t.light.ao/.14f);break;
        case SmrcView::contact:c.fill(255*t.light.contact/.09f);break;
        case SmrcView::light:c.fill(128*(t.light.diffuse+t.light.specular)*(1-t.light.ao)*(1-t.light.contact));break;
        case SmrcView::emissive:c.fill(255*t.light.emission);break;
        case SmrcView::material:{constexpr std::array<float,3> colors[]{{40,40,40},{50,140,250},{250,160,40},{170,90,210}};c=colors[unsigned(t.relief.family)];break;}
        case SmrcView::identity:for(int k=0;k<3;++k)c[k]=float((t.identity>>(k*8))&255);break;
        case SmrcView::owner:{auto h=t.owner*2654435761u;for(int k=0;k<3;++k)c[k]=float((h>>(k*8))&255);break;}
        case SmrcView::churn:c=t.changed?std::array<float,3>{255,128,0}:std::array<float,3>{0,0,0};break;
        case SmrcView::coverage:{
            const int x=int(at%im.width),y=int(at/im.width);
            const int expected=int(std::int64_t(2*y+1)*height/(2*im.height))*width+int(std::int64_t(2*x+1)*width/(2*im.width));
            c=i!=expected||d.owners[at]!=structure(f,expected)?std::array<float,3>{255,0,0}:std::array<float,3>{0,0,0};break;}
        case SmrcView::delta:for(int k=0;k<3;++k)c[k]=float(std::abs(int(f4.rgb[at*3+k])-f1.rgb[at*3+k])*4);break;
        case SmrcView::albedo:for(int k=0;k<3;++k)c[k]=f1.rgb[at*3+k];break;
        }
        for(int k=0;k<3;++k)im.rgb[at*3+k]=byte(c[k]);
    }
    return im;
}
}
