#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>

using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,std::uint64_t> failed;
void check(bool b,const char* name){++checks;if(!b){++failures;++failed[name];}}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad,"invalid input rejected");}
bool near(double a,double b){return std::abs(a-b)<1e-10;}
void paint(Frame& f,int x,int y,int v){f.samples[y*width+x].color=f.indices[y*width+x]=std::uint8_t(v);}
Frame world(int scroll=0,int color=0x27){
    Frame f;f.number=1;
    for(int i=0;i<pixels;++i){auto& s=f.samples[i];s.x=i%width;s.y=i/width;s.dot=s.x+1;
        s.layer=Layer::background;s.bg_opaque=true;auto& v=s.background;int x=s.x+scroll,y=s.y;
        v.nametable=std::uint16_t(0x2000+(y/8)*32+x/8);v.attribute=std::uint16_t(0x23c0|((v.nametable>>4)&0x38)|((v.nametable>>2)&7));
        v.pattern=16;v.tile=1;v.origin_x=std::int16_t(x/8*8);v.origin_y=std::int16_t(y/8*8);
        v.local_x=std::uint8_t(x%8);v.local_y=std::uint8_t(y%8);v.scroll_v=std::uint16_t((v.nametable&0xfff)|(v.local_y<<12));
        paint(f,s.x,s.y,color);
    }return f;
}
void actor(Frame& f,int x0,int y0,int w=32,int h=40,int flip=0,bool noisy=false){
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){int sx=(flip&1)?w-1-x:x,sy=(flip&2)?h-1-y:y;
        double dx=(sx+.5-w*.5)/(w*.5),dy=(sy+.5-h*.5)/(h*.5);if(dx*dx+dy*dy>1)continue;
        auto& s=f.samples[(y0+y)*width+x0+x];s.layer=Layer::sprite;s.sprite_opaque=true;
        s.sprite.pattern=32;s.sprite.tile=2;s.sprite.origin_x=std::int16_t(x0);s.sprite.origin_y=std::int16_t(y0);
        s.sprite.oam_slot=2;s.sprite.local_x=std::uint8_t(sx%8);s.sprite.local_y=std::uint8_t(sy%8);
        s.sprite.flags=std::uint8_t(((flip&1)?64:0)|((flip&2)?128:0));
        paint(f,s.x,s.y,noisy?((sx+sy)&1?0x10:0x20):0x27);
    }
}
Frame fixture(){
    auto f=world(0,0x20);actor(f,38,52);
    for(int y=25;y<210;++y)for(int x=100;x<239;++x){int dx=x-150,dy=y-72;
        if(dx*dx+dy*dy<1600)paint(f,x,y,0x1a);
        if(y>145&&x<165)paint(f,x,y,0x17);
        if(x>200&&y>75&&y<180)paint(f,x,y,0x28);
    }
    for(int y=18;y<35;++y)paint(f,80,y,0x0f);
    for(int x=80;x<93;++x)paint(f,x,18,0x0f);
    for(int y=0;y<12;++y)for(int x=0;x<width;++x){auto& s=f.samples[y*width+x];s.layer=Layer::backdrop;s.bg_opaque=false;paint(f,x,y,0x31);}
    return f;
}
void preparation_and_raster(){
    auto f=observe(fixture());const auto hash=frame_hash(*f);
    auto p=prepare_modern(*f),q=prepare_modern(*f);const auto ir=semantic_contours(*f);
    check(p.plan().cells==q.plan().cells,"prepared plan exact repeat");
    check(p.plan().contours.topology.adjacency==ir.topology.adjacency,"style cannot change adjacency");
    std::size_t contours=0,interiors=0,ui=0;
    for(int i=0;i<pixels;++i){const auto& a=p.plan().cells[i];const auto& b=q.plan().cells[i];
        check(p.paint()[i]==q.paint()[i],"prepared coefficients deterministic");
        check(a.region==ir.topology.components.labels[i]&&a.owner==b.owner,"immutable region and owner");
        check(a.depth==b.depth&&a.context==b.context,"immutable priority and context");
        check(p.paint()[i].form.x*p.paint()[i].form.x+p.paint()[i].form.y*p.paint()[i].form.y<=1.000001,"bounded broad form normal");
        for(int e=0;e<4;++e){const auto& c=p.plan().contours.cells[i];const auto& d=ir.cells[i];
            check(c.cuts[e].donor==d.cuts[e].donor&&c.cuts[e].reach_q8==d.cuts[e].reach_q8&&c.edge_donors[e]==d.edge_donors[e],"only frozen coverage donors");}
        contours+=p.paint()[i].contour;interiors+=!p.paint()[i].contour;ui+=a.ui;
    }
    check(contours>20&&interiors>1000&&ui>10,"contour interior and UI support exercised");
    for(auto [w,h]:std::array<std::pair<int,int>,5>{{{256,240},{512,480},{768,720},{901,803},{1024,960}}}){
        const auto fast=raster_modern(p,w,h),reference=render_modern_reference(*f,{},w,h);
        const auto hd=render(*f,Mode::SEMANTIC_HD,w,h),zero=render_modern(*f,{0,0},w,h);
        check(zero==hd,"zero controls literal HD identity");
        auto prepared_zero=prepare_modern(*f,{0,0});check(raster_modern(prepared_zero,w,h)==hd,"prepared zero exact legacy raster");
        check(fast==render_modern(*f,{},w,h),"public seam uses prepared path");
        Image reused;raster_modern_into(reused,p,w,h);check(reused==fast,"caller-owned buffer exact repeat");
        for(std::size_t at=0;at<fast.rgb.size();++at)check(fast.rgb[at]==reference.rgb[at],"every fast and contour byte equals scalar reference");
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int i=int(std::int64_t(2*y+1)*height/(2*h))*width+int(std::int64_t(2*x+1)*width/(2*w));
            if(p.plan().cells[i].ui)for(int k=0;k<3;++k)check(fast.rgb[(std::size_t(y)*w+x)*3+k]==hd.rgb[(std::size_t(y)*w+x)*3+k],"UI exact including contour seams");
        }
    }
    check(frame_hash(*f)==hash,"preparation and raster never modify observation");
    rejects([]{raster_modern(PreparedModernFrame{});});rejects([&]{raster_modern(p,255,240);});
    rejects([&]{prepare_modern(*f,{1,std::numeric_limits<double>::quiet_NaN()});});
}
void pigment_and_form(){
    const auto p=prepare_modern(*observe(fixture()));
    const ModernMaterial media[]{ModernMaterial::SOIL,ModernMaterial::STONE,ModernMaterial::ORGANIC,
        ModernMaterial::FOLIAGE,ModernMaterial::WOOD,ModernMaterial::ACTOR};
    // One input swatch, six distinct broad pigment responses. A global scalar
    // or global RGB grade cannot produce six results for the same source RGB.
    const std::array<std::uint8_t,3> source{126,160,74};std::set<std::array<int,3>> outputs;
    for(auto m:media){auto a=modern_illustrated_albedo(source,m,0);std::array<int,3> q{};
        for(int k=0;k<3;++k)q[k]=int(a[k]*255);outputs.insert(q);
        check(std::abs(a[0]/source[0]-a[1]/source[1])>1e-5,"pigment response is not source RGB times a scalar");}
    check(outputs.size()==6,"six distinguishable broad material responses");
    for(unsigned m=0;m<12;++m)for(int v=0;v<64;++v)for(int step=-2;step<=2;++step){
        const auto src=digital_color(std::uint8_t(v),0);auto a=modern_illustrated_albedo(src,ModernMaterial(m),step*.2);
        for(int k=0;k<3;++k){check(a[k]>=0&&a[k]<=1,"pigment gamut bounded");
            for(int j=0;j<3;++j){if(src[k]==src[j])check(a[k]==a[j],"neutral and equal channels retained");
                else check((a[k]>a[j])==(src[k]>src[j]),"palette hue family ordering retained");}}
        if(src[0]==0&&src[1]==0&&src[2]==0)check(a==std::array<float,3>{},"categorical ink stays ink");
    }
    auto raw=world();actor(raw,48,64);auto noisy=world();actor(noisy,48,64,32,40,0,true);
    auto a=prepare_modern(*observe(raw)),b=prepare_modern(*observe(noisy));
    for(int i=0;i<pixels;++i)if(a.plan().cells[i].actor)
        check(a.paint()[i].form==b.paint()[i].form,"grayscale microdetail never generates geometry");
    auto flat=prepare_modern(*observe(world(0,0x17)));
    for(int y=32;y<height-32;++y)for(int x=32;x<width-32;++x){int i=y*width+x;
        check(flat.paint()[i].form==ModernNormal{},"large flat owner has no geometric dome or tiled normals");}
    auto same=world(0,0x20);actor(same,40,48,48,56);
    for(auto& s:same.samples)if(s.layer==Layer::sprite)paint(same,s.x,s.y,0x20);
    auto painted=prepare_modern(*observe(same));
    auto im=raster_modern(painted,256,240);auto bg=im.rgb[(80*width+180)*3+1],act=im.rgb[(80*width+64)*3+1];
    check(std::abs(int(bg)-int(act))>=6,"actual renderer separates same source color by material not global grade");
    bool active=false;
    for(int i=0;i<pixels;++i){const auto& c=p.plan().cells[i];
        for(int k=0;k<3;++k)active|=c.actor&&std::abs(p.paint()[i].delta[k])>5;}
    check(active,"actor receives visible illustrated response");
}
void source_lock_and_shadows(){
    auto first=world();actor(first,40,48);auto a=prepare_modern(*observe(first));
    for(int flip=0;flip<4;++flip){auto moved=world();actor(moved,117,73,32,40,flip);auto b=prepare_modern(*observe(moved));
        for(int y=0;y<40;++y)for(int x=0;x<32;++x){int sx=flip&1?31-x:x,sy=flip&2?39-y:y;
            int i=(48+sy)*width+40+sx,j=(73+y)*width+117+x;if(!a.plan().cells[i].actor)continue;
            check(a.paint()[i].albedo==b.paint()[j].albedo&&a.paint()[i].pattern==b.paint()[j].pattern,"actor source pigment equivariant under translation and flips");
            const auto& n=a.paint()[i].form;const auto& m=b.paint()[j].form;
            check(near(n.x,m.x*(flip&1?-1:1))&&near(n.y,m.y*(flip&2?-1:1))&&near(n.z,m.z),"broad form transforms with actor while light stays global");
            if(!flip)check(a.paint()[i].delta==b.paint()[j].delta,"translated actor native shading exact");
        }
    }
    auto w0=prepare_modern(*observe(world(0,0x17))),w3=prepare_modern(*observe(world(3,0x17)));
    for(int y=8;y<230;++y)for(int x=8;x<236;++x){int i=y*width+x+3,j=y*width+x;
        check(w0.paint()[i].albedo==w3.paint()[j].albedo,"world scrolling cannot move pigment field");
        check(w0.paint()[i].pattern==w3.paint()[j].pattern,"world pattern does not crawl");}
    auto braw=first;for(auto& s:braw.samples)if(s.layer==Layer::sprite)s.sprite_behind=true;
    auto b=prepare_modern(*observe(braw));auto front=raster_modern(a,256,240),behind=raster_modern(b,256,240);
    bool support=false;
    for(int i=0;i<pixels;++i){const auto& c=a.plan().cells[i];
        check(c.contact+c.cast_shadow<=.083,"support shadow bounded");
        if(c.contact+c.cast_shadow>0){support=true;check(i/width>=81&&i/width<=90&&c.world,"only lower world support shadow");}
        if(i/width<81||i%width<40)check(c.contact+c.cast_shadow==0,"no upper or side halo");
        if(c.world)for(int k=0;k<3;++k)check(front.rgb[i*3+k]>=behind.rgb[i*3+k]*.89-1,"shadow cannot turn receiver black");
    }
    check(support,"support shadow exercised");
}
void invalidation_and_authority(){
    auto raw=fixture();auto f=observe(raw);auto p=prepare_modern(*f);auto old=raster_modern(p,512,480);
    for(int variant=0;variant<6;++variant){auto next=raw;
        for(auto& s:next.samples){
            if(variant==0&&s.x>128)paint(next,s.x,s.y,0x12);
            if(variant==1&&s.layer==Layer::sprite)s.sprite_behind=true;
            if(variant==2)++s.background.chr_epoch;
            if(variant==3){s.emphasis=1;next.effects[s.y*width+s.x]=32;}
            if(variant==4&&s.x>128)s.background.nametable=1;
            if(variant==5&&s.layer==Layer::sprite){s.layer=Layer::background;s.sprite_opaque=false;}
        }
        auto frame=observe(next);prepare_modern_into(p,*frame);const auto fresh=prepare_modern(*frame);
        for(int i=0;i<pixels;++i){check(p.paint()[i]==fresh.paint()[i],"every reused cache coefficient invalidates");
            check(p.plan().cells[i]==fresh.plan().cells[i],"all owner material support and source fields invalidate");}
        check(raster_modern(p,512,480)==raster_modern(fresh,512,480),"changed immutable frame equals fresh preparation");
    }
    prepare_modern_into(p,*f);check(raster_modern(p,512,480)==old,"return to original frame has no history");
    auto temporal=*f;temporal.number=998;
    for(auto& s:temporal.samples){s.confidence=255;s.reactive=Reactive::stable;s.motion_x=17;s.motion_y=-9;s.previous_surface=123;}
    auto independent=prepare_modern(temporal);check(raster_modern(independent,512,480)==old,"no temporal or motion authority");
    ModernAssetRegistry registry;RemasterAsset asset;asset.context=p.plan().cells[70*width+50].context;
    asset.albedo=true;asset.texels[0].albedo={.7,.75,.8};registry.add(asset);
    prepare_modern_into(p,*f,{},&registry);auto authored=raster_modern(p,512,480);
    check(authored!=old,"authored context exercised");registry.add(asset);
    // Prepared coefficients own their appearance; mutation of a later registry
    // cannot retroactively alter them. Repreparing must resolve ambiguity anew.
    check(raster_modern(p,512,480)==authored,"prepared snapshot owns registry-derived values");
    prepare_modern_into(p,*f,{},&registry);check(raster_modern(p,512,480)==old,"registry ambiguity invalidates exact cache");
    prepare_modern_into(p,*f,{0,0});check(raster_modern(p,512,480)==render(*f,Mode::SEMANTIC_HD,512,480),"controls invalidate prepared response");
    Machine machine(synthetic_nrom());machine.run_frames(1);auto observation=observe(*machine.frame());
    const auto state=machine.serialize();const auto pcm=machine.apu.pcm;const auto hash=frame_hash(*observation);
    (void)render_modern(*observation,{},512,480);
    check(state==machine.serialize()&&pcm==machine.apu.pcm&&hash==frame_hash(*observation),"canonical state frame and audio invariant");
    rejects([&]{render(*observation,Mode::TEMPORAL_RECONSTRUCTION);});rejects([&]{render(*observation,Mode::NEURAL_RESIDUAL_EXPERIMENTAL);});
    check(!NeuralResidualPolicy{}.enabled&&!NeuralResidualPolicy{}.geometry_authority,"neural has no authority");
}
}
int main(){
    try{preparation_and_raster();pigment_and_form();source_lock_and_shadows();invalidation_and_authority();}
    catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    check(checks>=2000000,"at least two million independent pixel and invariant checks");
    for(const auto& [name,count]:failed)std::cerr<<name<<": "<<count<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
