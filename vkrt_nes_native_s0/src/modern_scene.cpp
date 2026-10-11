#include "vkrt/nes_native_s0/modern_scene.hpp"
#include "vkrt/nes_native_s0/modern_color.hpp"
#include "vkrt/nes_native_s0/metroid_smrc.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vkrt::nes {
namespace detail {
// Compact smoothstep: attached maximum, zero value/slope at the support edge.
// Used only for occlusion; material light, relief normals and pigments are fixed.
float contact_falloff(float distance) noexcept {
    const float t=std::clamp(distance,0.f,1.f);
    return (1-t)*(1-t)*(1+2*t);
}
}
namespace {
using detail::contact_falloff;
ShapeAppearance compile_shape(const ShapeKey& key){
    ShapeAppearance a;
    // Small, smooth source-local pigment field. No temporal seed, tile address,
    // ROM identity, or screen coordinate enters the asset.
    const float phase=float(identity_hash(key.bits)&31)/31.f;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){int i=y*8+x;
        a.role[i]=std::uint8_t(((key.bits[y]>>(7-x))&1)|(((key.bits[y+8]>>(7-x))&1)<<1));
        float u=(x+.5f)/8.f,v=(y+.5f)/8.f;
        // Fade only the small pigment detail at source-cell edges. The full
        // neighborhood relief is unchanged; tile-local detail cannot introduce
        // a sawtooth seam on a connected flat surface.
        float edge=256*u*u*(1-u)*(1-u)*v*v*(1-v)*(1-v);
        a.pigment[i]=edge*(.55f*(u-v)+.30f*(u*v-.25f)+.15f*phase*(1-2*v));
    }
    return a;
}
ShapeKey oriented(ShapeKey key,unsigned flags){
    if(flags&128)for(int y=0;y<4;++y){std::swap(key.bits[y],key.bits[7-y]);std::swap(key.bits[y+8],key.bits[15-y]);}
    if(flags&64)for(auto& b:key.bits){unsigned v=b;v=((v&0x55)<<1)|((v>>1)&0x55);v=((v&0x33)<<2)|((v>>2)&0x33);b=std::uint8_t((v<<4)|(v>>4));}
    return key;
}
FormAppearance compile_form(const FormKey& key){
    // Source-space distance to the OUTER opaque silhouette, shared across a
    // 3x3 pattern neighborhood. Internal palette islands are never separate domes.
    std::array<float,24*24> d{};
    for(int y=0;y<24;++y)for(int x=0;x<24;++x){const auto& b=key.neighbors[(y/8)*3+x/8].bits;
        d[y*24+x]=((b[y%8]|b[y%8+8])&(128>>(x%8)))?12.f:0.f;}
    for(int y=0;y<24;++y)for(int x=0;x<24;++x){auto& v=d[y*24+x];if(x)v=std::min(v,d[y*24+x-1]+1);if(y)v=std::min(v,d[(y-1)*24+x]+1);}
    for(int y=23;y>=0;--y)for(int x=23;x>=0;--x){auto& v=d[y*24+x];if(x<23)v=std::min(v,d[y*24+x+1]+1);if(y<23)v=std::min(v,d[(y+1)*24+x]+1);}
    auto height=[&](int x,int y){float u=std::min(d[y*24+x]/9.f,1.f);return u*(2-u);};
    FormAppearance out;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){int xx=x+8,yy=y+8,i=y*8+x;
        float nx=(height(xx+2,yy)-height(xx-2,yy)),ny=(height(xx,yy+2)-height(xx,yy-2));
        out.light[i]=std::clamp(.79f+.64f*(nx+ny),.34f,1.20f);
        out.contact[i]=.10f*contact_falloff(height(xx,yy));
    }
    return out;
}
const Source& source(const Sample& s){return s.layer==Layer::sprite?s.sprite:s.background;}
bool span_sample(const NativeSpan& cmd,const Sample& s,unsigned offset){
    const auto& t=source(s);const auto& a=cmd.source;
    return s.layer==cmd.layer&&t.nametable==a.nametable&&t.pattern==a.pattern&&t.palette==a.palette
        &&t.flags==a.flags&&t.oam_slot==a.oam_slot&&t.origin_x==a.origin_x&&t.origin_y==a.origin_y
        &&t.local_y==a.local_y&&t.chr_source==a.chr_source&&t.chr_epoch==a.chr_epoch
        &&t.high_chr_epoch==a.high_chr_epoch
        &&(s.layer==Layer::backdrop||int(t.local_x)==int(a.local_x)+((a.flags&64)&&s.layer==Layer::sprite?-int(offset):int(offset)));
}
bool paint_join(const Frame& f,int a,int b){
    const auto& x=f.samples[a];const auto& y=f.samples[b];
    if(x.layer!=y.layer||f.indices[a]!=f.indices[b]||f.effects[a]!=f.effects[b])return false;
    if(x.layer==Layer::sprite)return x.sprite.oam_slot==y.sprite.oam_slot&&x.sprite.origin_x==y.sprite.origin_x&&x.sprite.origin_y==y.sprite.origin_y&&x.sprite.flags==y.sprite.flags;
    // Equal palette role and adjacent fetched world coordinates permit a world
    // surface to continue across tile boundaries, without merging its topology.
    if(x.layer==Layer::background)return x.background.palette==y.background.palette
        &&x.background.origin_x+x.background.local_x-(y.background.origin_x+y.background.local_x)==int(x.x)-y.x
        &&x.background.origin_y+x.background.local_y-(y.background.origin_y+y.background.local_y)==int(x.y)-y.y;
    return true;
}
float luma(const std::array<std::uint8_t,3>& c){return (c[0]*.2126f+c[1]*.7152f+c[2]*.0722f)/255.f;}
std::uint8_t byte(float v){return std::uint8_t(std::clamp(int(v+.5f),0,255));}
}

