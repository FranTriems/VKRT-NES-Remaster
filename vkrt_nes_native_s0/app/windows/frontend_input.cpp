#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <frontend_input.hpp>
#include <algorithm>
#include <cstdlib>

namespace vkrt::nes::frontend {
static_assert(pad_menu==XINPUT_GAMEPAD_RIGHT_THUMB);
bool bindable(unsigned k) noexcept {
    return (k>='A'&&k<='Z'&&k!='P') || (k>='0'&&k<='9') ||
        (k>=37&&k<=40) || k==8 || k==13 || k==32 ||
        (k>=96&&k<=111) || (k>=186&&k<=192) || (k>=219&&k<=222);
}
bool valid_bindings(const Bindings& b) noexcept {
    for(unsigned i=0;i<b.size();++i){
        if(!bindable(b[i]))return false;
        for(unsigned j=0;j<i;++j)if(b[i]==b[j])return false;
    }
    return true;
}
bool bind_key(Bindings& b,unsigned button,unsigned key) noexcept {
    if(button>=b.size()||!bindable(key))return false;
    for(unsigned i=0;i<b.size();++i)if(i!=button&&b[i]==key)return false;
    b[button]=key;return true;
}
std::wstring key_name(unsigned k){
    if(k==8)return L"Backspace";
    if(k==13)return L"Enter";
    if(k==32)return L"Space";
    if(k==37)return L"Left arrow";
    if(k==38)return L"Up arrow";
    if(k==39)return L"Right arrow";
    if(k==40)return L"Down arrow";
    if((k>='A'&&k<='Z')||(k>='0'&&k<='9'))return std::wstring(1,wchar_t(k));
    wchar_t name[80]{};
    const auto scan=MapVirtualKeyW(k,MAPVK_VK_TO_VSC);
    if(GetKeyNameTextW(LONG(scan<<16),name,80))return name;
    return L"Key "+std::to_wstring(k);
}
std::uint8_t resolve_directions(std::uint8_t b) noexcept {
    if((b&0x30)==0x30)b&=std::uint8_t(~0x30);
    if((b&0xc0)==0xc0)b&=std::uint8_t(~0xc0);
    return b;
}
std::uint8_t keyboard_buttons(const Bindings& b,const std::array<bool,256>& down) noexcept {
    std::uint8_t out=0;
    for(unsigned i=0;i<b.size();++i)if(b[i]<down.size()&&down[b[i]])out|=std::uint8_t(1u<<i);
    return resolve_directions(out);
}
std::uint8_t pad_buttons(unsigned b,int x,int y) noexcept {
    std::uint8_t out=0;
    if(b&XINPUT_GAMEPAD_A)out|=1;
    if(b&(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_B))out|=2;
    if(b&XINPUT_GAMEPAD_BACK)out|=4;
    if(b&XINPUT_GAMEPAD_START)out|=8;
    if((b&XINPUT_GAMEPAD_DPAD_UP)||y>16000)out|=16;
    if((b&XINPUT_GAMEPAD_DPAD_DOWN)||y<-16000)out|=32;
    if((b&XINPUT_GAMEPAD_DPAD_LEFT)||x<-16000)out|=64;
    if((b&XINPUT_GAMEPAD_DPAD_RIGHT)||x>16000)out|=128;
    return resolve_directions(out);
}
std::uint8_t combine_buttons(std::uint8_t keyboard,std::uint8_t controller) noexcept { return resolve_directions(keyboard|controller); }
void PadModel::update(bool enabled,bool connected,unsigned buttons,int x,int y) noexcept {
    connected_=enabled&&connected;buttons_=connected_?pad_buttons(buttons,x,y):0;
}
bool valid_pad_bindings(const PadBindings& b) noexcept {
    for(unsigned i=0;i<b.size();++i){
        if(std::find(pad_choices.begin(),pad_choices.end(),b[i])==pad_choices.end()&&!(i==1&&b[i]==0x6000))return false;
        for(unsigned j=0;j<i;++j)if(b[i]&b[j])return false;
    }
    return true;
}
bool bind_pad(PadBindings& b,unsigned button,unsigned code) noexcept {
    if(button>=b.size()||std::find(pad_choices.begin(),pad_choices.end(),code)==pad_choices.end())return false;
    auto candidate=b;candidate[button]=code;
    if(!valid_pad_bindings(candidate))return false;
    b=candidate;return true; // Reject duplicates; never silently steal an action.
}
std::wstring pad_name(unsigned code){
    constexpr const wchar_t* names[]{L"A",L"B",L"X",L"Y",L"LB",L"RB",L"L stick",L"Start/Menu",L"Back/View",L"LT",L"RT"};
    if(code==pad_menu)return L"R3 (emulator menu)";
    if(code==0x6000)return L"X / B";
    for(unsigned i=0;i<pad_choices.size();++i)if(code==pad_choices[i])return names[i];
    return L"Unbound";
}
std::uint8_t mapped_pad_buttons(const PadSnapshot& p,const PadBindings& b) noexcept {
    if(!p.connected)return 0;
    const unsigned pressed=(p.buttons&~pad_menu)|(p.lt>=153?pad_lt:0)|(p.rt>=153?pad_rt:0);
    auto out=std::uint8_t(pad_buttons(p.buttons,p.x,p.y)&0xf0);
    for(unsigned i=0;i<b.size();++i)if(pressed&b[i])out|=std::uint8_t(1u<<i);
    return out;
}
bool SystemMenuButton::update(const PadSnapshot& p,bool active) noexcept {
    // A held control on connect/focus return must be released before it can act.
    if(!active||!p.connected){armed_=false;return false;}
    if(!(p.buttons&pad_menu)){armed_=true;return false;}
    const bool edge=armed_;armed_=false;return edge;
}
namespace {
bool neutral(const PadSnapshot& p){return !p.buttons&&std::abs(p.x)<=12000&&std::abs(p.y)<=12000&&p.lt<=64&&p.rt<=64;}
Direction direction(const PadSnapshot& p,Direction held){
    int x=((p.buttons&8)!=0)-((p.buttons&4)!=0),y=((p.buttons&1)!=0)-((p.buttons&2)!=0);
    if(p.buttons&15){
        if(x)return x<0?Direction::left:Direction::right;
        if(y)return y<0?Direction::down:Direction::up;
        return Direction::none;
    }
    // Enter at 16000, retain until 12000. Keep the selected axis through
    // diagonal jitter; a perpendicular takeover needs a 4000-unit lead.
    const int ax=std::abs(p.x),ay=std::abs(p.y);
    const int along=held==Direction::left?-p.x:held==Direction::right?p.x:held==Direction::up?p.y:held==Direction::down?-p.y:0;
    const int across=held==Direction::left||held==Direction::right?ay:ax;
    if(along>12000&&across<=along+4000)return held;
    if(std::max(ax,ay)<=16000)return Direction::none;
    if(ax>ay)return p.x<0?Direction::left:Direction::right;
    return p.y<0?Direction::down:Direction::up;
}
}
PadEvents NavigationModel::update(const PadSnapshot& p,std::uint64_t now,bool active) noexcept {
    PadEvents e;
    if(!active||!p.connected){ready_=false;previous_=0;direction_=Direction::none;return e;}
    if(!ready_){ready_=neutral(p);previous_=p.buttons;return e;}
    const auto pressed=p.buttons&~previous_;previous_=p.buttons;
    e.confirm=(pressed&pad_a)!=0;e.back=(pressed&pad_b)!=0;e.start=(pressed&0x10)!=0;
    const auto d=direction(p,direction_);
    if(d!=direction_){direction_=d;next_=now+300;e.move=d;}
    else if(d!=Direction::none&&now>=next_){e.move=d;next_=now+110;}
    // At most one move per sample; a delayed UI tick never catches up in a burst.
    return e;
}
CaptureResult BindingCapture::update(const PadSnapshot& p,std::uint64_t now,unsigned& code,bool escape) noexcept {
    if(escape||!p.connected)return CaptureResult::cancelled;
    if(!armed_){armed_=neutral(p);return CaptureResult::listening;}
    if(p.buttons&pad_menu)return CaptureResult::cancelled;
    if(b_pending_){
        if(!(p.buttons&pad_b))return CaptureResult::cancelled;
        if(now-b_since_>=600){code=pad_b;return CaptureResult::captured;}
        return CaptureResult::listening;
    }
    if(p.buttons&pad_b){b_pending_=true;b_since_=now;return CaptureResult::listening;}
    const unsigned pressed=p.buttons|(p.lt>=153?pad_lt:0)|(p.rt>=153?pad_rt:0);
    for(unsigned candidate:pad_choices)if(pressed&candidate){code=candidate;return CaptureResult::captured;}
    return CaptureResult::listening;
}
int TriggerCycle::update(const PadSnapshot& p,bool playing,bool capture,const PadBindings& b) noexcept {
    if(!p.connected||!playing||capture){left_=right_=false;return 0;}
    if(p.lt<=64)left_=true;if(p.rt<=64)right_=true;
    const bool l=p.lt>=153,r=p.rt>=153,fire_l=l&&left_,fire_r=r&&right_;
    if(l)left_=false;if(r)right_=false;
    if(l&&r)return 0;
    unsigned bound=0;for(auto code:b)bound|=code;
    if(fire_l&&!(bound&pad_lt))return -1;
    if(fire_r&&!(bound&pad_rt))return 1;
    return 0;
}
Input::Input(){
    configure(default_bindings);
    configure_pad(default_pad_bindings);
    for(const auto* name:{L"xinput1_4.dll",L"xinput9_1_0.dll"}){
        auto m=LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!m)continue;
        auto p=GetProcAddress(m,"XInputGetState");
        if(p){module_=m;get_state_=reinterpret_cast<void*>(p);break;}
        FreeLibrary(m);
    }
    // XInput may probe an absent device slowly. Only this polling lane calls it;
    // emulation samples the last published byte and never waits for a driver.
    if(get_state_)try{
        polling_=std::thread([this]{
            auto get=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(get_state_);
            PadModel model;
            while(!stop_.load()){
                bool found=false;XINPUT_STATE state{};
                if(enabled_.load())for(DWORD index=0;index<4;++index){
                    if(get(index,&state)==ERROR_SUCCESS){found=true;break;}
                }
                model.update(enabled_.load(),found,state.Gamepad.wButtons,state.Gamepad.sThumbLX,state.Gamepad.sThumbLY);
                pad_.store(model.buttons());connected_.store(model.connected());
                const auto& g=state.Gamepad;
                raw_.store(model.connected()?(std::uint64_t(g.wButtons&0xf3ff)|0x400ull
                    |(std::uint64_t(std::uint16_t(g.sThumbLX))<<16)|(std::uint64_t(std::uint16_t(g.sThumbLY))<<32)
                    |(std::uint64_t(g.bLeftTrigger)<<48)|(std::uint64_t(g.bRightTrigger)<<56)):0);
                // Connected input at 250 Hz, disconnected probes at 10 Hz.
                const unsigned waits=model.connected()?1:25;
                for(unsigned i=0;i<waits&&!stop_.load();++i)Sleep(4);
            }
        });
    }catch(...){FreeLibrary(static_cast<HMODULE>(module_));module_=get_state_=nullptr;}
}
Input::~Input(){stop_.store(true);if(polling_.joinable())polling_.join();if(module_)FreeLibrary(static_cast<HMODULE>(module_));}
void Input::configure(const Bindings& b) noexcept {
    const auto& use=valid_bindings(b)?b:default_bindings;
    for(unsigned i=0;i<use.size();++i)keys_[i].store(use[i]);
}
void Input::configure_pad(const PadBindings& b) noexcept {
    const auto& use=valid_pad_bindings(b)?b:default_pad_bindings;
    for(unsigned i=0;i<use.size();++i)bindings_[i].store(use[i]);
}
PadSnapshot Input::snapshot() const noexcept {
    const auto raw=enabled_.load()?raw_.load():0;
    return {(raw&0x400)!=0,unsigned(raw&0xf3ff),std::int16_t(raw>>16),std::int16_t(raw>>32),unsigned((raw>>48)&255),unsigned(raw>>56)};
}
std::uint8_t Input::sample(void* window) const noexcept {
    if(GetForegroundWindow()!=window)return 0;
    std::uint8_t out=0;
    for(unsigned i=0;i<keys_.size();++i)
        if(GetAsyncKeyState(int(keys_[i].load()))&0x8000)out|=std::uint8_t(1u<<i);
    PadBindings bindings;for(unsigned i=0;i<bindings.size();++i)bindings[i]=bindings_[i].load();
    return combine_buttons(out,mapped_pad_buttons(snapshot(),bindings));
}
}
