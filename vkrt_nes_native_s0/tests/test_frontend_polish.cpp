#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <frontend_ui.hpp>
#include "vkrt/nes_native_s0/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace vkrt::nes;
namespace ui=vkrt::nes::frontend;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
int item(const ui::Layout& l,ui::Command command){
    for(unsigned i=0;i<l.count;++i)if(l.items[i].command==command)return int(i);
    return -1;
}
Bytes upload(const Frame& f,const Image& image){
    Bytes bytes(512*480*4);ui::compose_playfield(f,image,bytes);return bytes;
}
void same_pixel(const Bytes& bytes,const Image& image,int x,int y,bool blank){
    const auto p=std::size_t(y*512+x);
    for(unsigned k=0;k<3;++k)
        check(bytes[p*4+k]==(blank?0:image.rgb[p*3+2-k]),"only witnessed blank pixels may change RGB");
    check(bytes[p*4+3]==255,"upload is fully opaque");
}
Frame ppu_frame(unsigned mask){
    // Real PPU tap, without running a title or modifying canonical code.
    Machine machine(synthetic_nrom());machine.ppu.mask=std::uint8_t(mask);
    machine.ppu.palette.fill(0x31); // Deliberately cyan even for legitimate content.
    while(machine.ppu.frame_number<2)machine.ppu.tick(*machine.mapper);
    const auto before=machine.serialize();auto frame=observe(*machine.frame());
    for(auto mode:{Mode::EXACT_DIGITAL,Mode::MODERN_REMASTER}){
        const auto image=render_profile(*frame,mode,PresentationProfile::standard,512,480);
        const auto original=image.rgb;const auto hash=frame_hash(*frame);
        const auto bytes=upload(*frame,image);
        for(int y=0;y<480;++y)for(int x=0;x<512;++x)same_pixel(bytes,image,x,y,mask==0x18&&x<16);
        check(image.rgb==original&&frame_hash(*frame)==hash&&machine.serialize()==before,
            "frontend composition preserves image, immutable frame and canonical machine");
    }
    return *frame;
}
void playfield(const std::filesystem::path& out){
    auto clipped=ppu_frame(0x18);
    const auto fetched=ppu_frame(0x1e);
    const auto forced_blank=ppu_frame(0);
    Image cyan{512,480,Bytes(512*480*3)};
    for(std::size_t i=0;i<cyan.rgb.size();i+=3){cyan.rgb[i]=96;cyan.rgb[i+1]=216;cyan.rgb[i+2]=248;}
    const auto original=cyan;
    auto mixed=clipped;
    // A lower HUD enables left rendering. Another row is fully forced blank.
    // Neither may be inferred from the register value at the end of the frame.
    for(int y=192;y<240;++y)std::copy_n(fetched.samples.begin()+y*256,256,mixed.samples.begin()+y*256);
    std::copy_n(forced_blank.samples.begin()+80*256,256,mixed.samples.begin()+80*256);
    auto& sprite=mixed.samples[100*256+3];sprite.layer=Layer::sprite;sprite.sprite_opaque=true;
    const auto mixed_bytes=upload(mixed,cyan);
    for(int y=0;y<480;++y)for(int x=0;x<512;++x)
        same_pixel(mixed_bytes,cyan,x,y,x<16&&y<384&&y/2!=80&&!(y/2==100&&x/2==3));
    check(cyan==original,"no writes through rendered input");
    bool rejected=false;
    try{Bytes bad(17);ui::compose_playfield(clipped,cyan,bad);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalid upload extent rejected before writing");

    const auto bytes=upload(clipped,cyan);
    ui::Canvas canvas;ui::Config c;ui::State s;s.session=true;s.page=ui::Page::game;
    constexpr ui::Rect extents[]{{0,0,800,600},{0,0,1100,800},{0,0,1280,720},{0,0,1920,1080},{0,0,801,601}};
    for(const auto e:extents)for(unsigned dpi:{96u,144u,192u})for(bool integer:{false,true}){
        c.integer_scaling=integer;
        const auto l=ui::layout(s,c,e.w,e.h,dpi);
        const auto v=ui::game_viewport(e.w,e.h,integer,l.scale);
        const int bar=int(std::lround(56*l.scale));
        check(std::abs(v.x-(e.w-v.x-v.w))<=1,"left and right frame gutters are symmetric");
        check(std::abs((v.y-bar)-(e.h-v.y-v.h))<=1,"vertical frame gutters below toolbar are symmetric");
        check(v.y>=bar&&v.x>=0&&v.x+v.w<=e.w&&v.y+v.h<=e.h,"full picture fits below toolbar");
        check(std::abs(std::int64_t(v.w)*480-std::int64_t(v.h)*512)<=512,"no picture aspect distortion");
        canvas.draw(s,c,l,e.w,e.h,bytes);const auto hash=canvas.hash();
        auto screen=canvas.pixels();
        const int y=v.y+v.h/2;
        for(int x=0;x<e.w;++x){
            const auto p=std::size_t(y*e.w+x)*4;
            if(x<v.x||x>=v.x+v.w)
                check(screen[p]==27&&screen[p+1]==20&&screen[p+2]==16,"letterbox contains only neutral shell, no colored gutter");
        }
        auto color=[&](int x){const auto p=std::size_t(y*e.w+x)*4;return std::array<unsigned,3>{screen[p],screen[p+1],screen[p+2]};};
        check(color(v.x)==std::array<unsigned,3>{0,0,0},"blanked edge has no cyan strip at content boundary");
        check(color(v.x+v.w-1)==std::array<unsigned,3>{248,216,96},"rightmost source column is retained, without inner inset");
        if(v.w==512&&v.h==480){
            for(int sy=0;sy<480;++sy)for(int sx=0;sx<512;++sx){
                const auto a=std::size_t(((v.y+sy)*e.w+v.x+sx)*4),b=std::size_t((sy*512+sx)*4);
                check(std::equal(bytes.begin()+b,bytes.begin()+b+4,screen.begin()+a),"all source pixels map to unchanged coordinates");
            }
        }
        for(auto page:{ui::Page::pause,ui::Page::video,ui::Page::controls}){
            s.page=page;const auto overlay=ui::layout(s,c,e.w,e.h,dpi);
            check(ui::game_viewport(e.w,e.h,integer,overlay.scale)==v,"gameplay-adjacent pages retain exact viewport");
            canvas.draw(s,c,overlay,e.w,e.h,bytes);
        }
        s.page=ui::Page::game;s.loading=true;canvas.draw(s,c,ui::layout(s,c,e.w,e.h,dpi),e.w,e.h,bytes);s.loading=false;
        s.error=L"Fixture error";canvas.draw(s,c,ui::layout(s,c,e.w,e.h,dpi),e.w,e.h,bytes);s.error.clear();
        canvas.draw(s,c,l,e.w,e.h,bytes);
        check(canvas.hash()==hash,"return from overlays clears stale composition and retains geometry");
    }
    c.integer_scaling=true;
    canvas.draw(s,c,ui::layout(s,c,1100,800,96),1100,800,bytes);
    canvas.write_bmp(out/"playfield-clean.bmp");
    const auto before=upload(fetched,cyan);
    canvas.draw(s,c,ui::layout(s,c,1100,800,96),1100,800,before);
    canvas.write_bmp(out/"playfield-legitimate-cyan.bmp");
}
struct Frontend {
    ui::State state;ui::Config config;ui::ControllerUiModel controller;std::uint64_t now{};
    Frontend(){state.session=true;state.page=ui::Page::game;}
    ui::Command tick(ui::PadSnapshot p){
        const auto l=ui::layout(state,config,1100,800,96);
        const auto result=controller.update(state,config,l,p,now++);
        if(result.command!=ui::Command::none)ui::route(state,config,result.command);
        return result.command;
    }
    ui::Command press(unsigned buttons){tick({true});return tick({true,buttons});}
};
void home(const std::filesystem::path& out){
    for(bool replay:{false,true})for(bool tape:{false,true}){
        Frontend f;f.state.replay=replay;f.state.tape_locked=tape;
        const auto original=f.config;
        f.press(ui::pad_menu);check(f.state.page==ui::Page::pause,"R3 reaches in-game menu");
        auto l=ui::layout(f.state,f.config,1100,800,96);const int target=item(l,ui::Command::home);
        check(target>=0&&l.items[target].enabled&&l.items[target].label==L"Home","pause menu contains enabled Home");
        const auto r=l.items[target].rect;
        check(ui::hit_test(l,r.x+r.w/2,r.y+r.h/2)==target,"Home reachable by mouse");
        auto keyboard=f.state;
        for(unsigned i=0;i<l.count&&keyboard.selection!=target;++i)ui::move_selection(keyboard,l,1);
        check(keyboard.selection==target,"Tab/down keyboard traversal reaches Home");
        const auto effect=ui::route(keyboard,f.config,l.items[keyboard.selection].command);
        check(keyboard.page==ui::Page::home&&!effect.quit&&!effect.reset&&!effect.open,"keyboard activation opens front page, without destructive effect");
        check(f.press(2)==ui::Command::none&&f.state.selection==target,"controller D-pad reaches Home from Resume");
        check(f.press(ui::pad_a)==ui::Command::home&&f.state.page==ui::Page::home,"controller A activates Home");
        check(f.state.session&&f.state.replay==replay&&f.state.tape_locked==tape&&f.config==original&&ui::should_pause(f.state,f.config),"Home preserves session, tape and settings, while paused");
        for(int i=0;i<10;++i)check(f.tick({true,ui::pad_a})==ui::Command::none,"held confirmation cannot open ROM on Home");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::home,"R3 cannot accidentally resume from Home");
        ui::route(f.state,f.config,ui::Command::settings);ui::route(f.state,f.config,ui::Command::controls);
        f.press(ui::pad_b);check(f.state.page==ui::Page::home,"Home settings B returns to Home even with a session");
        ui::route(f.state,f.config,ui::Command::settings);ui::escape(f.state);
        check(f.state.page==ui::Page::home,"Home settings Escape returns to Home");
        ui::route(f.state,f.config,ui::Command::settings);ui::route(f.state,f.config,ui::Command::back);
        check(f.state.page==ui::Page::home,"Home settings Back returns to Home");
        l=ui::layout(f.state,f.config,1100,800,96);
        const int resume=item(l,ui::Command::resume);check(resume>=0,"paused session can be resumed from Home");
        f.state.selection=resume;check(f.press(ui::pad_a)==ui::Command::resume&&f.state.page==ui::Page::game,"Home Resume reachable by controller");
        check(!ui::should_pause(f.state,f.config),"Resume releases menu pause");
        f.press(ui::pad_menu);ui::route(f.state,f.config,ui::Command::controls);
        f.press(ui::pad_menu);check(f.state.page==ui::Page::pause,"in-game submenu still returns to Pause");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::game,"R3 still resumes from Pause");
        ui::escape(f.state);f.press(0x10);check(f.state.page==ui::Page::game,"Start still resumes from Pause");
        ui::escape(f.state);ui::escape(f.state);check(f.state.page==ui::Page::game,"Escape still toggles pause and resume");
    }
    for(int capture:{0,1}){
        Frontend f;f.press(ui::pad_menu);ui::route(f.state,f.config,ui::Command::controls);
        if(capture)f.state.pad_binding=0;else f.state.binding=0;
        ui::route(f.state,f.config,ui::Command::home);
        check(f.state.binding<0&&f.state.pad_binding<0&&f.state.page==ui::Page::controls,"Home first cancels active capture");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::pause,"R3 unwinds capture submenu to Pause");
        f.press(2);f.press(ui::pad_a);check(f.state.page==ui::Page::home,"Home works after capture unwind");
    }
    ui::State s;s.session=true;s.page=ui::Page::pause;ui::Config c;
    s.error=L"fixture";ui::route(s,c,ui::Command::home);
    check(s.error.empty()&&s.page==ui::Page::pause,"Home first dismisses modal error");
    s.loading=true;ui::route(s,c,ui::Command::home);check(s.page==ui::Page::pause,"Home cannot interrupt session staging");
    s.loading=false;ui::route(s,c,ui::Command::home);
    ui::Canvas canvas;const auto l=ui::layout(s,c,1100,800,96);
    canvas.draw(s,c,l,1100,800);const auto empty=canvas.hash();
    canvas.draw(s,c,l,1100,800,Bytes(512*480*4,255));
    check(canvas.hash()==empty,"Home has no stale gameplay behind panels");
    canvas.write_bmp(out/"home-with-session.bmp");
    s.page=ui::Page::pause;canvas.draw(s,c,ui::layout(s,c,1100,800,96),1100,800);
    canvas.write_bmp(out/"pause-home-action.bmp");
    s.session=false;s.page=ui::Page::home;
    check(item(ui::layout(s,c,1100,800,96),ui::Command::resume)<0,"fresh Home has no unavailable Resume");
    ui::route(s,c,ui::Command::resume);check(s.page==ui::Page::home,"no session cannot be resumed");
}
}
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("expected playfield/home and output directory");
        const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
        if(std::string_view(argv[1])=="playfield")playfield(output);
        else if(std::string_view(argv[1])=="home")home(output);
        else throw std::runtime_error("unknown suite");
        std::cout<<"CHECKS="<<checks<<" RESULT=PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" CHECKS="<<checks<<'\n';return 1;}
}
