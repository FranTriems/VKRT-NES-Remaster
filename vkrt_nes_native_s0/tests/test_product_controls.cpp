#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <frontend_ui.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace vkrt::nes;
namespace ui=vkrt::nes::frontend;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
constexpr PresentationProfile profiles[]{PresentationProfile::standard,PresentationProfile::metroid_usa_f1f4};
constexpr ui::Page pages[]{ui::Page::home,ui::Page::game,ui::Page::pause,ui::Page::video,ui::Page::audio,ui::Page::controls,ui::Page::advanced};
struct Frontend {
    ui::State state;
    ui::Config config;
    ui::ControllerUiModel controller;
    std::uint64_t now{};
    Frontend(){state.session=true;state.page=ui::Page::game;}
    ui::ControllerResult tick(ui::PadSnapshot p){
        auto l=ui::layout(state,config,960,700,96);
        const auto e=controller.update(state,config,l,p,now++);
        if(e.command!=ui::Command::none)ui::route(state,config,e.command);
        return e;
    }
    ui::ControllerResult press(unsigned buttons){tick({true});return tick({true,buttons});}
};
void product(const std::filesystem::path& output){
    check(product_mode_slots==std::array<unsigned,2>{0,3},"product exposes exactly slots 0/3");
    check(ui::visual_modes==std::array<Mode,2>{Mode::EXACT_DIGITAL,Mode::MODERN_REMASTER},"only two frontend renderer choices");
    check(ui::visual_names.size()==2&&std::wstring_view(ui::visual_names[0])==L"Original"&&
        std::wstring_view(ui::visual_names[1])==L"Remaster","only Original/Remaster labels");
    check(presentation_profile("")==profiles[0]&&presentation_profile(metroid_digest)==profiles[1],"ordinary and Metroid profiles resolved");
    for(auto profile:profiles){
        for(unsigned slot=0;slot<256;++slot){
            const bool allowed=slot==0||slot==3;
            check(profile_mode_allowed(profile,slot)==allowed,"global allowlist independent of ROM");
            check(profile_mode(profile,slot)==(slot==3?3u:0u),"legacy or invalid mode normalizes to Original");
            check(ui::visual_mode(slot)==(slot==3?Mode::MODERN_REMASTER:Mode::EXACT_DIGITAL),"frontend dispatch cannot select legacy renderer");
            for(int step:{-1,0,1})check(cycle_profile_mode(profile,slot,step)==(step?(slot==3?0u:3u):(slot==3?3u:0u)),"normalized two-mode cycling");
        }
        check(profile_mode(profile,std::numeric_limits<unsigned>::max())==0,"unbounded mode rejected");
        for(unsigned initial:{0u,3u})for(unsigned key=0;key<256;++key){
            ui::State s;s.profile=profile;ui::Config c;c.visual_mode=initial;
            const int slot=ui::shortcut_mode(key);
            check(slot==(key==VK_F1?0:key==VK_F4?3:-1),"only F1/F4 map to product modes");
            if(slot>=0)ui::route(s,c,ui::Command(unsigned(ui::Command::mode0)+unsigned(slot)));
            check(c.visual_mode==(key==VK_F1?0u:key==VK_F4?3u:initial),"F2/F3 leave active mode unchanged");
            for(auto denied:{ui::Command::mode1,ui::Command::mode2}){
                const auto before=c;const auto effect=ui::route(s,c,denied);
                check(c==before&&!effect.save,"direct legacy commands rejected without persistence");
            }
        }
        for(auto page:pages)for(unsigned initial:{0u,1u,2u,3u}){
            ui::State s;s.profile=profile;s.page=page;s.session=page!=ui::Page::home;
            ui::Config c;c.visual_mode=initial;
            for(unsigned dpi:{96u,144u,192u}){
                const auto l=ui::layout(s,c,1100,800,dpi);unsigned modes=0,selected=0;
                for(unsigned i=0;i<l.count;++i){const auto& item=l.items[i];
                    check(item.command!=ui::Command::mode1&&item.command!=ui::Command::mode2,"no legacy mode in any menu");
                    for(const auto* old:{L"CRT",L"scanline",L"Semantic HD"})check(item.label.find(old)==std::wstring::npos,"no legacy UI wording");
                    if(item.command==ui::Command::mode0||item.command==ui::Command::mode3){
                        ++modes;selected+=item.selected;
                        const bool remaster=item.command==ui::Command::mode3;
                        check(item.label==(remaster?L"Remaster":L"Original"),"mode cards named unambiguously");
                        check(item.selected==(remaster?(initial==3):(initial!=3)),"active card matches normalized preference");
                    }
                }
                check(modes==(page==ui::Page::video?2u:0u)&&selected==(page==ui::Page::video?1u:0u),"exactly two cards and one active mode");
            }
            const auto e=ui::route(s,c,ui::Command::none);
            check(c.visual_mode==(initial==3?3u:0u)&&e.save==(initial==1||initial==2),"state transition normalizes and requests persistence");
            ui::remember(c,L"C:\\library\\example.nes");
            check(ui::route(s,c,ui::Command::open).open,"open still routes");
            check(ui::route(s,c,ui::Command::recent0).recent==0,"recent still routes");
            s.profile=profile==profiles[0]?profiles[1]:profiles[0];
            ui::route(s,c,ui::Command::resume);
            check(c.visual_mode==(initial==3?3u:0u),"ROM/profile transition never restores legacy modes");
        }
        for(unsigned initial:{0u,3u})for(bool right:{false,true}){
            Frontend f;f.state.profile=profile;f.config.visual_mode=initial;f.tick({true});
            ui::PadSnapshot p{true};(right?p.rt:p.lt)=152;
            check(f.tick(p).command==ui::Command::none,"trigger below threshold ignored");
            (right?p.rt:p.lt)=153;f.tick(p);
            check(f.config.visual_mode==(initial==0?3u:0u),"either trigger toggles to other product mode");
            for(unsigned i=0;i<200;++i)check(f.tick(p).command==ui::Command::none,"held trigger never repeats");
            (right?p.rt:p.lt)=65;f.tick(p);(right?p.rt:p.lt)=255;
            check(f.tick(p).command==ui::Command::none,"partial release does not rearm");
            (right?p.rt:p.lt)=64;f.tick(p);(right?p.rt:p.lt)=153;f.tick(p);
            check(f.config.visual_mode==initial,"hysteresis release permits one new toggle");
            f.tick({});check(f.tick(p).command==ui::Command::none,"held trigger on reconnect suppressed");
        }
    }
    std::filesystem::create_directories(output);
    for(unsigned initial:{0u,1u,2u,3u}){
        ui::Config c;c.visual_mode=initial;c.volume=37;c.state_slot=4;c.stats=true;
        ui::remember(c,L"C:\\library\\\u65e5\u672c\u8a9e.nes");
        const unsigned expected=initial==3?3:0;
        const auto encoded=ui::serialize_config(c);c.visual_mode=expected;
        check(encoded.find("mode="+std::to_string(expected)+"\n")!=std::string::npos,"writer persists only normalized 0/3");
        check(ui::parse_config(encoded)==c,"persistence normalizes without altering unrelated preferences");
        auto legacy=encoded;const auto begin=legacy.find("mode=")+5;legacy.replace(begin,1,std::to_string(initial));
        const auto path=output/(std::to_string(initial)+".ini");
        {std::ofstream file(path,std::ios::binary);file<<legacy;check(bool(file),"legacy fixture written");}
        std::wstring warning;const auto loaded=ui::load_config(path,warning);
        check(loaded==c&&warning.empty(),"actual legacy config load migrates 1/2 to 0");
        check(ui::save_config(path,loaded,warning)&&warning.empty(),"migrated config saved atomically");
        std::ifstream file(path,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(file)),{});
        check(bytes==encoded&&ui::load_config(path,warning)==c,"disk bytes settle exactly on normalized config");
    }
    ui::Canvas canvas;ui::State s;ui::Config c;s.session=true;s.gamepad_connected=true;
    for(auto page:pages){
        s.page=page;const auto l=ui::layout(s,c,1100,800,96);canvas.draw(s,c,l,1100,800);
        canvas.write_bmp(output/("page-"+std::to_string(unsigned(page))+".bmp"));
    }
}
void menu(){
    check(ui::pad_menu==XINPUT_GAMEPAD_RIGHT_THUMB,"R3 bound to real XInput right thumb constant");
    check(std::find(ui::pad_choices.begin(),ui::pad_choices.end(),ui::pad_menu)==ui::pad_choices.end(),"R3 absent from gameplay binding choices");
    for(unsigned target=0;target<4;++target){
        auto b=ui::default_pad_bindings;const auto original=b;
        check(!ui::bind_pad(b,target,ui::pad_menu)&&b==original,"R3 rebind rejected transactionally");
        b[target]=ui::pad_menu;
        check(!ui::valid_pad_bindings(b),"legacy R3 binding invalid");
        check(ui::mapped_pad_buttons({true,ui::pad_menu},b)==0,"R3 stripped even with stale direct bindings");
        auto combined=b;combined[target]|=ui::pad_a;
        check(ui::mapped_pad_buttons({true,ui::pad_menu},combined)==0,"R3 stripped from combined stale bindings");
        ui::Config c;c.pad=b;
        check(ui::parse_config(ui::serialize_config(c)).pad==original,"writer migrates reserved R3 binding to defaults");
        const auto text="version=1\npad"+std::to_string(target)+"=128\nend=1\n";
        check(ui::parse_config(text).pad==original,"loader migrates legacy R3 binding to defaults");
    }
    for(unsigned mask=0;mask<65536;++mask){
        const ui::PadSnapshot p{true,mask},with_menu{true,mask|ui::pad_menu};
        check(ui::mapped_pad_buttons(p,ui::default_pad_bindings)==ui::mapped_pad_buttons(with_menu,ui::default_pad_bindings),"system button never contributes NES bits");
        check(ui::pad_buttons(mask,0,0)==ui::pad_buttons(mask|ui::pad_menu,0,0),"legacy sampler also excludes R3");
    }
    for(unsigned mask:{0u,ui::pad_a,ui::pad_b,0x10u,0x20u,0xfu}){
        Frontend f;f.tick({true,mask});const auto e=f.tick({true,mask|ui::pad_menu});
        check(e.changed&&f.state.page==ui::Page::pause&&ui::should_pause(f.state,f.config),"R3 edge opens pause while playing, including held NES controls");
        for(unsigned i=0;i<100;++i){f.tick({true,mask|ui::pad_menu});check(f.state.page==ui::Page::pause,"held R3 does not resume");}
        f.tick({true,mask});f.tick({true,mask|ui::pad_menu});
        check(f.state.page==ui::Page::game&&!ui::should_pause(f.state,f.config),"next R3 edge resumes");
        f.tick({true,mask|ui::pad_menu});check(f.state.page==ui::Page::game,"held R3 does not reopen menu");
    }
    for(auto page:{ui::Page::video,ui::Page::audio,ui::Page::controls,ui::Page::advanced})for(bool session:{false,true}){
        Frontend f;f.state.page=page;f.state.session=session;f.press(ui::pad_menu);
        check(f.state.page==(session?ui::Page::pause:ui::Page::home),"R3 unwinds settings safely");
        f.tick({true,ui::pad_menu});check(f.state.page==(session?ui::Page::pause:ui::Page::home),"unwinding does not cascade on hold");
    }
    {
        Frontend f;f.state.page=ui::Page::home;f.state.session=false;f.press(ui::pad_menu);
        check(f.state.page==ui::Page::home,"R3 at home never creates a session");
        f.state.page=ui::Page::pause;f.state.session=true;f.state.error=L"Load failed";f.press(ui::pad_menu);
        check(f.state.error.empty()&&f.state.page==ui::Page::pause,"R3 dismisses error without resuming");
    }
    for(bool pad:{false,true}){
        Frontend f;f.state.page=ui::Page::controls;
        (pad?f.state.pad_binding:f.state.binding)=0;const auto saved=f.config;
        f.press(ui::pad_menu);
        check(f.state.pad_binding<0&&f.state.binding<0&&f.state.page==ui::Page::controls&&f.config==saved,"R3 cancels capture without binding or navigating");
        f.tick({true,ui::pad_menu});check(f.state.page==ui::Page::controls,"capture cancellation consumes held edge");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::pause,"new edge after cancelled capture unwinds");
    }
    {
        ui::BindingCapture capture;unsigned code=777;capture.begin();capture.update({true},0,code);
        check(capture.update({true,ui::pad_menu|ui::pad_a},1,code)==ui::CaptureResult::cancelled&&code==777,"reserved R3 cancels standalone capture before other buttons");
        capture.begin();check(capture.update({true,ui::pad_menu},2,code)==ui::CaptureResult::listening,"held R3 on capture entry needs release");
    }
    for(unsigned gate=0;gate<5;++gate){
        Frontend f;f.tick({true});
        if(gate==0)f.tick({});
        if(gate==1)f.state.focused=false;
        if(gate==2)f.state.minimized=true;
        if(gate==3)f.state.loading=true;
        if(gate==4)f.config.gamepad_enabled=false;
        if(gate)f.tick({true,ui::pad_menu});
        check(f.state.page==ui::Page::game,"inactive context does not open menu");
        f.state.focused=true;f.state.minimized=f.state.loading=false;f.config.gamepad_enabled=true;
        f.tick({true,ui::pad_menu});check(f.state.page==ui::Page::game,"held on reconnect/reactivation does not open menu");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::pause,"release rearms after reconnect/reactivation");
        f.tick({});check(!f.state.gamepad_connected&&f.state.page==ui::Page::pause,"disconnect preserves paused menu");
        f.tick({true,ui::pad_menu});check(f.state.page==ui::Page::pause,"held reconnect cannot resume");
        f.press(ui::pad_menu);check(f.state.page==ui::Page::game,"fresh reconnect edge resumes");
    }
    for(unsigned button:{0x10u,ui::pad_b,ui::pad_a}){
        Frontend f;f.press(ui::pad_menu);f.press(button);
        check(f.state.page==ui::Page::game,"Start, B and confirm Resume remain functional after R3 entry");
    }
    for(bool stick:{false,true}){
        Frontend f;f.press(ui::pad_menu);f.tick({true});
        auto p=ui::PadSnapshot{true};if(stick)p.y=-20000;else p.buttons=2;
        f.tick(p);check(f.state.selection!=0,"D-pad and left stick navigate R3-opened menu");
        auto l=ui::layout(f.state,f.config,960,700,96);
        for(unsigned i=0;i<l.count;++i)if(l.items[i].command==ui::Command::video)f.state.selection=int(i);
        f.press(ui::pad_a);check(f.state.page==ui::Page::video,"controller confirms Video from opened menu");
        f.press(ui::pad_b);check(f.state.page==ui::Page::pause,"B returns from Video");
        f.press(0x10);check(f.state.page==ui::Page::game,"Start still resumes after navigation");
    }
    {
        Frontend f;f.tick({true});f.tick({true,ui::pad_menu,0,0,255,0});
        check(f.state.page==ui::Page::pause&&f.config.visual_mode==3,"menu edge wins over simultaneous trigger");
        f.tick({true,0,0,0,255,0});f.tick({true,ui::pad_menu,0,0,255,0});
        check(f.state.page==ui::Page::game&&f.config.visual_mode==3,"held trigger does not toggle on resume");
        f.tick({true,0,0,0,255,0});check(f.config.visual_mode==3,"trigger needs release after menu");
    }
}
}
int main(int argc,char** argv){
    try{
        if(argc>=3&&std::string_view(argv[1])=="product")product(std::filesystem::path(argv[2]));
        else if(argc==2&&std::string_view(argv[1])=="menu")menu();
        else throw std::runtime_error("expected product <output> or menu");
        std::cout<<"CHECKS="<<checks<<" RESULT=PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"CHECKS="<<checks<<" FAIL="<<e.what()<<'\n';return 1;}
}
