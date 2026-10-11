#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <frontend_ui.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace vkrt::nes;
namespace ui=vkrt::nes::frontend;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,std::uint64_t> failed;
void check(bool ok,const char* label){++checks;if(!ok){++failures;++failed[label];}}
std::uint32_t rng=0x19765123;
unsigned random_value(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
bool overlaps(ui::Rect a,ui::Rect b){return a.w>0&&a.h>0&&b.w>0&&b.h>0&&a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
void configs(){
    const ui::Config defaults;
    check(ui::parse_config(ui::serialize_config(defaults))==defaults,"default round trip");
    for(unsigned i=0;i<4096;++i){
        ui::Config c;c.visual_mode=i%2?3:0;c.integer_scaling=i&4;c.fullscreen=i&8;c.stats=i&16;c.pause_on_focus_loss=i&32;
        std::rotate(c.keys.begin(),c.keys.begin()+i%8,c.keys.end());
        for(unsigned j=0;j<i%12;++j)ui::remember(c,L"C:\\Library\\\u65e5\u672c\u8a9e\\\u00f1_"+std::to_wstring((i+j)%29)+L"_\U0001f3ae.nes");
        auto encoded=ui::serialize_config(c);auto decoded=ui::parse_config(encoded);
        check(decoded==c,"Unicode config scalar/map/recent round trip");
        check(ui::serialize_config(decoded)==encoded,"canonical serializer repeat");
        check(encoded.size()<=ui::max_config_bytes,"serialized config bound");
        check(encoded.starts_with("version=1\n")&&encoded.ends_with("end=1\n"),"version and complete-write marker");
        auto extra=encoded;extra.insert(extra.size()-6,"unknown=value\nrom_contents=never_persist_this\nvolume=999\npresenter=flip\n");
        check(ui::parse_config(extra)==c,"unknown fields and diagnostic presenter ignored");
        check(ui::serialize_config(ui::parse_config(extra)).find("never_persist_this")==std::string::npos,"no ROM payload persistence");
        check(ui::serialize_config(ui::parse_config(extra)).find("presenter")==std::string::npos,"no persisted presenter override");
        for(const auto& path:c.recents)check(ui::from_utf8(ui::utf8(path))==path,"strict Unicode path codec");
    }
    ui::Config c;
    for(unsigned i=0;i<31;++i)ui::remember(c,L"C:\\ROMs\\"+std::to_wstring(i)+L".nes");
    check(c.recents.size()==10,"recents bounded to ten");
    for(unsigned i=0;i<10;++i)check(c.recents[i]==L"C:\\ROMs\\"+std::to_wstring(30-i)+L".nes","MRU descending order");
    ui::remember(c,L"c:/roms/./28.NES");
    check(c.recents.size()==10&&c.recents[0]==L"c:\\roms\\28.NES","case slash dot dedupe");
    check(c.recents[1]==L"C:\\ROMs\\30.nes"&&c.recents[2]==L"C:\\ROMs\\29.nes","dedupe preserves other relative order");
    auto before=c;
    for(const auto* path:{L"relative.nes",L"C:\\bad.bin",L"C:\\a\n.nes",L"C:\\bad?.nes",L""}){ui::remember(c,path);check(c==before,"invalid recent ignored");}
    check(!ui::normalized_rom_path(L"\\\\server\\share\\\u4e00.nes").empty(),"UNC Unicode path");
    ui::remember(c,std::wstring(1100,L'a')+L".nes");check(c==before,"overlong recent ignored");
    const auto sample=ui::serialize_config(c);
    for(std::size_t cut=0;cut<sample.size();++cut)check(ui::parse_config(std::string_view(sample).substr(0,cut))==defaults,"every truncation defaults safely");
    check(ui::parse_config(std::string(ui::max_config_bytes+1,'x'))==defaults,"oversized config rejected");
    for(auto bad:{"", "version=2\nend=1\n", "version=1\r\nend=1\r\n", "version=1\nmode=-1\nend=1\n", "version=1\nmode=429496729600\nend=1\n"})
        check(ui::parse_config(bad)==defaults,"malformed or unsupported config safe");
    check(ui::parse_config("version=1\nkey0=88\nkey1=88\nend=1\n").keys==ui::default_bindings,"config duplicate keys reset whole map");
    check(ui::parse_config("version=1\nkey0=27\nend=1\n").keys==ui::default_bindings,"config reserved key resets map");
    for(unsigned i=0;i<2048;++i){
        std::string mutated=sample;mutated[random_value()%mutated.size()]=char(random_value()%256);
        auto result=ui::parse_config(mutated);
        check((result.visual_mode==0||result.visual_mode==3)&&ui::valid_bindings(result.keys),"mutated config bounded settings");
        check(result.recents.size()<=ui::max_recents,"mutated config bounded recents");
        check(ui::parse_config(ui::serialize_config(result))==result,"mutated input canonicalizes to stable config");
    }
    bool rejected=false;try{ui::from_utf8(std::string("\xc0\xaf",2));}catch(...){rejected=true;}check(rejected,"overlong UTF-8 rejected");
    rejected=false;try{ui::utf8(std::wstring(1,wchar_t(0xd800)));}catch(...){rejected=true;}check(rejected,"unpaired surrogate rejected");
}
void disk(){
    auto root=std::filesystem::temp_directory_path()/(L"vkrt-m5-config-test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);auto file=root/L"\u8a2d\u5b9a.ini";std::wstring error;
    ui::Config c;c.visual_mode=0;ui::remember(c,L"C:\\\u30ed\u30e0\\\u00e9.nes");
    check(ui::save_config(file,c,error)&&error.empty(),"atomic initial Unicode file write");
    check(ui::load_config(file,error)==c&&error.empty(),"Unicode file load");
    c.stats=true;c.visual_mode=3;
    check(ui::save_config(file,c,error),"atomic replace existing file");
    check(ui::load_config(file,error)==c,"atomic replacement complete");
    auto blocked=file;blocked+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    {std::ofstream f(blocked);f<<"occupied";}
    auto next=c;next.visual_mode=0;
    check(!ui::save_config(file,next,error)&&!error.empty(),"temporary-file collision reported");
    check(ui::load_config(file,error)==c,"failed write retains previous config");
    std::filesystem::remove(blocked);
    {std::ofstream f(file,std::ios::binary|std::ios::trunc);f<<std::string(ui::max_config_bytes+1,'x');}
    check(ui::load_config(file,error)==ui::Config{}&&!error.empty(),"oversized file detected before reading payload");
    std::filesystem::remove(file);std::filesystem::remove(root);
}
void inputs(){
    check(ui::default_bindings==ui::Bindings{'Z','X',8,13,38,40,37,39},"R5.2 default NES serial map");
    for(unsigned button=0;button<8;++button)for(unsigned key=0;key<256;++key){
        auto b=ui::default_bindings;auto original=b;
        const bool conflict=std::find(b.begin(),b.end(),key)!=b.end()&&b[button]!=key;
        const bool expected=ui::bindable(key)&&!conflict;
        check(ui::bind_key(b,button,key)==expected,"binding acceptance and conflict contract");
        check(ui::valid_bindings(b),"binding edit always valid");
        if(!expected)check(b==original,"rejection leaves complete map unchanged");
        else {check(b[button]==key,"requested key installed");for(unsigned i=0;i<8;++i)if(i!=button)check(b[i]==original[i],"other keys unaffected");}
    }
    for(unsigned mask=0;mask<256;++mask){
        std::array<bool,256> down{};for(unsigned i=0;i<8;++i)down[ui::default_bindings[i]]=(mask&(1<<i))!=0;
        unsigned expected=mask;if((mask&48)==48)expected&=~48u;if((mask&192)==192)expected&=~192u;
        check(ui::keyboard_buttons(ui::default_bindings,down)==expected,"keyboard complete input truth table");
        check(ui::resolve_directions(std::uint8_t(mask))==expected,"opposing directions resolve neutral");
    }
    check(ui::pad_buttons(0x1000,0,0)==1,"XInput A maps A");check(ui::pad_buttons(0x4000,0,0)==2,"XInput X maps B");
    check(ui::pad_buttons(0x2000,0,0)==2,"XInput B maps B");check(ui::pad_buttons(0x30,0,0)==12,"XInput Start Back map");
    for(int x=-32768;x<=32767;x+=257)for(int y=-32768;y<=32767;y+=257){
        unsigned oracle=(y>16000?16:y<-16000?32:0)|(x>16000?128:x<-16000?64:0);
        check(ui::pad_buttons(0,x,y)==oracle,"stick deadzone boundary oracle");
    }
}
void commands(){
    constexpr Mode modes[]{Mode::EXACT_DIGITAL,Mode::EXACT_DIGITAL,Mode::EXACT_DIGITAL,Mode::MODERN_REMASTER};
    for(unsigned k=0;k<256;++k)check(ui::shortcut_mode(k)==(k==112?0:k==115?3:-1),"F1 F4 exclusive shortcut mapping");
    for(unsigned i=0;i<4;++i){
        ui::State s;ui::Config c;auto e=ui::route(s,c,ui::Command(unsigned(ui::Command::mode0)+i));
        const bool allowed=i==0||i==3;
        check(c.visual_mode==(allowed?i:3)&&e.save==allowed&&!e.reset,"only product modes change frontend preference");
        check(ui::visual_mode(i)==modes[i],"exact qualified mode enum mapping including modern ordinal 5");
    }
    for(unsigned i=4;i<256;++i)check(ui::visual_mode(i)==Mode::EXACT_DIGITAL,"bad visual preference uses Original");
    for(unsigned p=0;p<7;++p)for(unsigned flags=0;flags<64;++flags){
        ui::State s;s.page=ui::Page(p);s.session=flags&1;s.focused=flags&2;s.minimized=flags&4;s.loading=flags&8;
        if(flags&16)s.error=L"error";ui::Config c;c.pause_on_focus_loss=flags&32;
        bool expected=!s.session||p!=unsigned(ui::Page::game)||s.loading||!s.error.empty()||((s.minimized||!s.focused)&&c.pause_on_focus_loss);
        check(ui::should_pause(s,c)==expected,"focus pause truth table independent of user menu pause");
        auto previous=s;ui::escape(s);
        if(!previous.error.empty())check(s.error.empty()&&s.page==previous.page,"escape dismisses error without resuming");
        else if(previous.loading)check(s.page==previous.page,"loading cannot resume incomplete session");
        else if(previous.page==ui::Page::game)check(s.page==ui::Page::pause,"escape pauses game");
        else if(previous.page==ui::Page::pause)check(s.page==ui::Page::game,"escape resumes pause");
        else if(previous.page!=ui::Page::home)check(s.page==(previous.session?ui::Page::pause:ui::Page::home),"settings return respects session");
    }
    for(unsigned command=0;command<90;++command)for(bool loading:{false,true}){
        ui::State s;s.session=true;s.page=ui::Page::pause;s.loading=loading;ui::Config c;auto original=c;
        auto e=ui::route(s,c,ui::Command(command));
        if(loading)check(c==original&&!e.open&&!e.reset&&!e.quit&&!e.save,"commands blocked while staging");
        else if(command==unsigned(ui::Command::open))check(e.open&&!e.quit,"open routes to dialog");
        else if(command==unsigned(ui::Command::reset))check(e.reset&&s.page==ui::Page::game,"reset routes warm reset");
        else if(command==unsigned(ui::Command::quit))check(e.quit,"quit route");
        else if(command==unsigned(ui::Command::fullscreen))check(c.fullscreen&&e.fullscreen&&e.save,"fullscreen applies and saves");
        else if(command==unsigned(ui::Command::defaults))check(c.keys==ui::default_bindings&&e.save,"restore defaults route");
    }
    ui::State s;s.session=true;s.replay=true;ui::Config c;
    check(!ui::route(s,c,ui::Command::reset).reset,"replay rejects out of tape reset");
    s.binding=3;s.page=ui::Page::controls;ui::escape(s);check(s.binding==-1&&s.page==ui::Page::controls,"binding escape cancels capture only");
    for(unsigned i=0;i<10;++i){ui::remember(c,L"C:\\r\\"+std::to_wstring(i)+L".nes");}
    for(unsigned i=0;i<10;++i)check(ui::route(s,c,ui::Command(unsigned(ui::Command::recent0)+i)).recent==int(i),"recent routes exact bounded index");
}
void geometry(){
    ui::Config c;for(int i=0;i<10;++i)ui::remember(c,L"C:\\ROMs\\"+std::to_wstring(i)+L".nes");
    constexpr unsigned dpis[]{48,72,96,120,144,168,192,240,288,384,768};
    for(unsigned iteration=0;iteration<9000;++iteration){
        int w=1+int(random_value()%8192),h=1+int(random_value()%4320);unsigned dpi=dpis[iteration%11];
        ui::State s;s.page=ui::Page(iteration%7);s.session=s.page!=ui::Page::home;s.replay=iteration%5==0;
        if(iteration%17==0)s.error=L"test";
        auto l=ui::layout(s,c,w,h,dpi);
        check(l.count<=l.items.size(),"bounded layout storage");
        check(ui::hit_test(l,-1,-1)==-1&&ui::hit_test(l,w,h)==-1,"outside canvas never hit");
        for(unsigned i=0;i<l.count;++i){
            const auto& item=l.items[i];auto r=item.rect;
            check(r.x>=0&&r.y>=0&&r.w>=0&&r.h>=0&&r.x+r.w<=w&&r.y+r.h<=h,"hit target within viewport at every DPI");
            check(!item.label.empty()&&item.command!=ui::Command::none,"every control labelled and routed");
            for(unsigned j=i+1;j<l.count;++j)check(!overlaps(r,l.items[j].rect),"no overlapping controls");
            if(r.w>0&&r.h>0){
                for(int x:{r.x,r.x+r.w/2,r.x+r.w-1})for(int y:{r.y,r.y+r.h/2,r.y+r.h-1})
                    check(ui::hit_test(l,x,y)==(item.enabled?int(i):-1),"control interior hit grid");
                check(ui::hit_test(l,r.x+r.w,r.y)!=int(i)&&ui::hit_test(l,r.x,r.y+r.h)!=int(i),"half-open right and bottom edges");
            }
        }
        for(bool integer:{false,true}){
            auto v=ui::viewport(w,h,integer);
            check(v.x>=0&&v.y>=0&&v.w>0&&v.h>0&&v.x+v.w<=w&&v.y+v.h<=h,"game viewport contained");
            check(std::abs((w-v.w)-2*v.x)<=1&&std::abs((h-v.h)-2*v.y)<=1,"game viewport centered");
            if(integer&&w>=512&&h>=480){
                int factor=std::min(w/512,h/480);
                check(v.w==512*factor&&v.h==480*factor,"largest whole frozen-image scale");
                check((v.w+512>w)||(v.h+480>h),"integer viewport maximal");
            }else check(std::abs(std::int64_t(v.w)*480-std::int64_t(v.h)*512)<=512,"fractional fallback aspect within one pixel");
            auto g=ui::game_viewport(w,h,integer,l.scale);
            check(g.x>=0&&g.y>=0&&g.x+g.w<=w&&g.y+g.h<=h,"toolbar reserved outside game viewport");
        }
    }
    check(ui::viewport(0,100,true)==ui::Rect{}&&ui::viewport(100,0,true)==ui::Rect{},"zero client extent safe");
    for(unsigned i=0;i<10000;++i){
        ui::FullscreenState f;ui::Rect window{int(random_value()%5000)-2500,int(random_value()%3000)-1500,800+int(i%900),600+int(i%600)};
        ui::Rect monitor{-1920,0,1920,1080};
        check(f.transition(false,window,monitor)==window&&!f.active,"windowed fullscreen no-op");
        check(f.transition(true,window,monitor)==monitor&&f.active&&f.restore==window,"fullscreen captures exact original rectangle");
        check(f.transition(true,monitor,monitor)==monitor&&f.restore==window,"repeated fullscreen does not overwrite restore");
        check(f.transition(false,monitor,monitor)==window&&!f.active,"fullscreen restores original rectangle");
    }
}
void probes(){
    auto first=ui::ui_probe({}),second=ui::ui_probe({});
    check(first==second,"UI probe repeated complete image hashes identical");
    check(std::set<std::uint64_t>(first.begin(),first.end()).size()==4,"four distinct UI states");
    ui::Canvas a,b;ui::State state;ui::Config config;
    for(unsigned page=0;page<7;++page){
        state.page=ui::Page(page);auto l=ui::layout(state,config,960,700,96);
        a.draw(state,config,l,960,700);b.draw(state,config,l,960,700);
        check(a.hash()==b.hash(),"independent compositor instances agree");
        for(std::size_t i=3;i<a.pixels().size();i+=4096)check(a.pixels()[i]==255,"DIB alpha initialized across surface");
    }
}
void source_policy(){
    auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
    for(const char* name:{"frontend_ui.hpp","frontend_ui.cpp","frontend_config.hpp","frontend_config.cpp","frontend_input.hpp","frontend_input.cpp","runtime_main.cpp"}){
        std::ifstream f(root/"app/windows"/name);std::string s((std::istreambuf_iterator<char>(f)),{});check(!s.empty(),"frontend source available");
        for(const char* token:{"Super " "Mario","SM" "B3","Nintendo","4377a7f5","tile_id ==","screen_x ==","rom_contents=","Save State","Load State"})
            check(s.find(token)==std::string::npos,"no game identity content persistence or fake state control literals");
        if(std::string(name).starts_with("frontend_"))check(s.find("Machine")==std::string::npos||std::string(name)=="frontend_ui.hpp","frontend implementation has no Machine authority");
    }
}
}
int main(){
    try{configs();disk();inputs();commands();geometry();probes();source_policy();}
    catch(const std::exception& e){check(false,e.what());}
    check(checks>=500000,"meaningful deterministic check floor");
    for(const auto& [name,count]:failed)std::cerr<<"FAIL "<<name<<" COUNT="<<count<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
