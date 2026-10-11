#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_scene.hpp"
#include "vkrt/nes_native_s0/modern_color.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,std::uint64_t> failed;
void check(bool ok,const char* label){++checks;if(!ok){++failures;++failed[label];}}
float hue(ModernRgb c){
    float hi=std::max({c[0],c[1],c[2]}),lo=std::min({c[0],c[1],c[2]});
    if(hi==lo)return 0;
    float h=hi==c[0]?(c[1]-c[2])/(hi-lo):hi==c[1]?2+(c[2]-c[0])/(hi-lo):4+(c[0]-c[1])/(hi-lo);
    return h<0?h+6:h;
}
float hue_distance(ModernRgb a,ModernRgb b){float d=std::abs(hue(a)-hue(b));return std::min(d,6-d);}
ModernRgb rgb(std::array<std::uint8_t,3> c){return {float(c[0]),float(c[1]),float(c[2])};}
ModernRgb pixel(const Image& im,int i){return {float(im.rgb[i*3]),float(im.rgb[i*3+1]),float(im.rgb[i*3+2])};}
float contrast(ModernRgb a,ModernRgb b){return std::max({std::abs(a[0]-b[0]),std::abs(a[1]-b[1]),std::abs(a[2]-b[2])});}

void generic_color_volume(){
    // 491,520 distinct RGB/material/control combinations, with independent HSV
    // and ordering oracles. This is a color volume, not a repeated check loop.
    std::uint64_t changed=0;
    for(int r=0;r<256;r+=17)for(int g=0;g<256;g+=17)for(int b=0;b<256;b+=17){
        std::array<std::uint8_t,3> source{std::uint8_t(r),std::uint8_t(g),std::uint8_t(b)};
        for(float tone:{0.f,.5f,1.f})for(float light:{.2f,.35f,.5f,.7f,.85f,1.f,1.2f,1.5f})for(float spec:{0.f,2.f,5.f,10.f,20.f}){
            auto base=modern_albedo(source,tone),out=modern_light_response(base,light,spec);
            auto repeat=modern_light_response(modern_albedo(source,tone),light,spec);
            for(int k=0;k<3;++k){
                check(std::isfinite(out[k])&&out[k]>=0&&out[k]<=255.001f,"bounded finite material color");
                check(out[k]==repeat[k],"material repeat determinism");
                int j=(k+1)%3;
                check(source[k]==source[j]?std::abs(out[k]-out[j])<.001f:(source[k]>source[j])==(out[k]>out[j]),"source channel order preserved");
            }
            check(hue_distance(rgb(source),out)<.0001f,"generic RGB hue anchored through light and material");
            check(hue_distance(rgb(source),base)<.0001f,"albedo independently hue anchored");
            if(r==g&&g==b)check(out[0]==out[1]&&out[1]==out[2],"neutral and dark never tinted");
            if(contrast(rgb(source),out)>8)++changed;
        }
    }
    check(changed>300000,"material response changes appearance substantially");
    // Independent family bounds, rather than comparison against the table.
    for(int level=0;level<4;++level){
        auto blue=rgb(modern_source_color(std::uint8_t(level*16+1),0));
        auto green=rgb(modern_source_color(std::uint8_t(level*16+10),0));
        auto warm=rgb(modern_source_color(std::uint8_t(level*16+7),0));
        auto red=rgb(modern_source_color(std::uint8_t(level*16+6),0));
        check(blue[2]>blue[1]&&blue[1]>blue[0],"NES blue family");
        check(green[1]>green[0]&&green[1]>green[2],"NES green family");
        check(warm[0]>warm[1]&&warm[1]>warm[2],"NES warm family");
        check(red[0]>red[1]&&red[0]>red[2],"NES red orange family");
    }
    auto cream=modern_source_color(0x37,0);
    check(cream[0]>cream[1]&&cream[1]>cream[2]&&cream[2]>150,"warm cream source identity");
    for(int c=0;c<256;++c)for(int e=0;e<256;++e){
        auto source=modern_source_color(std::uint8_t(c),std::uint8_t(e));
        check(source==modern_source_color(std::uint8_t(c&63),std::uint8_t(e&0xe1)),"only canonical color and effect bits affect albedo");
        if(e&1)check(source==modern_source_color(std::uint8_t(c&0x30),std::uint8_t(e&0xe0)),"grayscale is decoded before emphasis");
    }
}

