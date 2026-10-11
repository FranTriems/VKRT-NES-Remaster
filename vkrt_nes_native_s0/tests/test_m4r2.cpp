#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,std::uint64_t> failure_names;
void check(bool value,const char*name){++checks;if(!value){++failures;++failure_names[name];}}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad,"bounded input rejected");}
bool near(double a,double b,double e=1e-11){return std::abs(a-b)<e;}
void paint(Frame&f,int x,int y,std::uint8_t c){int i=y*width+x;f.samples[i].color=f.indices[i]=c;}
Frame blank(std::uint8_t color=0x0f){
    Frame f;f.number=1;
    for(int i=0;i<pixels;++i){auto&s=f.samples[i];s.x=i%width;s.y=i/width;s.dot=s.x+1;
        s.layer=Layer::background;s.bg_opaque=true;s.background.pattern=16;s.background.tile=1;
        s.background.local_x=s.x%8;s.background.local_y=s.y%8;paint(f,s.x,s.y,color);}
    return f;
}
void rect(Frame&f,int x0,int y0,int x1,int y1,std::uint8_t color){
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)paint(f,x,y,color);
}
void sprite(Frame&f,int x0,int y0,int x1,int y1,int slot=2){
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){auto&s=f.samples[y*width+x];
        s.layer=Layer::sprite;s.sprite_opaque=true;s.sprite.origin_x=std::int16_t(x0);s.sprite.origin_y=std::int16_t(y0);
        s.sprite.oam_slot=std::uint8_t(slot);s.sprite.pattern=32;s.sprite.tile=2;
        s.sprite.local_x=std::uint8_t((x-x0)%8);s.sprite.local_y=std::uint8_t((y-y0)%8);}
}
Frame shapes(){
    auto f=blank();
    for(int y=15;y<100;++y)for(int x=15;x<100;++x)if((x-55)*(x-55)+(y-55)*(y-55)<34*34)paint(f,x,y,0x30);
    rect(f,115,20,178,83,0x30);rect(f,132,37,161,66,0x0f); // categorical hole
    rect(f,194,20,196,51,0x30);rect(f,196,20,216,22,0x30);rect(f,196,33,211,35,0x30); // glyph
    rect(f,20,125,105,192,0x30);rect(f,44,125,81,171,0x0f); // concave U
    rect(f,120,124,148,165,0x30);rect(f,149,124,177,165,0x30); // one-source-pixel gap
    rect(f,194,110,225,154,0x30);sprite(f,194,110,225,154);
    return f;
}
std::array<int,3> rgb(const Image&im,int x,int y){auto p=(std::size_t(y)*im.width+x)*3;return {im.rgb[p],im.rgb[p+1],im.rgb[p+2]};}
struct Topology {int components{},holes{};bool operator==(const Topology&)const=default;};
Topology topology(const Image&im){
    const int w=im.width,h=im.height;std::vector<bool> bits(std::size_t(w)*h),seen(bits.size());
    for(int i=0;i<w*h;++i)bits[i]=int(im.rgb[i*3])+im.rgb[i*3+1]+im.rgb[i*3+2]>330;
    std::vector<int> queue;Topology t;
    for(int start=0;start<w*h;++start)if(!seen[start]){
        bool border=false;queue.clear();queue.push_back(start);seen[start]=true;
        for(std::size_t j=0;j<queue.size();++j){int i=queue[j],x=i%w,y=i/w;border|=!x||!y||x==w-1||y==h-1;
            for(auto [dx,dy]:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}}){
                int xx=x+dx,yy=y+dy;if(xx<0||xx>=w||yy<0||yy>=h)continue;int n=yy*w+xx;
                if(!seen[n]&&bits[n]==bits[start]){seen[n]=true;queue.push_back(n);}}
        }
        if(bits[start])++t.components;else if(!border)++t.holes;
    }return t;
}
void frozen_machine(){
    check(int(Mode::EXACT_DIGITAL)==0,"exact ordinal");check(int(Mode::AUTHENTIC_SIGNAL)==1,"signal ordinal");
    check(int(Mode::SEMANTIC_HD)==2,"semantic ordinal");check(int(Mode::TEMPORAL_RECONSTRUCTION)==3,"temporal ordinal");
    check(int(Mode::NEURAL_RESIDUAL_EXPERIMENTAL)==4,"neural ordinal");check(int(Mode::MODERN_REMASTER)==5,"modern appended");
    Machine m(synthetic_nrom());ImmutableFrame observation;
    for(unsigned frame=0;frame<8;++frame){m.controllers[0].buttons=frame%3==0?1:0;m.run_frames(1);observation=observe(*m.frame(),observation.get());}
    const auto state=m.serialize();const auto audio=m.apu.pcm;const auto hash=frame_hash(*observation);
    // RGB receipts captured from the untouched incoming R0.4.3 executable,
    // independently of this candidate (synthetic demo, 8 frames, default size).
    const char* goldens[]={"2bba2b84eb67008fa75e93a0eb31718b3280f2d0fcea837546aa874f8c82170c",
        "f577b82ed9fe7082a0ce2f0c4d8ba5435024030da7ba3675c2bbb3d065387eb3",
        "cb937d4f0153e689fb89c332616413ffb806cf0bf8d4e6a344347274644bd9bd"};
    for(int mode=0;mode<3;++mode)check(sha256(render(*observation,Mode(mode)).rgb)==goldens[mode],"legacy R0.4.3 RGB golden");
    auto modern=render(*observation,Mode::MODERN_REMASTER);
    check(modern==render_modern(*observation),"public mode dispatch");
    check(state==m.serialize()&&audio==m.apu.pcm&&hash==frame_hash(*observation),"machine and observation invariant");
    Machine reference(synthetic_nrom());for(unsigned frame=0;frame<8;++frame){reference.controllers[0].buttons=frame%3==0?1:0;reference.run_frames(1);}
    m.run_frames(1);reference.run_frames(1);check(m.serialize()==reference.serialize(),"future execution unaffected");
    rejects([&]{render(*observation,Mode::TEMPORAL_RECONSTRUCTION);});
    rejects([&]{render(*observation,Mode::NEURAL_RESIDUAL_EXPERIMENTAL);});
    rejects([&]{render(*observation,Mode(6));});
    check(!NeuralResidualPolicy{}.enabled,"neural default disabled");
}
void plan_and_extents(){
    auto f=observe(shapes());auto p=modern_plan(*f),repeat=modern_plan(*f);auto original=semantic_contours(*f);
    check(p.cells==repeat.cells,"entire plan exact repeat");
    check(p.contours.topology.components.labels==original.topology.components.labels,"categorical regions unchanged");
    check(p.contours.topology.adjacency==original.topology.adjacency,"categorical owner adjacency unchanged");
    check(p.contours.diagonals==original.diagonals,"saddle decisions immutable");
    for(int i=0;i<pixels;++i){const auto&c=p.cells[i];const auto&cell=p.contours.cells[i];
        check(c.region==original.topology.components.labels[i],"each region retains source identity");
        check(c.relief>=0&&c.relief<=1&&std::isfinite(c.relief),"bounded relief");
        check(near(c.normal.x*c.normal.x+c.normal.y*c.normal.y+c.normal.z*c.normal.z,1),"normalized finite normal");
        check(c.ao>=0&&c.ao<=.16&&c.contact>=0&&c.contact<=.14&&c.cast_shadow>=0&&c.cast_shadow<=.23,"bounded occlusion response");
        check(c.structure_mask>=0&&c.structure_mask<=1&&c.tone_mask>=0&&c.tone_mask<=1,"bounded independent masks");
        check(c.context.asset_key==f->samples[i].asset_key&&c.context.surface_key==f->samples[i].surface_key,"exact provenance");
        check(cell.locked==original.cells[i].locked&&cell.eligible_corners==original.cells[i].eligible_corners,"immutable contour guard");
        for(int j=0;j<4;++j)check(cell.cuts[j].donor==original.cells[i].cuts[j].donor&&cell.cuts[j].reach_q8==original.cells[i].cuts[j].reach_q8
            &&cell.edge_donors[j]==original.cells[i].edge_donors[j],"immutable authorized coverage");
    }
    check(p.cells[25*width+194].ui&&p.cells[25*width+194].structure_mask==0&&p.cells[25*width+194].tone_mask==0,"glyph masks flat");
    check(p.cells[130*width+206].actor&&!p.cells[130*width+206].world,"actor mask");
    check(p.cells[55*width+55].world&&!p.cells[55*width+55].actor,"world mask");
    const auto canonical=render(*f,Mode::EXACT_DIGITAL,256,240);auto expected=topology(canonical);
    for(auto [w,h]:std::array<std::pair<int,int>,5>{{{768,720},{1024,960},{1280,1200},{901,803},{256,240}}}){
        auto hd=render(*f,Mode::SEMANTIC_HD,w,h),modern=render_modern(*f,{},w,h);
        check(render_modern(*f,{0,0},w,h)==hd,"zero/zero exact entire semantic image");
        check(modern==render_modern(*f,{},w,h),"same frame full-image determinism");
        check(topology(modern)==expected,"components and holes survive ink lighting and coverage");
        check(modern!=hd,"modern is active");
        int interiors=0,interior_changed=0;std::uint64_t interior_delta=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int sx=int(std::int64_t(2*x+1)*width/(2*w)),sy=int(std::int64_t(2*y+1)*height/(2*h));
            const auto&c=p.cells[sy*width+sx];auto a=rgb(modern,x,y),b=rgb(hd,x,y);
            if(c.ui)check(a==b,"protected glyph/ink remains crisp at every output extent");
            if(c.distance>5&&c.structure_mask>.5&&c.material!=ModernMaterial::OBJECT_FLAT){
                ++interiors;interior_changed+=a!=b;
                for(int k=0;k<3;++k){interior_delta+=std::abs(a[k]-b[k]);check(a[k]>=int(b[k]*.59),"interior palette floor");}
            }
        }
        check(interiors>100,"interior support exercised");
        check(interior_changed>interiors*3/4&&interior_delta>std::uint64_t(interiors)*9,"material activity and amplitude in interiors");
    }
    auto structure=render_modern(*f,{1,0}),tone=render_modern(*f,{0,1}),full=render_modern(*f);
    check(structure!=tone&&structure!=full&&tone!=full,"separate structure tone controls");
    for(int d=0;d<4;++d){auto im=render_modern_diagnostic(*f,ModernDiagnostic(d),256,240);
        check(im==render_modern_diagnostic(*f,ModernDiagnostic(d),256,240),"diagnostic repeat");}
    rejects([&]{render_modern(*f,{-1,0});});rejects([&]{render_modern(*f,{0,1.01});});
    rejects([&]{render_modern(*f,{std::numeric_limits<double>::quiet_NaN(),0});});
    rejects([&]{render_modern(*f,{},255,240);});rejects([&]{render_modern(*f,{},2049,1920);});
    rejects([&]{render_modern_diagnostic(*f,ModernDiagnostic(4));});
}
void classification(){
    Region r;r.area=400;r.perimeter=80;r.layer=Layer::background;r.max_x=19;r.max_y=19;
    for(int red=0;red<256;red+=7)for(int green=0;green<256;green+=9)for(int blue=0;blue<256;blue+=11){
        std::array<std::uint8_t,3> c{std::uint8_t(red),std::uint8_t(green),std::uint8_t(blue)};
        auto a=modern_classify(c,r,false,false);
        check(a==modern_classify(c,r,false,false),"deterministic generic material classifier");
        check(a.roughness>=.5&&a.roughness<=1&&a.confidence>=0&&a.confidence<=1,"conservative material response");
        if(a.material==ModernMaterial::UNKNOWN_SAFE)check(a.specular==0&&a.roughness==1,"unknown remains matte");
    }
    check(modern_classify({180,45,190},r,false,false).material==ModernMaterial::UNKNOWN_SAFE,"unknown color fallback");
    check(modern_classify({20,170,35},r,false,false).material==ModernMaterial::FOLIAGE,"green geometry foliage cue");
    check(modern_classify({150,150,150},r,false,false).material==ModernMaterial::STONE,"neutral mineral cue");
    check(modern_classify({20,170,35},r,true,false).material==ModernMaterial::ACTOR,"actor evidence dominates color");
    check(modern_classify({20,170,35},r,false,true).material==ModernMaterial::UI_FLAT,"UI evidence dominates color");
}
void boundary_isolation(){
    for(int variant=0;variant<5;++variant){auto raw=blank(0x28);
        for(auto&s:raw.samples)if(s.x>=128){
            if(variant==0)s.background.nametable=1;
            if(variant==1){s.layer=Layer::sprite;s.sprite_opaque=true;}
            if(variant==2)s.background.chr_epoch=1;
            if(variant==3){s.emphasis=1;raw.effects[s.y*width+s.x]=32;}
            if(variant==4)s.sprite_behind=true;
        }
        auto a=observe(raw);const auto p=modern_plan(*a);
        for(int y=0;y<height;++y)for(int x=128;x<width;++x)paint(raw,x,y,((x+y)&1)?0x30:0x0f);
        auto b=observe(raw);const auto q=modern_plan(*b);
        for(int y=0;y<height;++y)for(int x=0;x<128;++x){int i=y*width+x;
            check(p.cells[i].relief==q.cells[i].relief,"foreign color/geometry cannot affect height");
            check(p.cells[i].normal==q.cells[i].normal,"normal taps do not cross hard/effect/occlusion boundary");
            check(p.cells[i].material==q.cells[i].material,"classification independent of foreign owner");
        }
        std::vector<double> h(pixels,.1),foreign=h;
        for(int y=0;y<height;++y)for(int x=128;x<width;++x)foreign[y*width+x]=.9;
        auto n=modern_normals(p.contours.topology,h),nn=modern_normals(p.contours.topology,foreign);
        for(int y=0;y<height;++y)for(int x=124;x<128;++x)check(n[y*width+x]==nn[y*width+x],"direct adversarial relief boundary");
    }
}
void geometry(){
    auto raw=blank(0x30);auto f=observe(raw);auto t=semantic_topology(*f);
    std::vector<double> ramp(pixels),convex(pixels),concave(pixels),flat(pixels,.5);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x;double r2=((x-128.)*(x-128.)+(y-120.)*(y-120.))/40000.;
        ramp[i]=double(x)/255;convex[i]=1-r2;concave[i]=r2;}
    auto nr=modern_normals(t,ramp),nc=modern_normals(t,convex),nv=modern_normals(t,concave),nf=modern_normals(t,flat);
    for(int y=8;y<height-8;++y)for(int x=8;x<width-8;++x){int i=y*width+x;
        check(nr[i].x<0&&near(nr[i].y,0),"linear ramp constant directional normal");
        check(near(nc[i].x,-nv[i].x)&&near(nc[i].y,-nv[i].y)&&near(nc[i].z,nv[i].z),"convex concave opposition");
        check(nf[i]==ModernNormal{},"flat has no artificial geometry");
        if(x!=128)check((nc[i].x>0)==(x>128),"radial convex normal sign");
    }
    auto shape=observe(shapes());auto p=modern_plan(*shape);
    check(p.cells[55*width+23].normal.x<-.05&&p.cells[55*width+87].normal.x>.05,"circle outward normals");
    check(p.cells[27*width+145].normal.y<0&&p.cells[33*width+145].normal.y>0,"ring inner and outer contour relief");
    // Compare a rotated/mirrored scalar field, independent of directional light.
    std::vector<double> mirror(pixels),rotate(pixels);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){mirror[y*width+x]=convex[y*width+255-x];
        rotate[y*width+x]=convex[(239-y)*width+255-x];}
    auto nm=modern_normals(t,mirror),nt=modern_normals(t,rotate);
    for(int y=4;y<236;++y)for(int x=4;x<252;++x){int i=y*width+x,j=y*width+255-x,k=(239-y)*width+255-x;
        check(near(nm[i].x,-nc[j].x)&&near(nm[i].y,nc[j].y),"mirror geometry symmetry");
        check(near(nt[i].x,-nc[k].x)&&near(nt[i].y,-nc[k].y),"half-turn geometry symmetry");}
    // Grayscale speckle in an actor cannot define its normals. The protected
    // color islands use only the large semantic-owner silhouette envelope.
    raw=blank();rect(raw,45,40,109,104,0x30);sprite(raw,45,40,109,104);
    for(int y=40;y<104;++y)for(int x=45;x<109;++x)paint(raw,x,y,((x+y)&1)?0x10:0x20);
    auto a=modern_plan(*observe(raw));
    for(int y=40;y<104;++y)for(int x=45;x<109;++x)paint(raw,x,y,((x+y)&1)?0x20:0x10);
    auto b=modern_plan(*observe(raw));
    for(int y=40;y<104;++y)for(int x=45;x<109;++x){int i=y*width+x;
        check(a.cells[i].normal==b.cells[i].normal&&a.cells[i].relief==b.cells[i].relief,"no grayscale micro-noise geometry");}
    rejects([&]{modern_normals(t,std::span<const double>{});});
}
void shadows(){
    auto raw=blank(0x30);rect(raw,80,70,112,105,0x28);sprite(raw,80,70,112,105);
    auto p=modern_plan(*observe(raw));bool cast=false,contact=false;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto&c=p.cells[y*width+x];
        if(c.actor)check(c.cast_shadow==0&&c.contact==0,"lower world cannot shadow higher actor");
        if(c.cast_shadow>0){cast=true;check(x>=80&&x<=114&&y>=70&&y<=107&&!c.actor,"shadow only down-right within 3 pixels");}
        if(c.contact>0){contact=true;check(!c.actor&&x>=79&&x<=112&&y>=69&&y<=105,"compact actor contact");}
    }
    check(cast&&contact,"actor grounding active");
    check(p.cells[69*width+79].cast_shadow==0,"no cast toward light");
    for(auto&s:raw.samples)if(s.layer==Layer::sprite)s.sprite_behind=true;
    auto behind=modern_plan(*observe(raw));check(behind.cells[80*width+90].depth<p.cells[80*width+90].depth,"priority-derived depth");
    for(const auto&c:behind.cells)if(c.world)check(c.cast_shadow==0&&c.contact==0,"behind-priority actor cannot shadow foreground background");
}
void assets_and_translation(){
    auto raw=blank();rect(raw,30,40,62,72,0x30);rect(raw,110,40,142,72,0x30);
    sprite(raw,30,40,62,72,2);sprite(raw,110,40,142,72,3);
    auto f=observe(raw);auto p=modern_plan(*f);const auto a=p.cells[50*width+40].context,b=p.cells[50*width+120].context;
    check(a.asset_key==b.asset_key&&a.surface_key==b.surface_key&&a!=b,"same asset and surface distinct OAM contexts");
    ModernAssetRegistry registry;check(registry.lookup(a).status==ModernLookup::missing,"missing fallback");
    RemasterAsset asset;asset.context=a;asset.albedo=true;asset.texels[0].albedo={.7,.75,.8};
    registry.add(asset);check(registry.lookup(a).status==ModernLookup::found&&registry.lookup(b).status==ModernLookup::missing,"exact contextual registry authority");
    auto fallback=render_modern(*f,{},256,240),replaced=render_modern(*f,{},256,240,&registry);
    int changed=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){if(x>=30&&x<62&&y>=40&&y<72)changed+=rgb(fallback,x,y)!=rgb(replaced,x,y);
        else check(rgb(fallback,x,y)==rgb(replaced,x,y),"replacement cannot leak into repeated asset or foreign ownership");}
    check(changed>300,"replacement seam visibly exercised");
    check(render_modern(*f,{0,0},768,720,&registry)==render(*f,Mode::SEMANTIC_HD),"zero controls override registry");
    registry.add(asset);check(registry.lookup(a).status==ModernLookup::ambiguous,"duplicate authorization ambiguous");
    check(render_modern(*f,{},256,240,&registry)==fallback,"ambiguous registry uses procedural fallback");
    ModernAssetRegistry masked;asset.texels[0].appearance_mask=0;masked.add(asset);
    check(render_modern(*f,{},256,240,&masked)==fallback,"zero replacement mask retains canonical visibility and fallback");
    // All future asset channels are bounded appearance, including the would-be
    // alpha/silhouette channel, which can only blend back to the procedural art.
    asset.texels[0].appearance_mask=1;asset.albedo=false;asset.normal=true;asset.roughness=true;
    asset.height_channel=true;asset.emissive=true;asset.texels[0].normal={.7,.2,.68};
    asset.texels[0].height=.95;asset.texels[0].roughness=.4;asset.texels[0].emissive=.07;
    ModernAssetRegistry channels;channels.add(asset);
    check(render_modern(*f,{0,1},256,240,&channels)==render_modern(*f,{0,1},256,240),"tone-only disables authored high-frequency channels");
    auto channel_image=render_modern(*f,{},256,240,&channels);
    check(channel_image!=fallback,"normal roughness height emissive seam active");
    check(topology(channel_image)==topology(fallback),"authored appearance preserves silhouette components and holes");
    for(int y=40;y<72;++y)for(int x=110;x<142;++x)check(rgb(channel_image,x,y)==rgb(fallback,x,y),"all authored channels context isolated");
    auto invalid=asset;invalid.width=65;rejects([&]{ModernAssetRegistry r;r.add(invalid);});
    invalid=asset;invalid.texels[0].height=std::numeric_limits<double>::infinity();rejects([&]{ModernAssetRegistry r;r.add(invalid);});
    invalid=asset;invalid.texels[0].albedo[0]=2;rejects([&]{ModernAssetRegistry r;r.add(invalid);});
    // Translating the same sprite changes placement but retains source-local UV,
    // asset context and material detail. No screen coordinate enters lookup.
    auto moved=blank();rect(moved,47,53,79,85,0x30);sprite(moved,47,53,79,85,2);
    auto pm=modern_plan(*observe(moved));
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){auto&c=p.cells[(40+y)*width+30+x];auto&d=pm.cells[(53+y)*width+47+x];
        check(c.context==d.context,"sprite registry context stable under translation");
        check(c.detail==d.detail,"asset-local microdetail stable under translation");
        check(c.relief==d.relief&&c.normal==d.normal,"translated sprite geometry identical");
    }
    // Coherent PPU world provenance under a non-tile-aligned screen scroll.
    auto world=[](int scroll){auto f=blank(0x28);
        for(auto&s:f.samples){int wx=s.x+scroll,wy=s.y;auto&v=s.background;
            v.nametable=std::uint16_t(0x2000+(wy/8)*32+(wx/8));v.attribute=std::uint16_t(0x23c0|((v.nametable>>4)&0x38)|((v.nametable>>2)&7));
            v.origin_x=std::int16_t(wx/8*8);v.origin_y=std::int16_t(wy/8*8);v.local_x=std::uint8_t(wx%8);v.local_y=std::uint8_t(wy%8);
            v.scroll_v=std::uint16_t((v.nametable&0xfff)|(v.local_y<<12));}
        return f;
    };
    auto w0=modern_plan(*observe(world(0))),w3=modern_plan(*observe(world(3)));
    for(int y=10;y<220;++y)for(int x=12;x<236;++x){const auto&c=w0.cells[y*width+x+3];const auto&d=w3.cells[y*width+x];
        check(c.context==d.context&&c.detail==d.detail,"world/AssetKey detail and registry stable under scroll");}
    for(int y=-8;y<40;++y)for(int x=-8;x<40;++x){auto value=modern_detail(a.asset_key,a.motif_key,x*.25,y*.25);
        check(value>=-.5&&value<=.5,"source field bounded including negative UV");
        check(value==modern_detail(a.asset_key,a.motif_key,x*.25,y*.25),"source field repeat");}
    rejects([&]{modern_detail(0,0,std::numeric_limits<double>::quiet_NaN(),0);});
    rejects([&]{modern_detail(0,0,0,std::numeric_limits<double>::infinity());});
    rejects([&]{modern_detail(0,0,65537,0);});
}
}
int main(){
    try{frozen_machine();plan_and_extents();classification();boundary_isolation();geometry();shadows();assets_and_translation();}
    catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    check(checks>=100000,"minimum independent check count");
    for(const auto&[name,n]:failure_names)std::cerr<<name<<": "<<n<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
