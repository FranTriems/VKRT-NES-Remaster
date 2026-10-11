#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/metroid_smrc.hpp"
#include "vkrt/nes_native_s0/display_reference.hpp"
#include <metroid_lab_png.hpp>
#ifdef _WIN32
#include <frontend_ui.hpp>
#endif
#include <cmath>
#include <iostream>
#include <limits>
#include <set>

using namespace vkrt::nes;
namespace {
constexpr auto profile=PresentationProfile::metroid_usa_f1f4;
unsigned checks=0;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const std::invalid_argument&){caught=true;}check(caught,"invalid SMRC input accepted");}
ShapeKey shape(unsigned kind){ShapeKey k;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){unsigned role=0;
        if(kind==0)role=(x>0&&x<7&&y>0&&y<7)?1+(x+y)%3:0;
        if(kind==1)role=x>1&&x<6?1+(y/2)%3:0;
        if(kind==2)role=(x+y)%3?1+(x+y)%2:0;
        if(kind==3)role=(x==2||y==2)&&x<6&&y<6?1:0;
        if(kind==4)role=1;
        if(role&1)k.bits[y]|=std::uint8_t(128>>x);if(role&2)k.bits[y+8]|=std::uint8_t(128>>x);
    }return k;
}
// First-party analytic semantic fixture. No commercial program, tile or map
// bytes are read. Per-pixel winners and fetched spans are independently built.
std::shared_ptr<Frame> fixture(unsigned variant=0,int scroll=0,bool palette_swap=false,unsigned flags=0){
    auto f=std::make_shared<Frame>();auto tap=std::make_shared<PpuProvenance>();f->native=tap;tap->palette[1]=palette_swap?0x28:0x21;
    const auto glyph=shape(3);std::vector<ShapeKey> keys(pixels);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){int i=y*width+x,wx=(x+scroll+512)%512;
        auto& s=f->samples[i];s.x=std::uint16_t(x);s.y=std::uint16_t(y);
        auto& src=s.background;src.local_x=std::uint8_t(wx%8);src.local_y=std::uint8_t(y%8);
        src.nametable=std::uint16_t((wx/256)*1024+(y/8)*32+(wx%256)/8);
        src.origin_x=std::int16_t(wx-wx%8);src.origin_y=std::int16_t(y-y%8);src.palette=1;
        ShapeKey k;
        if(y>=64&&y<192)k=shape(unsigned(wx/32)%4);
        if(y>=24&&y<32&&x>=24&&x<40)k=glyph;
        if(variant&&src.nametable==8*32+8)k.bits[15]^=1;
        unsigned role=((k.bits[y%8]>>(7-wx%8))&1)|(((k.bits[8+y%8]>>(7-wx%8))&1)<<1);
        s.bg_opaque=role!=0;s.layer=role?Layer::background:Layer::backdrop;keys[i]=k;
        // A small actor overlays the world and touches an existing receiver.
        if(x>=80&&x<88&&y>=120&&y<128){
            auto a=shape(0);auto& sp=s.sprite;sp.local_x=std::uint8_t((flags&64)?7-x%8:x%8);sp.local_y=std::uint8_t((flags&128)?7-y%8:y%8);
            sp.origin_x=80;sp.origin_y=120;sp.oam_slot=3;sp.flags=std::uint8_t(flags);sp.palette=2;
            auto rr=((a.bits[sp.local_y]>>(7-sp.local_x))&1)|(((a.bits[8+sp.local_y]>>(7-sp.local_x))&1)<<1);
            if(rr){role=rr;s.layer=Layer::sprite;s.sprite_opaque=true;keys[i]=a;}
        }
        f->indices[i]=role?std::uint8_t(palette_swap?0x20+role:0x10+role):0x0f;s.color=f->indices[i];
    }
    for(int y=0;y<height;++y)for(int x=0;x<width;){int i=y*width+x;const auto& s=f->samples[i];auto src=s.layer==Layer::sprite?s.sprite:s.background;
        int n=1;while(x+n<width){const auto& t=f->samples[i+n];const auto& b=t.layer==Layer::sprite?t.sprite:t.background;
            if(t.layer!=s.layer||b.nametable!=src.nametable||b.origin_x!=src.origin_x||b.flags!=src.flags||keys[i+n]!=keys[i])break;++n;}
        tap->spans.push_back({unsigned(i),unsigned(n),keys[i],src,s.layer,false});x+=n;}
    return f;
}
void profile_test(){
    check(presentation_profile(metroid_digest)==profile,"exact digest activates profile");
    std::string digest(metroid_digest);for(std::size_t i=0;i<digest.size();++i){auto changed=digest;changed[i]=changed[i]=='0'?'1':'0';check(presentation_profile(changed)==PresentationProfile::standard,"digest mismatch activates profile");}
    check(presentation_profile("")==PresentationProfile::standard,"empty digest activates profile");
    check(profile_mode(profile,2)==0&&!profile_mode_allowed(profile,1),"direct F2/F3 allowed");
    for(int step:{-1,1}){check(cycle_profile_mode(profile,0,step)==3,"F1 trigger");check(cycle_profile_mode(profile,3,step)==0,"F4 trigger");}
    check(cycle_profile_mode(PresentationProfile::standard,0,-1)==3&&cycle_profile_mode(PresentationProfile::standard,1,1)==3,"generic cycle must normalize then toggle");
#ifdef _WIN32
    namespace ui=vkrt::nes::frontend;ui::State s;ui::Config c;s.profile=profile;s.session=true;s.page=ui::Page::video;c.visual_mode=0;
    ui::route(s,c,ui::Command::mode1);ui::route(s,c,ui::Command::mode2);check(c.visual_mode==0,"UI accepted F2/F3");
    ui::route(s,c,ui::Command::mode3);check(c.visual_mode==3,"UI rejected F4");
    auto l=ui::layout(s,c,960,700,96);unsigned modes=0;for(unsigned i=0;i<l.count;++i){auto cmd=l.items[i].command;
        check(cmd!=ui::Command::mode1&&cmd!=ui::Command::mode2,"UI lists forbidden mode");if(cmd==ui::Command::mode0||cmd==ui::Command::mode3)++modes;}
    check(modes==2,"UI must list Original/Remaster");
    s.page=ui::Page::game;ui::ControllerUiModel controller;ui::PadSnapshot pad{true};l=ui::layout(s,c,960,700,96);
    controller.update(s,c,l,pad,0);pad.rt=255;auto e=controller.update(s,c,l,pad,20);check(e.command==ui::Command::mode0,"RT does not toggle to F1");
    ui::route(s,c,e.command);pad.rt=0;controller.update(s,c,l,pad,40);pad.lt=255;e=controller.update(s,c,l,pad,60);check(e.command==ui::Command::mode3,"LT does not toggle to F4");
    s.profile=PresentationProfile::standard;ui::route(s,c,ui::Command::mode2);check(c.visual_mode==0,"other ROM F3 must stay disabled");
#endif
    auto f=fixture();for(Mode mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD,Mode::MODERN_REMASTER})
        check(render_profile(*f,mode,PresentationProfile::standard,256,240)==render_reference(*f,mode,256,240),"other ROM rendering changed");
    check(render_profile(*f,Mode::SEMANTIC_HD,profile,256,240)==render_reference(*f,Mode::EXACT_DIGITAL,256,240),"render entry accepted F3");
}
void identity_test(){
    auto s=std::make_unique<ModernScene>(profile);auto f=fixture();observe_modern(*s,f);auto count=s->shape_count();auto im=render_modern_scene(*s,{},256,240);
    observe_modern(*s,f);check(s->stats().shape_builds==0&&s->stats().form_builds==0&&s->stats().background_updates==0,"repeat churn");
    observe_modern(*s,fixture(0,0,true));check(s->stats().shape_builds==0&&s->stats().form_builds==0,"palette rebuilt geometry");
    check(render_modern_scene(*s,{},256,240)!=im,"palette binding ignored");
    observe_modern(*s,fixture(1));check(s->shape_count()>count&&s->stats().shape_builds>0,"CHR RAM change with unchanged address/epoch missed");
    observe_modern(*s,f);check(render_modern_scene(*s,{},256,240)==im,"content restoration stale");
    auto moved=fixture(0,3);observe_modern(*s,moved);check(s->stats().shape_builds==0,"scroll recompiled shape identity");
    auto scrolled=render_modern_scene(*s,{},256,240);
    for(int y=80;y<112;++y)for(int x=32;x<192;++x)for(int k=0;k<3;++k)
        check(scrolled.rgb[(y*256+x)*3+k]==im.rgb[(y*256+x+3)*3+k],"source-local relief/detail swims under scroll");
    observe_modern(*s,fixture(0,122));auto plane1=render_modern_scene(*s,{},256,240);
    observe_modern(*s,fixture(0,250));auto plane2=render_modern_scene(*s,{},256,240);
    for(int y=80;y<112;++y)for(int x=32;x<192;++x)for(int k=0;k<3;++k)
        check(plane1.rgb[(y*256+x)*3+k]==plane2.rgb[(y*256+x)*3+k],"logical nametable seam changes stable appearance");
    auto later=std::make_shared<Frame>(*f);later->number=123456;observe_modern(*s,later);check(render_modern_scene(*s,{},256,240)==im,"frame number animates detail");
    std::set<ShapeKey> exact;auto a=shape(0),b=a;b.bits[15]^=1;exact.insert(a);exact.insert(b);check(exact.size()==2,"full 16-byte equality");
    observe_modern(*s,fixture(0,0,false,192));SmrcDiagnostics flipped;Image flipped_image;
    raster_metroid_smrc_into(flipped_image,*s,{},256,240,&flipped);
    check(flipped.coverage_xor_pixels==0&&flipped.owner_mismatch_pixels==0,"sprite flip ownership");
    auto bad=std::make_shared<Frame>(*f);auto tap=std::make_shared<PpuProvenance>(*f->native);tap->spans[0].count=0;bad->native=tap;rejects([&]{observe_modern(*s,bad);});
}
void invariance_test(){
    auto m=std::make_unique<Machine>(synthetic_nrom());auto scene=std::make_unique<ModernScene>(profile);
    for(unsigned n=0;n<12;++n){m->controllers[0].buttons=std::uint8_t(n);m->run_frames(1);auto state=m->serialize(),save=m->save_state();auto pcm=m->apu.pcm;auto f=m->frame();auto fh=frame_hash(*f);
        observe_modern(*scene,f);for(auto extent:{std::pair{256,240},std::pair{512,480},std::pair{427,389}}){SmrcDiagnostics d;Image im;
            raster_metroid_smrc_into(im,*scene,{},extent.first,extent.second,&d);check(d.coverage_xor_pixels==0&&d.owner_mismatch_pixels==0,"coverage invariance");
            for(int y=0;y<extent.second;++y)for(int x=0;x<extent.first;++x){int i=int(std::int64_t(2*y+1)*240/(2*extent.second))*256+int(std::int64_t(2*x+1)*256/(2*extent.first));
                auto at=y*extent.first+x;check(d.source_cells[at]==unsigned(i),"output displaced source");check(d.owners[at].layer==f->samples[i].layer,"winner changed");}}
        check(m->serialize()==state&&m->save_state()==save&&m->apu.pcm==pcm&&frame_hash(*f)==fh,"canonical mutation");
    }
    auto f=fixture();observe_modern(*scene,f);auto exact=render_reference(*f,Mode::EXACT_DIGITAL,512,480);
    check(render_modern_scene(*scene,{0,0},512,480)==exact,"neutral controls not exact F1");
    auto stale=std::make_shared<Frame>(*f);stale->samples[121*256+82].sprite.origin_x+=1;observe_modern(*scene,stale);SmrcDiagnostics d;Image im;
    raster_metroid_smrc_into(im,*scene,{},256,240,&d);auto i=121*256+82;auto base=reference_display_color(stale->indices[i],stale->effects[i]);
    check(d.texels[i].fallback,"stale provenance did not abstain");for(int k=0;k<3;++k)check(im.rgb[i*3+k]==base[k],"fallback differs from F1");
    rejects([&]{render_modern_scene(*scene,{-1,1});});rejects([&]{render_modern_scene(*scene,{},255,240);});
}
void visual_test(const std::filesystem::path& out){
    // Translate an analytic plate across CHR boundaries. Every point's geometry
    // must translate exactly; a per-tile radius/family would fail this oracle.
    auto plate=[](int move_x,int move_y){std::vector<ReliefTexel> result(40*40);
        for(int ty=0;ty<5;++ty)for(int tx=0;tx<5;++tx){FormKey key;
            for(int ky=0;ky<3;++ky)for(int kx=0;kx<3;++kx)for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                int wx=(tx+kx-1)*8+x-move_x,wy=(ty+ky-1)*8+y-move_y;
                if(wx>=5&&wx<26&&wy>=7&&wy<27){key.neighbors[ky*3+kx].bits[y]|=std::uint8_t(128>>x);
                    if((wx+wy)%3)key.neighbors[ky*3+kx].bits[y+8]|=std::uint8_t(128>>x);}}
            auto form=compile_smrc_form(key);for(int y=0;y<8;++y)for(int x=0;x<8;++x)result[(ty*8+y)*40+tx*8+x]=form.texels[y*8+x];
        }return result;};
    auto original=plate(0,0),translated=plate(3,2);
    for(int y=5;y<30;++y)for(int x=3;x<29;++x){const auto& a=original[y*40+x];const auto& b=translated[(y+2)*40+x+3];
        check(a.height==b.height&&a.nx==b.nx&&a.ny==b.ny&&a.ao==b.ao,"geometry depends on tile partition");}
    auto f=fixture();auto scene=std::make_unique<ModernScene>(profile);observe_modern(*scene,f);SmrcDiagnostics d;Image f4;
    raster_metroid_smrc_into(f4,*scene,{},512,480,&d);auto f1=render_reference(*f,Mode::EXACT_DIGITAL,512,480);
    check(f1!=f4,"SMRC visual no-op");check(render_modern_scene(*scene,{},512,480)==f4,"diagnostic output differs");
    unsigned relit=0,protected_pixels=0,contact_pixels=0;std::set<unsigned> families;
    for(int i=0;i<pixels;++i){const auto& t=d.texels[i];families.insert(unsigned(t.relief.family));
        check(t.relief.height>=0&&t.relief.height<=1&&t.light.ao<=.14f&&t.light.contact<=.09f,"unbounded material fields");
        if(t.light.contact>0){++contact_pixels;check(f->samples[i].layer==Layer::background&&f->samples[i].bg_opaque,"contact has no receiver");}
        if(t.protected_flat){++protected_pixels;auto c=reference_display_color(f->indices[i],f->effects[i]);
            for(int k=0;k<3;++k)check(f4.rgb[((i/256)*2*512+(i%256)*2)*3+k]==c[k],"protected HUD/text/backdrop changed");}
        relit+=t.light.diffuse!=1;
    }
    check(relit>0&&protected_pixels>0&&families.size()>=3,"fixture lacks material diversity");
    check(contact_pixels>0,"fixture lacks witnessed contact");
    check(d.texels[127*256+82].light.contact==0,"contact crosses a backdrop gap");
    for(unsigned code=0;code<64;++code){auto c=reference_display_color(std::uint8_t(code),0);auto neutral=smrc_response(c,{});
        for(int k=0;k<3;++k)check(neutral[k]==c[k],"neutral light not exactly F1");}
    if(!out.empty()){
        std::filesystem::create_directories(out);lab::write_png(out/"synthetic-f1.png",f1);lab::write_png(out/"synthetic-f4.png",f4);
        for(auto [name,view]:{std::pair{"height",SmrcView::height},std::pair{"normal",SmrcView::normal},std::pair{"light",SmrcView::light},std::pair{"owner",SmrcView::owner},std::pair{"delta",SmrcView::delta}})
            lab::write_png(out/(std::string("synthetic-")+name+".png"),render_smrc_diagnostic(*f,f1,f4,d,view));
    }
}
}
int main(int argc,char** argv){try{
    std::string suite=argc>1?argv[1]:"";
    if(suite=="profile")profile_test();else if(suite=="identity")identity_test();else if(suite=="invariance")invariance_test();
    else if(suite=="visual")visual_test(argc>2?std::filesystem::path(argv[2]):std::filesystem::path{});else throw std::runtime_error("unknown suite");
    std::cout<<"PASS "<<suite<<" checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
