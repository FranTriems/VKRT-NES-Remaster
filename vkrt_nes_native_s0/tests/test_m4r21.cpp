#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>

using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,std::uint64_t> failed;
void check(bool value,const char* name){++checks;if(!value){++failures;++failed[name];}}
bool near(double a,double b,double epsilon=1e-11){return std::abs(a-b)<epsilon;}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad,"invalid input rejected");}
double dot(ModernNormal a,ModernNormal b){return a.x*b.x+a.y*b.y+a.z*b.z;}
std::array<int,3> rgb(const Image&im,int i){return {im.rgb[i*3],im.rgb[i*3+1],im.rgb[i*3+2]};}
void paint(Frame&f,int x,int y,int color){f.samples[y*width+x].color=f.indices[y*width+x]=std::uint8_t(color);}
Frame blank(int color=0x20){
    Frame f;f.number=1;
    for(int i=0;i<pixels;++i){auto&s=f.samples[i];s.x=i%width;s.y=i/width;s.dot=s.x+1;
        s.layer=Layer::background;s.bg_opaque=true;s.background.pattern=16;s.background.tile=1;
        s.background.local_x=s.x%8;s.background.local_y=s.y%8;paint(f,s.x,s.y,color);}
    return f;
}
void rectangle(Frame&f,int x0,int y0,int w,int h,int color){
    for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x)paint(f,x,y,color);
}
void actor(Frame&f,int x0,int y0,int w,int h,int color=0x27,bool ellipse=false){
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        double xx=(x+.5-w*.5)/(w*.5),yy=(y+.5-h*.5)/(h*.5);
        if(ellipse&&xx*xx+yy*yy>1)continue;
        auto&s=f.samples[(y+y0)*width+x+x0];s.layer=Layer::sprite;s.sprite_opaque=true;
        s.sprite.pattern=32;s.sprite.tile=2;s.sprite.oam_slot=2;
        s.sprite.origin_x=std::int16_t(x0);s.sprite.origin_y=std::int16_t(y0);
        s.sprite.local_x=std::uint8_t(x%8);s.sprite.local_y=std::uint8_t(y%8);paint(f,x+x0,y+y0,color);
    }
}
Frame fixture(){
    auto f=blank();actor(f,60,65,40,56,0x27,true);
    for(int y=65;y<121;++y)for(int x=60;x<100;++x)
        if(f.samples[y*width+x].layer==Layer::sprite&&y>94)paint(f,x,y,0x16);
    rectangle(f,130,50,50,60,0x1a);rectangle(f,130,130,50,60,0x17);
    rectangle(f,208,48,1,23,0x0f);rectangle(f,209,48,13,1,0x0f);
    for(int y=0;y<24;++y)for(int x=0;x<width;++x){auto&s=f.samples[y*width+x];
        s.layer=Layer::backdrop;s.bg_opaque=false;paint(f,x,y,0x31);}
    return f;
}
void legacy_and_extents(){
    for(int ordinal=0;ordinal<6;++ordinal){const Mode modes[]{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,
        Mode::SEMANTIC_HD,Mode::TEMPORAL_RECONSTRUCTION,Mode::NEURAL_RESIDUAL_EXPERIMENTAL,Mode::MODERN_REMASTER};
        check(int(modes[ordinal])==ordinal,"presentation ordinal preserved");}
    Machine machine(synthetic_nrom());ImmutableFrame observation;
    for(unsigned frame=0;frame<8;++frame){machine.controllers[0].buttons=frame%3==0?1:0;machine.run_frames(1);
        observation=observe(*machine.frame(),observation.get());}
    const char* goldens[]{"2bba2b84eb67008fa75e93a0eb31718b3280f2d0fcea837546aa874f8c82170c",
        "f577b82ed9fe7082a0ce2f0c4d8ba5435024030da7ba3675c2bbb3d065387eb3",
        "cb937d4f0153e689fb89c332616413ffb806cf0bf8d4e6a344347274644bd9bd"};
    for(int mode=0;mode<3;++mode)check(sha256(render(*observation,Mode(mode)).rgb)==goldens[mode],"independent historical RGB receipt");
    const auto state=machine.serialize();const auto pcm=machine.apu.pcm;const auto hash=frame_hash(*observation);
    check(render(*observation,Mode::MODERN_REMASTER)==render_modern(*observation),"M4-R2 dispatch seam");
    check(state==machine.serialize()&&pcm==machine.apu.pcm&&hash==frame_hash(*observation),"machine audio observation immutable");
    rejects([&]{render(*observation,Mode::TEMPORAL_RECONSTRUCTION);});
    rejects([&]{render(*observation,Mode::NEURAL_RESIDUAL_EXPERIMENTAL);});
    check(!NeuralResidualPolicy{}.enabled,"neural runtime disabled");
    auto f=observe(fixture());auto p=modern_plan(*f),q=modern_plan(*f);auto contours=semantic_contours(*f);
    check(p.cells==q.cells,"entire modern plan deterministic");
    check(p.contours.topology.adjacency==contours.topology.adjacency,"topological adjacency immutable");
    check(p.contours.diagonals==contours.diagonals,"saddles immutable");
    for(int i=0;i<pixels;++i){const auto&c=p.cells[i];auto profile=modern_material_profile(c.material,c.confidence);
        check(c.region==contours.topology.components.labels[i],"region labels immutable");
        check(c.relief>=0&&c.relief<=1,"bounded shape relief");
        check(near(dot(c.normal,c.normal),1),"unit normal");
        check(c.roughness==profile.roughness&&c.specular==profile.specular_budget,"cell follows material profile");
        check(c.ao>=0&&c.ao<=.06,"restrained AO");
        check(c.contact>=0&&c.contact<=.035&&c.cast_shadow>=0&&c.cast_shadow<=.048,"compact shadow budgets");
        check(c.detail>=-.5&&c.detail<=.5,"low frequency detail bound");
        check(c.local_tone>=-.012&&c.local_tone<=.012,"local luminance budget");
        check(c.context.asset_key==f->samples[i].asset_key&&c.context.surface_key==f->samples[i].surface_key,"AssetKey semantics preserved");
        check(c.structure_mask>=0&&c.structure_mask<=1&&c.tone_mask>=0&&c.tone_mask<=1,"control masks bounded");
        if(c.ui)check(c.structure_mask==0&&c.tone_mask==0&&c.local_tone==0,"UI exact flat");
        if(f->samples[i].layer==Layer::backdrop)check(c.relief==0&&c.normal==ModernNormal{}&&c.structure_mask==0
            &&c.detail==0&&c.tone_mask<=.12,"backdrop has ambient only");
        for(int e=0;e<4;++e){const auto&a=p.contours.cells[i];const auto&b=contours.cells[i];
            check(a.cuts[e].donor==b.cuts[e].donor&&a.cuts[e].reach_q8==b.cuts[e].reach_q8
                &&a.edge_donors[e]==b.edge_donors[e],"no coverage or categorical topology modification");}
    }
    for(auto [w,h]:std::array<std::pair<int,int>,5>{{{256,240},{768,720},{1024,960},{1280,1200},{901,803}}}){
        auto hd=render(*f,Mode::SEMANTIC_HD,w,h),modern=render_modern(*f,{},w,h);
        check(render_modern(*f,{0,0},w,h)==hd,"zero controls whole-image identity");
        check(modern==render_modern(*f,{},w,h),"repeat at every supported extent");
        check(modern.width==w&&modern.height==h&&modern!=hd,"modern extent and activity");
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int i=int(std::int64_t(2*y+1)*height/(2*h))*width+int(std::int64_t(2*x+1)*width/(2*w));
            if(p.cells[i].ui)check(rgb(modern,y*w+x)==rgb(hd,y*w+x),"protected glyph crisp at output extent");}
    }
    auto hd=render(*f,Mode::SEMANTIC_HD,256,240),structure=render_modern(*f,{1,0},256,240),full=render_modern(*f,{},256,240);
    bool atmosphere=false;
    for(int i=width;i<22*width;++i){check(rgb(structure,i)==rgb(hd,i),"backdrop structure control disabled");
        for(int k=0;k<3;++k){check(std::abs(int(full.rgb[i*3+k])-hd.rgb[i*3+k])<=2,"subtle backdrop tone bound");
            atmosphere|=full.rgb[i*3+k]!=hd.rgb[i*3+k];}}
    check(atmosphere,"ambient backdrop response active");
}
void lighting_and_profiles(){
    const auto&rig=modern_lighting_rig();
    check(near(dot(rig.key,rig.key),1)&&near(dot(rig.fill,rig.fill),1),"global directions normalized");
    check(near(rig.cast_x,-rig.key.x/std::abs(rig.key.y))&&near(rig.cast_y,-rig.key.y/std::abs(rig.key.y)),"cast derived from key");
    check(rig.ambient_floor>=.7&&rig.ambient_floor<1,"ambient floor bounded");
    std::set<int> roughnesses;
    for(unsigned m=0;m<12;++m){auto material=ModernMaterial(m);auto profile=modern_material_profile(material);roughnesses.insert(int(profile.roughness*1000));
        check(profile.specular_budget>=0&&profile.specular_budget<=.12,"bounded specular art budget");
        if(m<8||m==11)check(profile.specular_budget<=.006&&profile.roughness>=.94,"ordinary materials matte");
        if(material==ModernMaterial::ACTOR)check(profile.specular_budget<=.002&&profile.rim_budget<=.007,"actor restrained highlight budgets");
        if(material==ModernMaterial::UI_FLAT||material==ModernMaterial::UNKNOWN_SAFE||material==ModernMaterial::OBJECT_FLAT)
            check(profile.specular_budget==0,"safe flat has no procedural specular");
        if(m>=8&&m<=10){auto low=modern_material_profile(material,.89);check(low.specular_budget==0&&low.relief_gain<=.16,"unconfident specialized material falls back");}
    }
    check(roughnesses.size()>=4,"distinct material roughness profiles");
    bool distinct_response=false;
    for(int z=1;z<=5;++z)for(int y=-20;y<=20;++y)for(int x=-20;x<=20;++x){
        ModernNormal n{x/20.,y/20.,z/5.};double inv=1/std::sqrt(dot(n,n));n={n.x*inv,n.y*inv,n.z*inv};
        const auto reference=modern_light(n,ModernMaterial::ACTOR);
        for(unsigned m=0;m<12;++m){auto material=ModernMaterial(m);auto p=modern_material_profile(material);auto light=modern_light(n,material);
            check(near(light.key,dot(n,rig.key))&&near(light.fill,dot(n,rig.fill)),"same normal same global light directions");
            check(light.key==reference.key&&light.fill==reference.fill,"materials change response not direction");
            check(light.diffuse>=rig.ambient_floor&&light.diffuse<=1.30,"bounded rough diffuse");
            check(light.specular>=0&&light.specular<=p.specular_budget&&light.rim>=0&&light.rim<=p.rim_budget,"per-normal highlight budget");
            check(light==modern_light(n,material),"lighting deterministic");
            distinct_response|=!near(light.diffuse,reference.diffuse);
        }
    }
    check(distinct_response,"material parameters alter diffuse response");
    Region r;r.layer=Layer::background;r.area=1000;r.perimeter=140;r.max_x=99;r.max_y=9;
    for(int red=0;red<256;red+=13)for(int green=0;green<256;green+=11)for(int blue=0;blue<256;blue+=9){
        auto c=modern_classify({std::uint8_t(red),std::uint8_t(green),std::uint8_t(blue)},r,false,false);
        check(c.material!=ModernMaterial::METAL&&c.material!=ModernMaterial::WATER&&c.material!=ModernMaterial::EMISSIVE,"color and aspect cannot authorize shiny material");
    }
    rejects([]{modern_light({0,0,0},ModernMaterial::ACTOR);});
    rejects([]{modern_light({},ModernMaterial::ACTOR,1,.1);});
    rejects([]{modern_material_profile(ModernMaterial(12));});
}
void actor_forms(){
    auto raw=blank();actor(raw,48,56,48,64,0x27,true);const auto solid=modern_plan(*observe(raw));
    auto noisy=raw;
    for(auto&s:noisy.samples)if(s.layer==Layer::sprite)paint(noisy,s.x,s.y,((s.x+s.y)&1)?0x10:0x20);
    auto checker=modern_plan(*observe(noisy));
    for(auto&s:noisy.samples)if(s.layer==Layer::sprite)paint(noisy,s.x,s.y,((s.x+s.y)&1)?0x20:0x10);
    auto inverted=modern_plan(*observe(noisy));
    for(int i=0;i<pixels;++i)if(solid.cells[i].actor){
        check(solid.cells[i].relief==checker.cells[i].relief&&solid.cells[i].normal==checker.cells[i].normal,"checker adds no geometry even versus solid");
        check(checker.cells[i].relief==inverted.cells[i].relief&&checker.cells[i].normal==inverted.cells[i].normal,"grayscale noise cannot alter normals");
        check(checker.cells[i].structure_mask<=.18&&checker.cells[i].macro_ink==0,"fine actor features attenuated without outline emboss");
    }
    check(solid.cells[88*width+72].relief>solid.cells[88*width+50].relief+.25,"broad convex actor form");
    check(solid.cells[88*width+54].normal.x<-.05&&solid.cells[88*width+89].normal.x>.05,"actor outward normals");
    auto regions=raw;for(auto&s:regions.samples)if(s.layer==Layer::sprite&&s.y>=88)paint(regions,s.x,s.y,0x16);
    auto regional=modern_plan(*observe(regions));bool internal=false;
    for(int i=0;i<pixels;++i){check(regional.cells[i].owner==solid.cells[i].owner,"internal relief does not change owner topology");
        if(solid.cells[i].actor&&solid.cells[i].owner_distance>5)internal|=regional.cells[i].relief>solid.cells[i].relief+.02;}
    check(internal,"safe semantic internal region contributes large form");
    // Translation and both flips, including source-local texel coordinates.
    for(int flip=0;flip<4;++flip){auto moved=blank();
        for(int y=0;y<64;++y)for(int x=0;x<48;++x){int sx=flip&1?47-x:x,sy=flip&2?63-y:y;
            const auto&s=regions.samples[(56+sy)*width+48+sx];if(s.layer!=Layer::sprite)continue;
            auto&d=moved.samples[(73+y)*width+119+x];d=s;d.x=119+x;d.y=73+y;d.dot=d.x+1;
            d.sprite.origin_x=119;d.sprite.origin_y=73;d.sprite.flags=std::uint8_t((flip&1?64:0)|(flip&2?128:0));
            paint(moved,d.x,d.y,s.color);
        }
        auto p=modern_plan(*observe(moved));
        for(int y=0;y<64;++y)for(int x=0;x<48;++x){int sx=flip&1?47-x:x,sy=flip&2?63-y:y;
            const auto&a=regional.cells[(56+sy)*width+48+sx];const auto&b=p.cells[(73+y)*width+119+x];if(!a.actor)continue;
            check(a.detail==b.detail&&a.detail_key==b.detail_key&&a.detail_motif==b.detail_motif,"source-local material follows flip and translation");
            check(near(a.relief,b.relief),"actor relief equivariant under flip and translation");
            check(near(a.normal.x,b.normal.x*(flip&1?-1:1))&&near(a.normal.y,b.normal.y*(flip&2?-1:1))&&near(a.normal.z,b.normal.z),"geometry normal transforms but light stays global");
            check(a.material==b.material,"actor material identity survives transform");
            if(!flip)check(a.context==b.context,"translation preserves exact registry authority");
            else check(a.context!=b.context,"flip never widens registry context authority");
        }
    }
}
void support_shadows(){
    for(int size:{4,8,16,32}){auto raw=blank();actor(raw,80,70,size,size+8);
        auto front=modern_plan(*observe(raw));const int bottom=78+size;
        int footprint=0;double first=0,last=0;bool contact=false;
        for(int i=0;i<pixels;++i){const auto&c=front.cells[i];int x=i%width,y=i/width;
            check(c.contact+c.cast_shadow<=.083,"total shadow intensity cap");
            if(c.actor)check(c.contact==0&&c.cast_shadow==0,"no self shadow ring");
            if(c.contact+c.cast_shadow>0){++footprint;contact|=c.contact>0;
                check(y>=bottom&&y<=bottom+2&&x>=80&&x<=80+size+2,"only lower support casts away from light");
                check(c.world&&!c.ui,"world support required");
                if(y==bottom)first=std::max(first,c.cast_shadow);
                if(y==bottom+2)last=std::max(last,c.cast_shadow);
            }
            if(y<bottom||x<80)check(c.contact==0&&c.cast_shadow==0,"upper and side contours have no fake drop outline");
        }
        check(footprint>0&&footprint<=size*(size+8)/2,"shadow footprint bounded by actor footprint");
        check(contact&&first>last&&last>0,"soft compact penumbra decays");
        auto f=observe(raw);auto image=render_modern(*f,{},256,240);
        for(auto&s:raw.samples)if(s.layer==Layer::sprite)s.sprite_behind=true;
        auto b=observe(raw);auto behind=modern_plan(*b);auto unshadowed=render_modern(*b,{},256,240);
        for(int i=0;i<pixels;++i)if(front.cells[i].world){
            check(behind.cells[i].contact==0&&behind.cells[i].cast_shadow==0,"behind actor cannot shadow foreground");
            for(int k=0;k<3;++k)check(image.rgb[i*3+k]>=unshadowed.rgb[i*3+k]*.89-1,"colored ambient shadow cannot become black halo");
        }
    }
    auto blocked=blank();actor(blocked,80,70,8,16);
    for(auto&s:blocked.samples)if(s.x>=88&&s.x<90)s.background.nametable=1;
    auto p=modern_plan(*observe(blocked));
    for(int y=86;y<90;++y)for(int x=88;x<95;++x)
        check(p.cells[y*width+x].cast_shadow==0&&p.cells[y*width+x].contact==0,"penumbra cannot bridge receiver owners");
    for(auto&s:blocked.samples)if(s.layer==Layer::background){s.layer=Layer::backdrop;s.bg_opaque=false;}
    auto airborne=modern_plan(*observe(blocked));
    for(const auto&c:airborne.cells)check(c.contact==0&&c.cast_shadow==0,"no invented support on backdrop");
}
void owner_tone(){
    auto frame=observe(blank());const auto topology=semantic_topology(*frame);
    for(int variant=0;variant<5;++variant){std::vector<double> input(pixels);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){double v=.4;
            if(variant==1)v=x<128?.2:.8;
            if(variant==2)v=.2+.5*x/255.;
            if(variant==3)v=x<128?.4:.44;
            if(variant==4)v=.4+.012*std::sin(x*.35)+.008*std::cos(y*.31);
            input[y*width+x]=v;
        }
        auto output=modern_local_tone(topology,input),repeat=modern_local_tone(topology,input);
        check(output==repeat,"tone operator repeat");bool active=false;
        for(int y=1;y<height-1;++y)for(int x=1;x<width-1;++x){int i=y*width+x;
            auto lo=std::min({input[i],input[i-1],input[i+1],input[i-width],input[i+width]});
            auto hi=std::max({input[i],input[i-1],input[i+1],input[i-width],input[i+width]});
            check(output[i]>=lo-1e-12&&output[i]<=hi+1e-12,"tone convex envelope prevents overshoot");
            check(std::abs(output[i]-input[i])<=.012+1e-12,"tone bounded per sample");
            if(input[i]<input[i+1])check(output[i]<=output[i+1],"tone cannot reverse local gradient");
            if(input[i]>input[i+1])check(output[i]>=output[i+1],"tone cannot reverse descending gradient");
            if(variant<2||variant==3)check(output[i]==input[i],"constant and step fixtures have no halos");
            if(variant==2&&x>4&&x<width-5)check(near(output[i],input[i]),"linear ramp interior preserved");
            active|=!near(input[i],output[i]);
        }
        if(variant==4)check(active,"small interior luminance detail shaped");
    }
    for(int boundary=0;boundary<3;++boundary){auto raw=blank();
        for(auto&s:raw.samples)if(s.x>=128){
            if(boundary==0)s.background.nametable=1;
            if(boundary==1){s.emphasis=1;raw.effects[s.y*width+s.x]=32;}
            if(boundary==2){s.layer=Layer::sprite;s.sprite_opaque=true;}
        }
        const auto t=semantic_topology(*observe(raw));std::vector<double> a(pixels),b(pixels);
        for(int i=0;i<pixels;++i){a[i]=.4+.01*std::sin(i%width*.3)+.01*std::cos(i/width*.3);b[i]=i%width>=128?.42:a[i];}
        auto ta=modern_local_tone(t,a),tb=modern_local_tone(t,b);
        for(int y=0;y<height;++y)for(int x=0;x<128;++x)check(ta[y*width+x]==tb[y*width+x],"tone taps stop at owner effect and occlusion boundaries");
        for(int y=1;y<height-1;++y)check(ta[y*width+127]==a[y*width+127],"hard silhouette luminance exact");
    }
    rejects([&]{modern_local_tone(topology,std::span<const double>{});});
    std::vector<double> bad(pixels,.5);bad[4]=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{modern_local_tone(topology,bad);});
}
Frame world(int scroll){
    auto f=blank(0x17);
    for(auto&s:f.samples){int x=s.x+scroll,y=s.y;auto&v=s.background;
        v.nametable=std::uint16_t(0x2000+(y/8)*32+x/8);v.attribute=std::uint16_t(0x23c0|((v.nametable>>4)&0x38)|((v.nametable>>2)&7));
        v.origin_x=std::int16_t(x/8*8);v.origin_y=std::int16_t(y/8*8);v.local_x=std::uint8_t(x%8);v.local_y=std::uint8_t(y%8);
        v.scroll_v=std::uint16_t((v.nametable&0xfff)|(v.local_y<<12));}
    return f;
}
void fields_and_assets(){
    auto p=modern_plan(*observe(world(0))),q=modern_plan(*observe(world(3)));
    for(int y=10;y<220;++y)for(int x=12;x<236;++x){const auto&a=p.cells[y*width+x+3];const auto&b=q.cells[y*width+x];
        check(a.detail==b.detail&&a.context==b.context,"world field and context locked under scroll");
        if(a.owner_distance>28&&b.owner_distance>28)check(a.normal==ModernNormal{}&&b.normal==ModernNormal{},"large flat world has no tiled emboss normals");
    }
    bool varied=false,context_varied=false;
    for(int y=-20;y<100;++y)for(int x=-20;x<100;++x){double a=modern_detail(42,57,x,y),b=modern_detail(42,57,x+.25,y);
        check(a==modern_detail(42,57,x,y),"field repeats only for same semantic inputs");
        check(std::abs(a-b)<.012,"low frequency field suppresses micrograin");
        check(std::abs(modern_detail(42,57,x+1,y)-2*a+modern_detail(42,57,x-1,y))<.018,"bounded second difference no high-frequency emboss");
        varied|=std::abs(a-b)>.0001;context_varied|=a!=modern_detail(42,58,x,y);
    }
    check(varied&&context_varied,"motif field active and context sensitive");
    auto raw=blank();actor(raw,32,48,24,32);actor(raw,112,48,24,32);
    for(auto&s:raw.samples)if(s.layer==Layer::sprite&&s.x>=112)s.sprite.oam_slot=3;
    auto f=observe(raw);auto plan=modern_plan(*f);const auto&a=plan.cells[60*width+40].context;const auto&b=plan.cells[60*width+120].context;
    check(a.asset_key==b.asset_key&&a!=b,"reused AssetKey has distinct exact context");
    RemasterAsset asset;asset.context=a;asset.albedo=true;asset.texels[0].albedo={.72,.8,.85};
    ModernAssetRegistry registry;registry.add(asset);
    check(registry.lookup(a).status==ModernLookup::found&&registry.lookup(b).status==ModernLookup::missing,"registry exact context preserved");
    auto base=render_modern(*f,{},256,240),replaced=render_modern(*f,{},256,240,&registry);int activity=0;
    for(int i=0;i<pixels;++i){if(plan.cells[i].context==a)activity+=rgb(base,i)!=rgb(replaced,i);
        else if(plan.cells[i].owner_distance>1)check(rgb(base,i)==rgb(replaced,i),"asset replacement isolated to authorized owner");}
    check(activity>100,"M4-R2 asset seam still active");
    check(render_modern(*f,{0,0},256,240,&registry)==render(*f,Mode::SEMANTIC_HD,256,240),"zero controls override authored seam");
    registry.add(asset);check(render_modern(*f,{},256,240,&registry)==base,"ambiguous registry conservative fallback");
    for(int field=0;field<8;++field){auto changed=a;
        if(field==0)++changed.chr_epoch;if(field==1)++changed.high_chr_epoch;if(field==2)++changed.mapper_domain;
        if(field==3)++changed.palette;if(field==4)changed.flags^=64;if(field==5)changed.behind=!changed.behind;
        if(field==6)++changed.oam_slot;if(field==7)++changed.motif_key;
        check(registry.lookup(changed).status==ModernLookup::missing,"no relaxed authorization after refinement");
    }
}
void diagnostics(){
    auto raw=fixture();auto f=observe(raw);auto plan=modern_plan(*f);auto before=render_modern(*f,{},256,240);
    for(auto kind:{ModernDiagnostic::shadow,ModernDiagnostic::roughness,ModernDiagnostic::light}){
        auto im=render_modern_diagnostic(*f,kind,256,240);
        check(im==render_modern_diagnostic(*f,kind,256,240),"new diagnostic deterministic");std::set<std::array<int,3>> colors;
        for(int i=0;i<pixels;++i){auto c=rgb(im,i);colors.insert(c);
            if(kind==ModernDiagnostic::roughness)check(c[0]==std::lround(plan.cells[i].roughness*255),"roughness diagnostic exposes profile");
            if(kind==ModernDiagnostic::light)check(c[0]==c[1]&&c[1]==c[2],"light diagnostic independent of source hue");
            if(kind==ModernDiagnostic::shadow&&plan.cells[i].contact+plan.cells[i].cast_shadow==0)
                check(c==std::array<int,3>{0,0,0},"shadow diagnostic has exact zero outside footprint");
        }
        check(colors.size()>1,"new diagnostic active");
    }
    check(before==render_modern(*f,{},256,240),"diagnostics never influence rendering");
    // Source palette changes with identical actor shape cannot tint light evidence.
    auto a=blank();actor(a,48,64,32,40,0x27);auto b=a;
    for(auto&s:b.samples)if(s.layer==Layer::sprite)paint(b,s.x,s.y,0x16);
    auto la=render_modern_diagnostic(*observe(a),ModernDiagnostic::light,256,240);
    auto lb=render_modern_diagnostic(*observe(b),ModernDiagnostic::light,256,240);
    for(int y=64;y<104;++y)for(int x=48;x<80;++x)check(rgb(la,y*width+x)==rgb(lb,y*width+x),"broad light evidence not source color");
    rejects([&]{render_modern_diagnostic(*f,ModernDiagnostic(4));});
    rejects([&]{render_modern_diagnostic(*f,ModernDiagnostic(11));});
}
}
int main(){
    try{legacy_and_extents();lighting_and_profiles();actor_forms();support_shadows();owner_tone();fields_and_assets();diagnostics();}
    catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    check(checks>=1000000,"at least one million independent observations");
    for(const auto&[name,count]:failed)std::cerr<<name<<": "<<count<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