AppearanceKey native_appearance_key(const NativeSpan& span){
    // Palette role is a binding, not part of content identity. Instance context
    // disambiguates reused shapes without claiming that an OAM slot is an object.
    const auto context=(unsigned(span.layer)<<16)|(span.layer==Layer::background
        ?span.source.nametable:span.layer==Layer::sprite?span.source.oam_slot:0);
    return {span.shape,span.source.palette,context};
}
const Frame& ModernScene::canonical() const {
    if(!frame_)throw std::invalid_argument("unprepared native scene");return *frame_;
}

void observe_modern(ModernScene& scene,ImmutableFrame f){
    if(!f||!f->native)throw std::invalid_argument("native PPU provenance required");
    if(f->indices.size()!=pixels||f->effects.size()!=pixels||f->samples.size()!=pixels)throw std::invalid_argument("native frame extent");
    scene.stats_={.generation=scene.stats_.generation+1};
    scene.frame_=std::move(f);const auto& tap=*scene.frame_->native;
    // Bound an adversarial stream of changing CHR. Eviction affects performance,
    // never output: assets are pure functions of complete content keys.
    if(scene.shapes_.size()>8192)scene.shapes_.clear();
    if(scene.forms_.size()>4096)scene.forms_.clear();
    scene.commands_.clear();scene.commands_.reserve(tap.spans.size());
    scene.form_commands_.clear();scene.form_commands_.reserve(tap.spans.size());
    std::array<bool,4096> bg_changed{};std::array<bool,64> sprite_changed{};
    for(unsigned i=0;i<64;++i)for(unsigned k=0;k<4;++k)if(scene.oam_[i*4+k]!=tap.oam[i*4+k])sprite_changed[i]=true;
    unsigned next=0;
    struct SpriteTile {int x,y;ShapeKey shape;unsigned palette;};
    std::vector<SpriteTile> sprite_tiles;
    std::array<bool,4096> bg_seen{},bg_irregular{};
    for(const auto& cmd:tap.spans){
        if(!cmd.count||cmd.begin!=next||cmd.count>unsigned(width)-cmd.begin%width||cmd.begin+cmd.count>pixels)
            throw std::invalid_argument("invalid PPU span coverage");
        next+=cmd.count;++scene.stats_.spans;
        bool unstable=cmd.unstable||!span_sample(cmd,scene.frame_->samples[cmd.begin],0)
            ||!span_sample(cmd,scene.frame_->samples[cmd.begin+cmd.count-1],cmd.count-1);
        if(unstable)++scene.stats_.unstable_spans;
        auto [it,inserted]=scene.shapes_.try_emplace(cmd.shape);
        if(inserted){it->second=compile_shape(cmd.shape);++scene.stats_.shape_builds;}
        scene.commands_.push_back(unstable?nullptr:&it->second);
        // Only content changes rebuild shape geometry. Palette binding and scroll
        // transforms are separate. Actual rows remain commands even for splits.
        if(cmd.layer==Layer::background){auto index=cmd.source.nametable&0xfff;auto& cell=scene.background_[index];
            if(bg_seen[index]&&cell.shape!=cmd.shape)bg_irregular[index]=true;
            bg_seen[index]=true;
            auto& row=scene.background_rows_[index][cmd.source.local_y&7];
            if(!cell.valid||row!=cmd.shape)bg_changed[index]=true;
            row=cmd.shape;
            cell.shape=cmd.shape;cell.palette_role=cmd.source.palette;cell.valid=true;
        }
        if(cmd.layer==Layer::sprite&&cmd.source.oam_slot<64){auto& instance=scene.sprites_[cmd.source.oam_slot];
            // OAM deltas invalidate the instance; CHR changes invalidate content.
            // Different halves of an unchanged 8x16 sprite are not a mutation.
            auto& row=scene.sprite_rows_[cmd.source.oam_slot][(int(cmd.begin/width)-cmd.source.origin_y)&15];
            if(!instance.valid||row!=cmd.shape)sprite_changed[cmd.source.oam_slot]=true;
            row=cmd.shape;
            instance.shape=cmd.shape;instance.palette_role=cmd.source.palette;instance.valid=true;
            int x=int(cmd.begin%width)-((cmd.source.flags&64)?7-cmd.source.local_x:cmd.source.local_x);
            int y=int(cmd.begin/width)-((cmd.source.flags&128)?7-cmd.source.local_y:cmd.source.local_y);
            auto found=std::find_if(sprite_tiles.begin(),sprite_tiles.end(),[&](const auto& t){return t.x==x&&t.y==y;});
            if(found==sprite_tiles.end())sprite_tiles.push_back({x,y,oriented(cmd.shape,cmd.source.flags),cmd.source.palette});
        }
    }
    if(next!=pixels)throw std::invalid_argument("incomplete PPU span coverage");
    auto form=[&](const FormKey& key){auto [it,added]=scene.forms_.try_emplace(key);if(added){
        if(scene.profile_==PresentationProfile::metroid_usa_f1f4)it->second.smrc=std::make_shared<SmrcForm>(compile_smrc_form(key));
        else it->second=compile_form(key);
        ++scene.stats_.form_builds;}return &it->second;};
    // At most one form resolution per visible world cell, independent of how
    // many pixels or scanlines expose it. Scroll only selects existing keys.
    std::array<bool,4096> resolved{};
    std::vector<const FormAppearance*> sprite_forms;
    for(const auto& tile:sprite_tiles){FormKey key;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            auto found=std::find_if(sprite_tiles.begin(),sprite_tiles.end(),[&](const auto& t){return t.x==tile.x+dx*8&&t.y==tile.y+dy*8&&t.palette==tile.palette;});
            if(found!=sprite_tiles.end())key.neighbors[(dy+1)*3+dx+1]=found->shape;
        }
        sprite_forms.push_back(form(key));
    }
    for(const auto& cmd:tap.spans){const FormAppearance* prepared=nullptr;
        if(cmd.layer==Layer::background){unsigned cell=cmd.source.nametable&0xfff;
            if(!resolved[cell]){FormKey key;unsigned bank=cell&0xc00;int x=cell&31,y=(cell&1023)/32;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int xx=x+dx,yy=y+dy;
                    // Follow the logical nametable plane across its scrolling
                    // seam. Unknown/irregular neighbors use an empty mask.
                    unsigned nb=bank;if(xx<0){xx+=32;nb^=0x400;}if(xx>=32){xx-=32;nb^=0x400;}
                    if(yy<0){yy+=30;nb^=0x800;}if(yy>=30){yy-=30;nb^=0x800;}
                    auto n=nb+yy*32+xx;
                    if(n<4096&&bg_seen[n]&&!bg_irregular[n]&&scene.background_[n].palette_role==scene.background_[cell].palette_role)
                        key.neighbors[(dy+1)*3+dx+1]=scene.background_[n].shape;
                }
                scene.background_forms_[cell]=form(key);resolved[cell]=true;
            }
            if(!bg_irregular[cell])prepared=scene.background_forms_[cell];
        }else if(cmd.layer==Layer::sprite){int x=int(cmd.begin%width)-((cmd.source.flags&64)?7-cmd.source.local_x:cmd.source.local_x);
            int y=int(cmd.begin/width)-((cmd.source.flags&128)?7-cmd.source.local_y:cmd.source.local_y);
            for(unsigned j=0;j<sprite_tiles.size();++j)if(sprite_tiles[j].x==x&&sprite_tiles[j].y==y){prepared=sprite_forms[j];break;}
        }
        scene.form_commands_.push_back(cmd.unstable?nullptr:prepared);
    }
    for(unsigned i=0;i<4096;++i)if(bg_changed[i]){++scene.background_[i].generation;scene.background_[i].last_change=scene.stats_.generation;++scene.stats_.background_updates;}
    for(unsigned i=0;i<64;++i)if(sprite_changed[i]){++scene.sprites_[i].generation;scene.sprites_[i].last_change=scene.stats_.generation;++scene.stats_.sprite_updates;}
    for(unsigned i=0;i<32;++i)if(scene.palette_[i]!=tap.palette[i])++scene.stats_.palette_updates;
    scene.palette_=tap.palette;scene.oam_=tap.oam;
}

