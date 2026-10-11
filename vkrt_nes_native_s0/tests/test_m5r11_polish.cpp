#include <frontend_ui.hpp>
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/display_reference.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>
using namespace vkrt::nes;
namespace ui=vkrt::nes::frontend;
namespace {
std::uint64_t checks{},failures{};
void check(bool ok,const char* label){++checks;if(!ok&&++failures<=24)std::cerr<<label<<'\n';}
void equal_bytes(const Bytes& a,const Bytes& b,const char* label){
    check(a.size()==b.size(),label);
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i)check(a[i]==b[i],label);
}
ui::PadSnapshot neutral(){return {true};}
// Opaque shapes on visible backdrop reproduce the old ownership veto without
// any title, ROM, tile number, or hidden geometry. Sprite and tile owners vary.
Frame silhouette(unsigned variant){
    Frame f;f.number=variant+1;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const int i=y*width+x;auto& s=f.samples[i];s.x=std::uint16_t(x);s.y=std::uint16_t(y);s.dot=std::uint16_t(x+1);
        const int lx=x%32,ly=y%32;
        const bool solid=variant%2?(lx>=12&&lx<20&&ly>=12&&ly<20&&lx+ly>=26&&lx+ly<=36):(lx>=12&&lx<20&&ly>=12&&ly<20);
        s.layer=solid?(variant<2?Layer::background:Layer::sprite):Layer::backdrop;
        s.bg_opaque=solid&&variant<2;s.sprite_opaque=solid&&variant>=2;
        auto& src=variant<2?s.background:s.sprite;
        src.origin_x=std::int16_t(x/32*32);src.origin_y=std::int16_t(y/32*32);
        src.local_x=std::uint8_t(x%8);src.local_y=std::uint8_t(y%8);
        s.color=solid?std::uint8_t(0x21+variant):0x0f;
        s.emphasis=std::uint8_t(variant==3?1:0);f.indices[i]=s.color;f.effects[i]=std::uint8_t(s.emphasis<<5);
    }
    return *observe(f);
}
void rendering(){
    for(unsigned variant=0;variant<4;++variant){
        const auto f=silhouette(variant);
        const auto a=render_reference(f,Mode::EXACT_DIGITAL,512,480);
        const auto b=render_reference(f,Mode::SEMANTIC_HD,512,480);
        const auto c=render_reference(f,Mode::MODERN_REMASTER,512,480);
        check(a!=b,"F1/F3 actual silhouette pixels differ");check(b!=c,"F3/F4 actual pixels differ");
        equal_bytes(a.rgb,render_reference(f,Mode::EXACT_DIGITAL,512,480).rgb,"F1 roundtrip every channel");
        equal_bytes(b.rgb,render_reference(f,Mode::SEMANTIC_HD,512,480).rgb,"F3 deterministic every channel");
        // The legacy path on these fixtures is exactly a no-op: the fix is in
        // the product presentation renderer, not in enum routing or source color.
        check(render(f,Mode::SEMANTIC_HD,512,480)==render(f,Mode::EXACT_DIGITAL,512,480),"old silhouette no-op reproduced");
        unsigned changed=0;
        for(int y=0;y<480;++y)for(int x=0;x<512;++x){
            const int src=(y/2)*width+x/2;const auto anchor=reference_display_color(f.indices[src],f.effects[src]);
            const auto outside=reference_display_color(0x0f,f.effects[src]);
            for(int k=0;k<3;++k){const auto value=b.rgb[(y*512+x)*3+k];
                check(a.rgb[(y*512+x)*3+k]==anchor[k],"F1 exact source palette anchor");
                check(value>=std::min(anchor[k],outside[k])&&value<=std::max(anchor[k],outside[k]),"F3 convex source colors without lighting");
                changed+=value!=anchor[k];
            }
        }
        check(changed>100,"F3 broad fixture visibly nonzero coverage");
        if(variant%2==0){
            const auto base=reference_display_color(f.indices[12*width+12],f.effects[12*width+12]);
            const auto donor=reference_display_color(0x0f,f.effects[12*width+12]);
            for(int k=0;k<3;++k){
                check(b.rgb[(24*512+24)*3+k]==(unsigned(base[k])+donor[k]+1)/2,"analytic half-covered 2x silhouette corner");
                check(b.rgb[(25*512+25)*3+k]==base[k],"canonical corner cell retains its exact center-facing sample");
            }
        }
        // Source history does not alter reconstruction or introduce flicker.
        auto history=f;history.number+=100;
        for(auto& s:history.samples){s.reactive=Reactive::ambiguous;s.confidence=0;s.motion_x=23;}
        check(render_reference(history,Mode::SEMANTIC_HD,512,480)==b,"history-independent stable shape reconstruction");
    }
    Machine m(synthetic_nrom());
    constexpr const char* f1[]{"69a4510ca6feddea6349fb211f63ffaf11d263dd003383efa79826a804b9931a","d6db1a8adf1e7352e92558ba3b05539ff93d99b324b6d1ec9b8a05ad742918b1"};
    constexpr const char* f4[]{"eb055743e782240122a0b3d584248178f16e982989990df5f61565788822d457","e472225b75133850260951b74d710db5861693b17f12e78fbd7510597b9d452d"};
    for(unsigned n=1;n<=120;++n){m.run_frames(1);m.apu.pcm.clear();if(n!=60&&n!=120)continue;
        const auto canonical=m.serialize(),state=m.save_state();const auto frame=observe(*m.frame());
        const auto first=render_reference(*frame,Mode::EXACT_DIGITAL,512,480);
        check(sha256(first.rgb)==f1[n/60-1],"F1 pre-edit qualified hash");
        Image hd,modern;
        for(auto mode:{Mode::SEMANTIC_HD,Mode::MODERN_REMASTER,Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL}){
            auto image=render_reference(*frame,mode,512,480);
            if(mode==Mode::SEMANTIC_HD)hd=image;if(mode==Mode::MODERN_REMASTER)modern=image;
            if(mode==Mode::EXACT_DIGITAL)equal_bytes(first.rgb,image.rgb,"native F1 -> F3 -> F4 -> F1 exact bytes");
            check(m.serialize()==canonical,"switching leaves canonical state unchanged");
            check(m.save_state()==state,"switching leaves complete serialized state unchanged");
        }
        check(first!=hd&&hd!=modern,"actual PPU semantic output distinct");
        check(sha256(modern.rgb)==f4[n/60-1],"F4 pre-edit R5.2 qualified hash");
    }
}
void navigation(){
    using D=ui::Direction;
    for(auto [mask,dir]:std::array<std::pair<unsigned,D>,4>{{{1,D::up},{2,D::down},{4,D::left},{8,D::right}}}){
        for(unsigned sample_ms:{1u,7u,16u,33u,117u,1000u}){
            ui::NavigationModel nav;auto p=neutral();nav.update(p,0,true);p.buttons=mask;
            check(nav.update(p,10,true).move==dir,"one initial move");
            std::uint64_t expected=310;
            for(std::uint64_t t=11;t<=6000;t+=sample_ms){
                const bool fire=t>=expected;
                check(nav.update(p,t,true).move==(fire?dir:D::none),"wall-clock repeat cadence with no catchup burst");
                if(fire)expected=t+110;
            }
            p.buttons=0;check(nav.update(p,7000,true).move==D::none,"neutral resets repeat");
            p.buttons=mask;check(nav.update(p,7001,true).move==dir,"new deflection immediate");
        }
    }
    for(int x=-32768;x<=32767;x+=127)for(int y=-32768;y<=32767;y+=127){
        ui::NavigationModel nav;nav.update(neutral(),0,true);auto p=neutral();p.x=x;p.y=y;
        const int ax=std::abs(x),ay=std::abs(y);
        D expected=std::max(ax,ay)<=16000?D::none:ax>ay?(x<0?D::left:D::right):(y<0?D::down:D::up);
        check(nav.update(p,1,true).move==expected,"stick deadzone and dominant-axis oracle");
    }
    ui::NavigationModel nav;auto p=neutral();nav.update(p,0,true);p.x=16001;
    check(nav.update(p,1,true).move==D::right,"stick enters outside deadzone");p.x=15000;
    check(nav.update(p,301,true).move==D::right,"stick hysteresis holds steady");p.x=12000;
    check(nav.update(p,302,true).move==D::none,"stick re-centers");
    p={true,ui::pad_a};check(nav.update(p,303,true).confirm,"A confirm");check(!nav.update(p,304,true).confirm,"A edge only");
    p.buttons=ui::pad_b;check(nav.update(p,305,true).back,"B back");
    check(!nav.update({},306,true).back,"disconnect emits nothing");
    check(!nav.update(p,307,true).back,"reconnect held button cannot act");
    nav.update(neutral(),308,true);check(nav.update(p,309,true).back,"reconnect neutral then new press");
    ui::NavigationModel diagonal;diagonal.update(neutral(),0,true);p={true,0,18000,18001};
    check(diagonal.update(p,1,true).move==D::up,"diagonal initial axis");
    unsigned next=301;
    for(unsigned t=2;t<10000;++t){p.x=18000+int(t%3);p.y=18000+int((t+1)%3);
        const bool repeat=t>=next;
        check(diagonal.update(p,t,true).move==(repeat?D::up:D::none),"diagonal noise cannot create frame-dependent axis runaway");
        if(repeat)next=t+110;
    }
    for(unsigned page=0;page<7;++page){
        ui::State s;s.page=ui::Page(page);s.session=page!=0;s.gamepad_connected=true;
        ui::Config c;for(unsigned i=0;i<10;++i)ui::remember(c,L"C:\\fixtures\\"+std::to_wstring(i)+L".nes");
        auto l=ui::layout(s,c,1100,800,96);std::set<int> reached{0};std::queue<int> work;work.push(0);
        while(!work.empty()){auto index=work.front();work.pop();for(auto dir:{D::up,D::down,D::left,D::right}){
            s.selection=index;ui::navigate(s,l,dir);if(reached.insert(s.selection).second)work.push(s.selection);
        }}
        for(unsigned i=0;i<l.count;++i)if(l.items[i].enabled)check(reached.contains(int(i)),"every page control reachable with D-pad including states/tabs");
        for(unsigned i=0;i<l.count;++i)if(l.items[i].enabled&&page!=1){
            ui::ControllerUiModel model;s.selection=int(i);model.update(s,c,l,neutral(),0);
            auto action=model.update(s,c,l,{true,ui::pad_a},1);
            check(action.command==l.items[i].command,"controller A targets exact focused control");
            check(model.update(s,c,l,{true,ui::pad_a},2).command==ui::Command::none,"held A cannot reactivate");
        }
    }
    ui::State s;s.page=ui::Page::pause;s.session=true;ui::Config c;ui::ControllerUiModel model;
    auto l=ui::layout(s,c,1100,800,96);model.update(s,c,l,neutral(),0);
    ui::move_selection(s,l,1); // keyboard owns focus; controller must use it.
    check(model.update(s,c,l,{true,ui::pad_a},1).command==l.items[1].command,"keyboard controller focus coexistence");
    model.update(s,c,l,neutral(),2);
    check(model.update(s,c,l,{true,0x10},3).command==ui::Command::resume,"Start resumes Pause");
    model.update(s,c,l,neutral(),4);model.update(s,c,l,{true,ui::pad_b},5);
    check(s.page==ui::Page::game,"B closes pause");
}
void rebinding(){
    for(unsigned target=0;target<4;++target)for(unsigned code:ui::pad_choices){
        ui::BindingCapture capture;capture.begin();unsigned captured=0;
        auto p=neutral();p.buttons=ui::pad_a;
        check(capture.update(p,0,captured)==ui::CaptureResult::listening,"activation press not captured");
        capture.update(neutral(),1,captured);p=neutral();
        if(code==ui::pad_lt)p.lt=153;else if(code==ui::pad_rt)p.rt=153;else p.buttons=code;
        auto result=capture.update(p,2,captured);
        if(code==ui::pad_b){check(result==ui::CaptureResult::listening,"B tap pending cancellation");result=capture.update(p,602,captured);}
        check(result==ui::CaptureResult::captured&&captured==code,"all eligible XInput controls captured");
        auto b=ui::default_pad_bindings,original=b;bool conflict=false;
        for(unsigned i=0;i<4;++i)if(i!=target&&(b[i]&code))conflict=true;
        check(ui::bind_pad(b,target,captured)==!conflict,"deterministic reject duplicate policy");
        if(conflict)check(b==original,"rejected binding retains full map");
        else check(b[target]==code&&ui::valid_pad_bindings(b),"captured mapping installed");
        ui::Config c;c.pad=b;check(ui::parse_config(ui::serialize_config(c))==c,"controller binding persistence roundtrip");
        for(unsigned mask=0;mask<65536;mask+=17){
            p={true,mask};unsigned expected=ui::pad_buttons(mask,0,0)&0xf0;
            for(unsigned i=0;i<4;++i)if(mask&b[i])expected|=1u<<i;
            check(ui::mapped_pad_buttons(p,b)==expected,"rebound gameplay truth table");
        }
    }
    for(unsigned initial=65;initial<=255;++initial){
        ui::BindingCapture capture;capture.begin();unsigned code=0;auto p=neutral();p.rt=initial;
        capture.update(p,0,code);p.rt=255;check(capture.update(p,1,code)==ui::CaptureResult::listening,"held trigger requires neutral before capture");
        p.rt=64;capture.update(p,2,code);p.rt=152;check(capture.update(p,3,code)==ui::CaptureResult::listening,"trigger below capture threshold");
        p.rt=153;check(capture.update(p,4,code)==ui::CaptureResult::captured&&code==ui::pad_rt,"neutral then trigger threshold captures");
    }
    ui::BindingCapture cap;unsigned code=0;cap.begin();cap.update(neutral(),0,code);
    cap.update({true,ui::pad_b},1,code);check(cap.update(neutral(),600,code)==ui::CaptureResult::cancelled,"tap B cancels safely");
    cap.begin();check(cap.update(neutral(),0,code,true)==ui::CaptureResult::cancelled,"Escape cancels");
    cap.begin();check(cap.update({},0,code)==ui::CaptureResult::cancelled,"disconnect cancels capture");
    ui::State s;s.page=ui::Page::controls;s.pad_binding=0;s.gamepad_connected=true;ui::Config c;ui::ControllerUiModel model;
    auto l=ui::layout(s,c,1100,800,96);model.update(s,c,l,neutral(),0);
    auto result=model.update(s,c,l,{true,0x8000},1);
    check(result.save&&s.pad_binding<0&&c.pad[0]==0x8000,"integrated binding persists");
    check(model.update(s,c,l,{true,ui::pad_a},2).command==ui::Command::none,"completion consumes input until full release");
    s.pad_binding=1;model.update(s,c,l,neutral(),3);model.update(s,c,l,{},4);
    check(s.pad_binding<0&&!s.gamepad_connected,"integrated disconnect cancellation");
    ui::route(s,c,ui::Command::defaults);check(c.pad==ui::default_pad_bindings&&c.keys==ui::default_bindings,"Restore Defaults both maps");
    check(ui::parse_config("version=1\npad0=0\nend=1\n").pad==ui::default_pad_bindings,"invalid pad config safe default");
}
void triggers(){
    for(unsigned lt=0;lt<256;++lt)for(unsigned rt=0;rt<256;++rt){
        ui::TriggerCycle cycle;cycle.update(neutral(),true,false,ui::default_pad_bindings);
        auto p=neutral();p.lt=lt;p.rt=rt;
        int expected=lt>=153&&rt>=153?0:lt>=153?-1:rt>=153?1:0;
        check(cycle.update(p,true,false,ui::default_pad_bindings)==expected,"all analog pairs threshold and simultaneous oracle");
        check(cycle.update(p,true,false,ui::default_pad_bindings)==0,"held trigger changes exactly once");
        for(bool capture:{false,true})check(cycle.update(p,false,capture,ui::default_pad_bindings)==0,"menu suppresses cycling");
    }
    for(unsigned bound=0;bound<3;++bound)for(unsigned released=0;released<153;++released){
        ui::PadBindings b=ui::default_pad_bindings;if(bound==1)b[0]=ui::pad_lt;if(bound==2)b[0]=ui::pad_rt;
        for(bool right:{false,true}){
            ui::TriggerCycle cycle;auto p=neutral();cycle.update(p,true,false,b);(right?p.rt:p.lt)=153;
            int expected=(right?bound==2:bound==1)?0:right?1:-1;
            check(cycle.update(p,true,false,b)==expected,"bound trigger suppresses only its own cycle");
            (right?p.rt:p.lt)=released;cycle.update(p,true,false,b);(right?p.rt:p.lt)=255;
            check(cycle.update(p,true,false,b)==(released<=64?expected:0),"hysteresis rearm boundary");
            check(cycle.update(p,true,true,b)==0,"capture suppresses cycling");
        }
    }
    for(unsigned mode=0;mode<4;++mode)for(bool right:{false,true}){
        ui::ControllerUiModel model;ui::State s;s.page=ui::Page::game;s.session=true;ui::Config c;c.visual_mode=mode;
        auto l=ui::layout(s,c,1100,800,96);model.update(s,c,l,neutral(),0);
        auto p=neutral();(right?p.rt:p.lt)=255;auto result=model.update(s,c,l,p,1);
        const unsigned expected=mode==3?0:3;
        check(result.command==ui::Command(unsigned(ui::Command::mode0)+expected),"LT and RT toggle only Original/Remaster");
        check(ui::route(s,c,result.command).save&&c.visual_mode==expected,"cycling persists preference");
        check(model.update(s,c,l,p,2).command==ui::Command::none,"integrated trigger hold one change");
        for(unsigned k=112;k<116;++k){
            const int slot=ui::shortcut_mode(k);const auto previous=c.visual_mode;
            auto effect=ui::route(s,c,slot<0?ui::Command::none:ui::Command(unsigned(ui::Command::mode0)+unsigned(slot)));
            check(effect.save==(slot>=0)&&c.visual_mode==(slot<0?previous:unsigned(slot)),"product keyboard routing after trigger");
        }
    }
    for(unsigned context=0;context<8;++context){
        ui::ControllerUiModel model;ui::State s;ui::Config c;s.page=ui::Page::game;s.session=true;
        if(context==0)s.page=ui::Page::pause;if(context==1)s.page=ui::Page::controls;
        if(context==2)s.binding=0;if(context==3)s.pad_binding=0;
        if(context==4)s.loading=true;if(context==5)s.error=L"error";
        if(context==6)s.focused=false;if(context==7)s.session=false;
        auto l=ui::layout(s,c,1100,800,96);model.update(s,c,l,neutral(),0);
        auto p=neutral();p.rt=255;auto result=model.update(s,c,l,p,1);
        check(result.command==ui::Command::none&&c.visual_mode==3,"all overlays capture focus and session gates suppress visual cycling");
    }
    for(unsigned keys=0;keys<256;++keys)for(unsigned pad=0;pad<256;++pad){
        unsigned expected=keys|pad;if((expected&48)==48)expected&=~48u;if((expected&192)==192)expected&=~192u;
        check(ui::combine_buttons(std::uint8_t(keys),std::uint8_t(pad))==expected,"keyboard controller gameplay coexistence");
    }
}
void policy(){
    auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
    for(const char* file:{"src/remaster.cpp","app/windows/frontend_input.cpp","app/windows/frontend_ui.cpp","app/windows/runtime_main.cpp"}){
        std::ifstream in(root/file);std::string source((std::istreambuf_iterator<char>(in)),{});check(!source.empty(),"source audit available");
        for(const char* token:{"Super " "Mario","SM" "B3","4377a7f5","title_screen","tile_id ==","screen_x ==","screen_y ==","onnx","torch"})
            check(source.find(token)==std::string::npos,"no title ROM or ML rules");
    }
    std::ostringstream out;ui::controller_diagnostics(out);check(out.str().find("TRIGGER_CYCLE=PASS")!=std::string::npos,"probe exercises production models");
}
}
int main(){
    try{rendering();navigation();rebinding();triggers();policy();}catch(const std::exception& e){check(false,e.what());}
    check(checks>=1000000,"meaningful polish check floor");
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