struct Rig {
    Ppu p;
    std::unique_ptr<Mapper> mapper;
    Rig(){
        Bytes rom(16+32768);rom[0]='N';rom[1]='E';rom[2]='S';rom[3]=26;rom[4]=2;
        mapper=load_cartridge(rom);p.mask=0x1e;
        p.palette={0x37,0x12,0x1a,0x30,0x37,0x16,0x27,0x30,0x37,0x1a,0x2a,0x30,0x37,0x11,0x21,0x30,
                   0x37,0x0f,0x16,0x30,0x37,0x12,0x22,0x30,0x37,0x1a,0x2a,0x30,0x37,0x16,0x27,0x30};
        for(int y=0;y<8;++y){
            mapper->ppu_write(16+y,255); // connected solid
            mapper->ppu_write(32+y,y==0||y==7?0x7e:0x42); // ring/one-pixel edges
            mapper->ppu_write(40+y,y==3||y==4?0x18:0);
            mapper->ppu_write(48+y,0x10); // thin actor
        }
        for(int y=0;y<30;++y)for(int x=0;x<32;++x)
            p.write_memory(*mapper,std::uint16_t(0x2000+y*32+x),std::uint8_t(y<8?1:(x/4+y/4)%3));
        p.oam.fill(255);
        p.oam[0]=100;p.oam[1]=2;p.oam[2]=0;p.oam[3]=64;
        p.oam[4]=100;p.oam[5]=3;p.oam[6]=1;p.oam[7]=80;
    }
    ImmutableFrame next(){auto target=p.frame_number+1;for(int n=0;n<90000&&p.frame_number!=target;++n)p.tick(*mapper);return p.completed;}
};
void authority_and_render(){
    auto rig=std::make_unique<Rig>();rig->next();auto f=rig->next();
    auto scene=std::make_unique<ModernScene>();auto calls=legacy_observation_calls();
    observe_modern(*scene,f);
    check(&scene->canonical()==f.get(),"retains actual immutable PPU frame");
    check(f->native&&scene->stats().spans==f->native->spans.size(),"native commands remain in use");
    check(legacy_observation_calls()==calls&&scene->stats().legacy_observer_calls==0,"no legacy semantic observer");
    const auto before=frame_hash(*f);Encoder machine_before;rig->p.encode(machine_before);rig->mapper->encode(machine_before);
    Image native;
    for(auto [w,h]:{std::pair{256,240},std::pair{512,480},std::pair{427,389}}){
        auto a=render_modern_scene(*scene,{},w,h),b=render_modern_scene(*scene,{},w,h);
        auto structure=modern_structure(*scene,w,h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            int i=int(std::int64_t(2*y+1)*height/(2*h))*width+int(std::int64_t(2*x+1)*width/(2*w)),j=y*w+x;
            const auto& s=f->samples[i];
            check(structure[j].color==f->indices[i]&&structure[j].effects==f->effects[i],"canonical color and effects immutable");
            check(structure[j].layer==s.layer,"silhouette layer exact");
            check(structure[j].oam_slot==(s.layer==Layer::sprite?s.sprite.oam_slot:255),"winning owner exact");
            for(int k=0;k<3;++k)check(a.rgb[j*3+k]==b.rgb[j*3+k],"retained render repeated bytes exact");
            auto source=rgb(modern_source_color(f->indices[i],f->effects[i]));
            if(contrast(source,{source[0],source[0],source[0]})>24)
                check(hue_distance(source,pixel(a,j))<.09f,"raster hue family including subpixel paint");
        }
        check(render_modern_scene(*scene,{0,0},w,h)==render(*f,Mode::SEMANTIC_HD,w,h),"zero zero delegates frozen semantic renderer");
        if(w==256)native=a;
    }
    check(render_modern(*f,{},256,240)==native,"public F4 dispatch retains native path");
    auto still=std::make_shared<Frame>(*f);still->number+=1000;observe_modern(*scene,still);
    auto stable=render_modern_scene(*scene,{},256,240);
    for(unsigned i=0;i<native.rgb.size();++i)check(native.rgb[i]==stable.rgb[i],"static frame has zero shimmer");
    check(scene->stats().shape_builds==0&&scene->stats().form_builds==0&&scene->stats().background_updates==0,"static scene reuses compiled forms");

    // Metamorphic identity test: relabel all pattern/tile/mapper identities while
    // preserving actual fetched shapes and source-local coordinates.
    auto relabeled=std::make_shared<Frame>(*f);auto tap=std::make_shared<PpuProvenance>(*f->native);
    auto rename=[](Source& s){s.pattern^=0x800;s.tile^=0x5a;s.chr_source+=777;s.chr_epoch+=5;s.high_chr_epoch+=5;};
    for(auto& s:relabeled->samples){rename(s.background);rename(s.sprite);s.asset_key+=345;s.surface_key+=234;}
    for(auto& cmd:tap->spans)rename(cmd.source);
    relabeled->native=tap;relabeled->mapper_domain=251;observe_modern(*scene,relabeled);
    check(render_modern_scene(*scene,{},256,240)==native,"appearance independent of title tile pattern mapper labels");

    // Strong source edges must remain edges at the SAME pixel, including thin
    // sprites. Appearance never borrows a neighbor across incompatible support.
    unsigned strong=0,readable=0,actor=0;double delta=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x;
        auto source=rgb(modern_source_color(f->indices[i],f->effects[i]));
        delta+=contrast(source,pixel(native,i));
        if(f->samples[i].layer==Layer::sprite&&contrast(source,pixel(native,i))>5)++actor;
        if(!x)continue;
        auto other=rgb(modern_source_color(f->indices[i-1],f->effects[i-1]));
        if(contrast(source,other)>64){++strong;bool edge=contrast(pixel(native,i),pixel(native,i-1))>32;readable+=edge;check(edge,"strong edge remains readable at canonical boundary");}
    }
    check(strong>1000&&readable*100>=strong*95,"strong edge recall at least 95 percent");
    check(delta/pixels>12&&actor>8,"strong remaster changes world and actor appearance");
    auto tone=render_modern_scene(*scene,{0,1},256,240),structure=render_modern_scene(*scene,{1,0},256,240);
    check(tone!=native&&structure!=native&&tone!=structure,"independent tone and structure controls remain active");
    Encoder machine_after;rig->p.encode(machine_after);rig->mapper->encode(machine_after);
    check(machine_before.bytes==machine_after.bytes&&frame_hash(*f)==before,"canonical PPU mapper and source frame immutable");
}