void raster_modern_scene_into(Image& out,ModernScene& scene,ModernControls controls,int w,int h){
    if(scene.profile_==PresentationProfile::metroid_usa_f1f4){raster_metroid_smrc_into(out,scene,controls,w,h);return;}
    if(!scene.frame_)throw std::invalid_argument("unprepared native scene");
    for(double c:{controls.structure_intensity,controls.tone_intensity})if(!std::isfinite(c)||c<0||c>1)throw std::invalid_argument("modern controls");
    if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("modern extent");
    const auto& f=*scene.frame_;
    if(controls.structure_intensity==0&&controls.tone_intensity==0){out=render(f,Mode::SEMANTIC_HD,w,h);return;}
    const float structure=float(controls.structure_intensity),tone=float(controls.tone_intensity);
    auto& paint=scene.paint_;auto& shadow=scene.shadow_;
    paint.resize(pixels);shadow.assign(pixels,0.f);
    // Color lookup is prepared once. The output loop never classifies material,
    // hashes identity, searches a graph, allocates, or evaluates transcendental math.
    std::array<std::array<std::array<std::uint8_t,3>,64>,16> palette;
    std::array<std::array<ModernRgb,64>,16> albedos;
    for(int e=0;e<16;++e)for(int c=0;c<64;++c){
        palette[e][c]=modern_source_color(std::uint8_t(c),std::uint8_t(((e>>1)<<5)|(e&1)));
        albedos[e][c]=modern_albedo(palette[e][c],tone);
    }
    auto color=[&](int i)->const std::array<std::uint8_t,3>& {return palette[((f.effects[i]>>5)<<1)|(f.effects[i]&1)][f.indices[i]&63];};
    // Contact is conservative: an actually visible sprite with no sprite below,
    // and an opaque background immediately below, provides ordinal adjacency.
    // Projection stays on that receiver's connected color support; no floating
    // objects cast guesses into the empty backdrop.
    for(int y=0;y<height-1;++y)for(int x=0;x<width;++x){int i=y*width+x;
        const auto& s=f.samples[i];int below=i+width;
        if(s.layer!=Layer::sprite||s.sprite_behind||f.samples[below].layer!=Layer::background||!f.samples[below].bg_opaque)continue;
        for(int dy=0;dy<3&&y+1+dy<height;++dy)for(int dx=-2;dx<=2;++dx){
            if(x+dx<0||x+dx>=width)continue;
            const int n=below+dy*width+dx;
            // Certify every step on connected receiver support. Equal endpoint
            // colors alone must never let a shadow jump an intervening edge.
            bool connected=true;int previous=below;
            for(int sy=1;sy<=dy;++sy){int next=below+sy*width;connected&=paint_join(f,previous,next)&&f.samples[next].bg_opaque;previous=next;}
            const int direction=dx<0?-1:1;
            for(int sx=0;sx!=dx;sx+=direction){int next=previous+direction;connected&=paint_join(f,previous,next)&&f.samples[next].bg_opaque;previous=next;}
            if(!connected)continue;
            const float radius_squared=dx*dx/6.25f+dy*dy/4.f;
            shadow[n]=std::max(shadow[n],.18f*contact_falloff(radius_squared));
        }
    }
    unsigned command=0;
    for(const auto& cmd:f.native->spans){const auto* asset=scene.commands_[command];const auto* form=scene.form_commands_[command++];
        for(unsigned offset=0;offset<cmd.count;++offset){int i=int(cmd.begin+offset),x=i%width,y=i/width;
            const auto& sample=f.samples[i];const auto& src=source(sample);const auto base=color(i);
            const float lum=luma(base);auto& p=paint[i];p.dx={};p.dy={};
            const bool safe=asset&&span_sample(cmd,sample,offset);
            // Preserve the frozen split-fetch/stale-provenance fallback contract.
            if(!safe){auto legacy=digital_color(f.indices[i],f.effects[i]);for(int k=0;k<3;++k)p.color[k]=legacy[k];continue;}
            const auto& albedo=albedos[((f.effects[i]>>5)<<1)|(f.effects[i]&1)][f.indices[i]&63];
            const float pigment=asset->pigment[(src.local_y&7)*8+(src.local_x&7)];
            // One fixed decorative light field on the backdrop: no stars/cloud
            // silhouettes, geometry, motion, or temporal random seeds.
            if(sample.layer==Layer::backdrop){
                float u=(x+.5f)/width,v=(y+.5f)/height;
                float pool=std::max(0.f,1.f-1.65f*((u-.30f)*(u-.30f)+(v-.33f)*(v-.33f)));
                float wash=.38f+.49f*pool+.10f*v;
                // Compact separation on the universal backdrop only. This is
                // a decorative light occlusion field, not a physical Z estimate.
                // The source silhouette is in front of this receiver by the PPU
                // composition relation; OAM rank never scales the projection.
                float occlusion=0;
                int previous=i;
                for(int d=1;d<3&&x>=d&&y>=d;++d){const int n=(y-d)*width+x-d;
                    if(f.samples[n].layer!=Layer::backdrop){occlusion=.16f*contact_falloff(d/3.f);break;}
                    if(!paint_join(f,previous,n))break;
                    previous=n;
                }
                const float visibility=1-structure*occlusion;
                p.color=modern_light_response(albedo,(1+tone*(wash-1))*visibility,14.f*tone*pool*lum*visibility);
                continue;
            }
            int fx=src.local_x,fy=src.local_y;
            if(sample.layer==Layer::sprite){if(src.flags&64)fx=7-fx;if(src.flags&128)fy=7-fy;}
            float key=form?form->light[fy*8+fx]:.85f;
            const float mx=std::max({base[0],base[1],base[2]})/255.f;
            const bool actor=sample.layer==Layer::sprite;
            const bool ink=mx<.10f;
            // Material families are palette/structure roles: warm matte pigment,
            // cool glazed pigment, neutral paper and actor lacquer. No labels.
            float gloss=(base[2]>base[0]+12?.23f:.09f)+(actor?.10f:0.f);
            float body=.85f+.16f*key+.13f*pigment;
            float highlight=std::max(0.f,key-.92f)*gloss;
            float ao=(form?form->contact[fy*8+fx]:0.f)+shadow[i];
            const float illumination=.26f+.82f*key;
            const float diffuse=1+tone*(body-1)+structure*body*(illumination*(1-ao)-1);
            p.color=modern_light_response(albedo,diffuse,structure*255*highlight*lum);
            // Categorical dark linework stays legible, including actor faces.
            if(ink)for(int k=0;k<3;++k)p.color[k]=base[k];
        }
    }
    // Continuous subpixel paint only within an unchanged canonical color/owner.
    // Exact source-cell boundaries and thin silhouettes remain authoritative.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x;
        for(int k=0;k<3;++k){
            if(x>0&&x+1<width&&paint_join(f,i,i-1)&&paint_join(f,i,i+1))paint[i].dx[k]=.5f*(paint[i+1].color[k]-paint[i-1].color[k]);
            if(y>0&&y+1<height&&paint_join(f,i,i-width)&&paint_join(f,i,i+width))paint[i].dy[k]=.5f*(paint[i+width].color[k]-paint[i-width].color[k]);
        }
    }
    struct Axis {int cell;float local;};std::vector<Axis> xs(w),ys(h);
    for(int x=0;x<w;++x){float u=(x+.5f)*width/w;xs[x]={int(u),u-int(u)-.5f};}
    for(int y=0;y<h;++y){float v=(y+.5f)*height/h;ys[y]={int(v),v-int(v)-.5f};}
    out.width=w;out.height=h;out.rgb.resize(std::size_t(w)*h*3);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto& p=paint[ys[y].cell*width+xs[x].cell];auto at=(std::size_t(y)*w+x)*3;
        for(int k=0;k<3;++k)out.rgb[at+k]=byte(p.color[k]+p.dx[k]*xs[x].local+p.dy[k]*ys[y].local);
    }
}
Image render_modern_scene(ModernScene& s,ModernControls c,int w,int h){Image out;raster_modern_scene_into(out,s,c,w,h);return out;}
std::vector<NativeStructure> modern_structure(const ModernScene& scene,int w,int h){
    if(w<256||h<240||w>2048||h>1920)throw std::invalid_argument("modern structure extent");
    const auto& f=scene.canonical();std::vector<NativeStructure> out(std::size_t(w)*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){int sx=int(std::int64_t(2*x+1)*width/(2*w)),sy=int(std::int64_t(2*y+1)*height/(2*h));
        int i=sy*width+sx;const auto& s=f.samples[i];out[std::size_t(y)*w+x]={f.indices[i],f.effects[i],s.layer==Layer::sprite?s.sprite.oam_slot:std::uint8_t(255),s.layer};}
    return out;
}
Image render_native_modern(const Frame& f,ModernControls c,int w,int h){
    // Content-keyed cache; the borrowed Frame never survives the call. Thread
    // isolation avoids locks and does not make frame addresses cache identity.
    thread_local ModernScene s;
    struct Release {ModernScene& s;~Release(){s.frame_.reset();}} release{s};
    observe_modern(s,ImmutableFrame(&f,[](const Frame*){}));return render_modern_scene(s,c,w,h);
}
}