void continuity_and_boundaries(){
    auto rig=std::make_unique<Rig>();rig->next();auto f=rig->next();auto scene=std::make_unique<ModernScene>();observe_modern(*scene,f);
    auto full=render_modern_scene(*scene,{},512,480);
    // Solid neighboring cells share the same surface. Their gradient must not
    // have a seam larger than the bounded source-local pigment variation.
    for(int y=24;y<48;++y)for(int x=32;x<224;x+=8){int j=y*2*512+x*2;
        check(contrast(pixel(full,j-1),pixel(full,j))<12,"connected surface continuous across tile seam");
    }
    // A palette-role boundary with equal visible color still separates form
    // support. This catches accidental joining based only on RGB equality.
    auto boundary=std::make_shared<Frame>(*f);auto tap=std::make_shared<PpuProvenance>(*f->native);
    for(auto& s:boundary->samples)if(s.layer==Layer::background&&s.background.origin_x>=128)s.background.palette=1;
    for(auto& cmd:tap->spans)if(cmd.layer==Layer::background&&cmd.source.origin_x>=128)cmd.source.palette=1;
    boundary->native=tap;observe_modern(*scene,boundary);auto split=render_modern_scene(*scene,{},512,480);
    unsigned changes=0;
    for(int y=24;y<48;++y)for(int x=124;x<132;++x){int i=y*2*512+x*2;if(pixel(split,i)!=pixel(full,i))++changes;}
    check(changes>24,"incompatible palette boundary separates relief support");
    // Move the PPU view: all interior source-local appearance must travel with
    // its world surface, with no screen-coordinate asset substitutions.
    rig->p.fine_x=3;auto moved=rig->next();observe_modern(*scene,moved);auto after=render_modern_scene(*scene,{0,1},256,240);
    observe_modern(*scene,f);auto before=render_modern_scene(*scene,{0,1},256,240);
    for(int y=24;y<48;++y)for(int x=32;x<200;++x)for(int k=0;k<3;++k)
        check(after.rgb[(y*width+x)*3+k]==before.rgb[(y*width+x+3)*3+k],"world pigment scroll covariance");
    before=render_modern_scene(*scene,{},256,240);
    observe_modern(*scene,moved);after=render_modern_scene(*scene,{},256,240);
    for(int y=24;y<48;++y)for(int x=32;x<200;++x)for(int k=0;k<3;++k)
        check(after.rgb[(y*width+x)*3+k]==before.rgb[(y*width+x+3)*3+k],"full relief and material scroll covariance");
}
void source_policy(){
    auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
    for(const auto& name:{"src/modern_scene.cpp","include/vkrt/nes_native_s0/modern_color.hpp"}){
        std::ifstream file(root/name);std::ostringstream s;s<<file.rdbuf();auto text=s.str();
        check(bool(file),"implementation available for source policy");
        for(const auto* token:{"SMB3","Super Mario","title_screen",".tile==",".tile ==","connected_regions(","semantic_contours(","sin(","cos("})
            check(text.find(token)==std::string::npos,"no title tile rediscovery or temporal color mechanism");
    }
}
}
int main(){
    try{generic_color_volume();authority_and_render();continuity_and_boundaries();source_policy();}
    catch(const std::exception& e){check(false,e.what());}
    for(const auto& [name,count]:failed)std::cerr<<name<<": "<<count<<'\n';
    check(checks>=5000000,"at least five million executed assertions");
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';
    return failures?1:0;
}
