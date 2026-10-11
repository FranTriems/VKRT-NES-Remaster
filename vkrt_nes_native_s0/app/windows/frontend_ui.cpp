#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <frontend_ui.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <ostream>
#include <stdexcept>

namespace vkrt::nes::frontend {
Mode visual_mode(unsigned n) noexcept {return visual_modes[n==3?1:0];}
const wchar_t* visual_name(unsigned n) noexcept {return visual_names[n==3?1:0];}
int shortcut_mode(unsigned k) noexcept {return k==112?0:k==115?3:-1;}
Rect viewport(int w,int h,bool integer) noexcept {
    if(w<=0||h<=0)return {};
    // Integer multiples of the frozen 512x480 presentation image.
    int dw,dh;
    if(integer&&w>=512&&h>=480){int scale=std::min(w/512,h/480);dw=512*scale;dh=480*scale;}
    else if(std::int64_t(w)*480<=std::int64_t(h)*512){dw=w;dh=std::max(1,int(std::int64_t(w)*480/512));}
    else {dh=h;dw=std::max(1,int(std::int64_t(h)*512/480));}
    return {(w-dw)/2,(h-dh)/2,dw,dh};
}
Rect FullscreenState::transition(bool desired,Rect current,Rect monitor) noexcept {
    if(desired==active)return current;
    if(desired){restore=current;restore_valid=true;active=true;return monitor;}
    active=false;return restore_valid?restore:current;
}
Rect game_viewport(int w,int h,bool integer,double scale) noexcept {
    int bar=std::min(std::max(0,h),std::max(0,int(std::lround(56*scale))));
    auto r=viewport(w,h-bar,integer);r.y+=bar;return r;
}
void compose_playfield(const Frame& frame,const Image& image,std::span<std::uint8_t> bgra){
    if(image.width!=512||image.height!=480||image.rgb.size()!=512u*480u*3u||
       bgra.size()!=512u*480u*4u||frame.samples.size()!=std::size_t(pixels))
        throw std::invalid_argument("Invalid frontend playfield extent");
    for(std::size_t i=0;i<512u*480u;++i){
        bgra[i*4]=image.rgb[i*3+2];bgra[i*4+1]=image.rgb[i*3+1];
        bgra[i*4+2]=image.rgb[i*3];bgra[i*4+3]=255;
    }
    for(int y=0;y<height;++y){
        const auto* row=frame.samples.data()+y*width;
        // The PPU tap leaves Source empty when background fetching is masked.
        // A fetched neighbor distinguishes left clipping from full forced blank.
        // Use per-scanline evidence, not the end-of-frame register or RGB color.
        const auto nt=row[8].background.nametable;
        if(nt<0x2000||nt>=0x3000)continue;
        bool clipped=true;
        for(int x=0;x<8;++x)clipped&=!row[x].bg_opaque&&row[x].background==Source{};
        if(!clipped)continue;
        for(int x=0;x<8;++x){
            if(row[x].layer!=Layer::backdrop||row[x].sprite_opaque)continue;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                const auto i=std::size_t((y*2+dy)*512+x*2+dx)*4;
                bgra[i]=bgra[i+1]=bgra[i+2]=0;
            }
        }
    }
}
void paint_stats(void* target,int w,int h,std::wstring_view value){
    auto dc=static_cast<HDC>(target);RECT r{0,0,w,h};
    SetDCBrushColor(dc,RGB(25,31,40));FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(242,245,247));
    r.left+=8;DrawTextW(dc,value.data(),int(value.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
}
bool should_pause(const State& s,const Config& c) noexcept {
    return !s.session||s.page!=Page::game||s.loading||!s.error.empty()||
        ((s.minimized||!s.focused)&&c.pause_on_focus_loss);
}
void escape(State& s) noexcept {
    if(s.pad_binding>=0){s.pad_binding=-1;return;}
    if(s.binding>=0){s.binding=-1;return;}
    if(!s.error.empty()){s.error.clear();return;}
    if(s.loading)return;
    if(s.page==Page::game)s.page=Page::pause;
    else if(s.page==Page::pause)s.page=Page::game;
    else if(s.page!=Page::home)s.page=s.session?s.settings_parent:Page::home;
    s.selection=0;
}
Effect route(State& s,Config& c,Command command){
    Effect e;
    const auto normalized=profile_mode(s.profile,c.visual_mode);
    if(c.visual_mode!=normalized){c.visual_mode=normalized;e.save=true;}
    if(command==Command::dismiss){s.error.clear();return e;}
    if(command==Command::home&&!s.loading&&(s.binding>=0||s.pad_binding>=0||!s.error.empty())){
        escape(s);return e;
    }
    if(s.loading||!s.error.empty())return e;
    if(s.binding>=0||s.pad_binding>=0)return e;
    auto value=unsigned(command);
    if(value>=unsigned(Command::mode0)&&value<unsigned(Command::mode0)+4){
        const auto slot=value-unsigned(Command::mode0);
        if(profile_mode_allowed(s.profile,slot)){c.visual_mode=slot;e.save=true;}return e;
    }
    if(value>=unsigned(Command::recent0)&&value<unsigned(Command::recent0)+max_recents){
        auto i=value-unsigned(Command::recent0);if(i<c.recents.size())e.recent=int(i);return e;
    }
    if(value>=unsigned(Command::bind0)&&value<unsigned(Command::bind0)+8){s.binding=int(value-unsigned(Command::bind0));return e;}
    if(value>=unsigned(Command::pad_bind0)&&value<unsigned(Command::pad_bind0)+4){s.pad_binding=int(value-unsigned(Command::pad_bind0));return e;}
    const auto old=s.page;
    if(command==Command::video||command==Command::settings||command==Command::audio||
       command==Command::controls||command==Command::advanced){
        if(old==Page::home)s.settings_parent=Page::home;
        else if(old==Page::game||old==Page::pause)s.settings_parent=Page::pause;
    }
    switch(command){
    case Command::resume:if(s.session)s.page=Page::game;break;
    case Command::home:s.page=Page::home;s.visual_notice.clear();break;
    case Command::open:e.open=true;break;
    case Command::reset:if(s.session&&!s.replay){e.reset=true;s.page=Page::game;}break;
    case Command::video:case Command::settings:s.page=Page::video;break;
    case Command::audio:s.page=Page::audio;break;
    case Command::controls:s.page=Page::controls;break;
    case Command::advanced:s.page=Page::advanced;break;
    case Command::quit:e.quit=true;break;
    case Command::back:s.page=!s.session||old==Page::home?Page::home:
        old==Page::game||old==Page::pause?Page::pause:s.settings_parent;break;
    case Command::integer_scale:c.integer_scaling=!c.integer_scaling;e.save=true;break;
    case Command::fullscreen:c.fullscreen=!c.fullscreen;e.fullscreen=e.save=true;break;
    case Command::stats:c.stats=!c.stats;e.save=true;break;
    case Command::focus_pause:c.pause_on_focus_loss=!c.pause_on_focus_loss;e.save=true;break;
    case Command::defaults:c.keys=default_bindings;c.pad=default_pad_bindings;s.binding=s.pad_binding=-1;e.save=true;break;
    case Command::save_state:e.save_state=s.session&&!s.replay&&!s.tape_locked;break;
    case Command::load_state:e.load_state=s.session&&!s.replay&&!s.tape_locked;break;
    case Command::slot_next:c.state_slot=c.state_slot%4+1;e.save=true;break;
    case Command::volume_down:c.volume=c.volume>5?std::min(c.volume,100u)-5:0;e.save=true;break;
    case Command::volume_up:c.volume=std::min(c.volume,95u)+5;e.save=true;break;
    case Command::mute:c.mute=!c.mute;e.save=true;break;
    case Command::gamepad:c.gamepad_enabled=!c.gamepad_enabled;e.save=true;break;
    default:break;
    }
    if(old!=s.page){s.selection=0;s.binding=s.pad_binding=-1;}
    return e;
}
Rect Layout::rect(int x,int y,int w,int h) const noexcept {
    int left=ox+int(std::lround(x*scale)),top=oy+int(std::lround(y*scale));
    return {left,top,int(std::lround((x+w)*scale))+ox-left,int(std::lround((y+h)*scale))+oy-top};
}
Layout layout(const State& s,const Config& c,int width,int height,unsigned dpi){
    Layout l;l.scale=std::max(0.,std::min({double(std::max(0,width))/960,double(std::max(0,height))/700,double(std::clamp(dpi,48u,768u))/96}));
    l.ox=(width-int(std::lround(960*l.scale)))/2;l.oy=(height-int(std::lround(700*l.scale)))/2;
    auto add=[&](int x,int y,int w,int h,Command cmd,std::wstring label,bool primary=false,bool selected=false,bool enabled=true){
        auto& i=l.items[l.count++];i={l.rect(x,y,w,h),cmd,std::move(label),selected,primary,enabled};
    };
    if(s.page==Page::game)l.oy=0;
    if(!s.error.empty()){add(330,428,300,48,Command::dismiss,L"Got it",true);return l;}
    if(s.loading)return l;
    if(s.page==Page::game){
        add(706,8,206,40,Command::back,L"Menu   R3 / Esc");
    }else if(s.page==Page::home){
        add(48,280,310,58,Command::open,L"Open ROM",true);
        if(s.session)add(48,352,310,46,Command::resume,L"Resume");
        add(48,516,148,46,Command::settings,L"Settings");add(210,516,148,46,Command::quit,L"Quit");
        for(unsigned i=0;i<std::min(c.recents.size(),max_recents);++i)
            add(426,176+int(i)*43,486,38,Command(unsigned(Command::recent0)+i),std::filesystem::path(c.recents[i]).filename().wstring());
    }else if(s.page==Page::pause){
        constexpr Command commands[]{Command::resume,Command::home,Command::open,Command::reset,Command::video,Command::settings,Command::quit};
        constexpr const wchar_t* names[]{L"Resume",L"Home",L"Open ROM",L"Reset",L"Video / visual mode",L"Settings",L"Quit"};
        for(unsigned i=0;i<7;++i)add(48,222+int(i)*50,304,44,commands[i],names[i],i==0,false,commands[i]!=Command::reset||!s.replay);
        add(426,262,250,46,Command::slot_next,L"State slot   "+std::to_wstring(c.state_slot)+L" / 4");
        add(426,328,250,46,Command::save_state,L"Save state",false,false,!s.replay&&!s.tape_locked);
        add(426,390,250,46,Command::load_state,L"Load state",false,false,!s.replay&&!s.tape_locked);
    }else if(s.page!=Page::game){
        constexpr Command commands[]{Command::video,Command::audio,Command::controls,Command::advanced};
        constexpr const wchar_t* names[]{L"Video",L"Audio",L"Controls",L"Advanced"};
        constexpr Page pages[]{Page::video,Page::audio,Page::controls,Page::advanced};
        for(unsigned i=0;i<4;++i)add(48+int(i)*220,128,204,44,commands[i],names[i],false,s.page==pages[i]);
        add(48,622,160,44,Command::back,L"Back");
        if(s.page==Page::video){
            for(unsigned i=0;i<product_mode_slots.size();++i){
                const auto slot=product_mode_slots[i];
                add(48+int(i)*220,242,204,62,Command(unsigned(Command::mode0)+slot),visual_names[i],false,profile_mode(s.profile,c.visual_mode)==slot);
            }
            add(48,354,424,54,Command::integer_scale,c.integer_scaling?L"Integer scaling   On":L"Integer scaling   Off");
            add(488,354,424,54,Command::fullscreen,c.fullscreen?L"Fullscreen   On":L"Fullscreen   Off");
            add(48,426,424,54,Command::stats,c.stats?L"Stats overlay   On":L"Stats overlay   Off");
        }else if(s.page==Page::audio){
            add(48,354,164,54,Command::volume_down,L"Volume   -");
            add(388,354,164,54,Command::volume_up,L"Volume   +");
            add(598,354,314,54,Command::mute,c.mute?L"Mute   On":L"Mute   Off");
        }else if(s.page==Page::controls){
            constexpr unsigned order[]{4,5,6,7,0,1,3,2};
            for(unsigned i=0;i<8;++i){unsigned b=order[i];
                auto label=std::wstring(button_names[b])+L"    "+(s.binding==int(b)?L"Press a key...":key_name(c.keys[b]));
                add(48+int(i%2)*220,226+int(i/2)*58,208,48,Command(unsigned(Command::bind0)+b),label,false,s.binding==int(b));
            }
            constexpr unsigned pad_order[]{0,1,3,2};
            for(unsigned row=0;row<4;++row){unsigned b=pad_order[row];
                auto label=std::wstring(L"Pad ")+button_names[b]+L"   "+(s.pad_binding==int(b)?L"Listening...":pad_name(c.pad[b]));
                add(500,226+int(row)*58,412,48,Command(unsigned(Command::pad_bind0)+b),label,false,s.pad_binding==int(b),c.gamepad_enabled&&s.gamepad_connected);
            }
            add(672,622,240,44,Command::defaults,L"Restore Defaults");
            add(48,527,424,40,Command::gamepad,c.gamepad_enabled?L"Gamepad   Enabled":L"Gamepad   Disabled");
        }else if(s.page==Page::advanced){
            add(48,238,864,56,Command::focus_pause,c.pause_on_focus_loss?L"Pause on focus loss   On":L"Pause on focus loss   Off");
            add(48,314,864,56,Command::stats,c.stats?L"Diagnostic stats   On":L"Diagnostic stats   Off");
        }
    }
    return l;
}
int hit_test(const Layout& l,int x,int y) noexcept {
    for(unsigned i=0;i<l.count;++i)if(l.items[i].enabled&&l.items[i].rect.contains(x,y))return int(i);
    return -1;
}
void move_selection(State& s,const Layout& l,int direction) noexcept {
    if(!l.count){s.selection=0;return;}
    int next=std::clamp(s.selection,0,int(l.count)-1);
    for(unsigned i=0;i<l.count;++i){next=(next+(direction<0?-1:1)+int(l.count))%int(l.count);if(l.items[next].enabled){s.selection=next;return;}}
}
void navigate(State& s,const Layout& l,Direction d) noexcept {
    if(d==Direction::none||!l.count)return;
    if(s.selection<0||unsigned(s.selection)>=l.count){s.selection=0;return;}
    const auto r=l.items[unsigned(s.selection)].rect;
    int best=-1;std::int64_t score=INT64_MAX;
    for(unsigned i=0;i<l.count;++i){if(int(i)==s.selection||!l.items[i].enabled)continue;
        const auto q=l.items[i].rect;
        const auto dx=std::int64_t(q.x)*2+q.w-(std::int64_t(r.x)*2+r.w),dy=std::int64_t(q.y)*2+q.h-(std::int64_t(r.y)*2+r.h);
        const auto forward=d==Direction::left?-dx:d==Direction::right?dx:d==Direction::up?-dy:dy;
        const auto cross=d==Direction::left||d==Direction::right?dy:dx;
        if(forward<=0)continue;
        const auto cost=forward*forward+4*cross*cross;
        if(cost<score){score=cost;best=int(i);}
    }
    if(best>=0)s.selection=best;
    else move_selection(s,l,d==Direction::left||d==Direction::up?-1:1);
}
void ControllerUiModel::cancel(State& s) noexcept {
    s.pad_binding=-1;listening_=-1;capture_.begin();
    // Do not rearm the system button on page/capture changes while it is held.
    navigation_.update({},0,false);triggers_.update({},false,true,default_pad_bindings);
}
ControllerResult ControllerUiModel::update(State& s,Config& c,const Layout& l,PadSnapshot p,std::uint64_t now){
    ControllerResult out;
    if(!c.gamepad_enabled)p={};
    if(s.gamepad_connected!=p.connected){s.gamepad_connected=p.connected;out.changed=true;}
    const bool active=s.focused&&!s.minimized&&!s.loading;
    if(menu_.update(p,active)){
        // Same safe unwind as Escape: cancel capture/dismiss error first, then
        // settings -> pause/home, pause -> game, gameplay -> pause.
        escape(s);cancel(s);out.changed=true;return out;
    }
    if(s.pad_binding>=0){
        navigation_.update(p,now,false);triggers_.update(p,false,true,c.pad);
        if(listening_!=s.pad_binding){capture_.begin();listening_=s.pad_binding;}
        unsigned code=0;const auto result=capture_.update(p,now,code,!active);
        if(result==CaptureResult::cancelled){cancel(s);s.notice=L"Controller binding cancelled.";out.changed=true;}
        if(result==CaptureResult::captured){
            if(bind_pad(c.pad,unsigned(s.pad_binding),code)){cancel(s);s.notice=L"Controller binding saved.";out.save=true;}
            else {capture_.begin();s.notice=L"Already assigned. Release, then choose another button.";}
            out.changed=true;
        }
        return out;
    }
    if(listening_>=0)cancel(s);
    const bool playing=active&&s.session&&s.page==Page::game&&s.error.empty()&&s.binding<0;
    const int step=triggers_.update(p,playing,s.binding>=0,c.pad);
    if(step){out.command=Command(unsigned(Command::mode0)+cycle_profile_mode(s.profile,c.visual_mode,step));return out;}
    const auto e=navigation_.update(p,now,active&&s.page!=Page::game);
    if(e.back){escape(s);out.changed=true;return out;}
    if(s.binding>=0)return out;
    if(e.start&&s.page==Page::pause&&s.error.empty()){out.command=Command::resume;return out;}
    if(e.move!=Direction::none){navigate(s,l,e.move);out.changed=true;}
    if(e.confirm&&s.selection>=0&&unsigned(s.selection)<l.count&&l.items[unsigned(s.selection)].enabled)
        out.command=l.items[unsigned(s.selection)].command;
    return out;
}
void controller_diagnostics(std::ostream& out){
    auto require=[](bool ok){if(!ok)throw std::runtime_error("controller diagnostic failed");};
    PadSnapshot p{true};NavigationModel nav;nav.update(p,0,true);
    p.buttons=2;require(nav.update(p,1,true).move==Direction::down);
    require(nav.update(p,300,true).move==Direction::none);
    require(nav.update(p,301,true).move==Direction::down);
    require(nav.update(p,410,true).move==Direction::none);
    require(nav.update(p,411,true).move==Direction::down);
    p.buttons=0;nav.update(p,412,true);p.x=12000;require(nav.update(p,413,true).move==Direction::none);
    p.x=20000;require(nav.update(p,414,true).move==Direction::right);
    p={true,pad_a};require(nav.update(p,415,true).confirm);
    p.buttons=pad_b;require(nav.update(p,416,true).back);
    require(!nav.update({},417,true).confirm);
    BindingCapture capture;unsigned code=0;capture.begin();p={true,0,0,0,255,0};
    require(capture.update(p,0,code)==CaptureResult::listening);
    p.lt=0;capture.update(p,1,code);p.lt=153;
    require(capture.update(p,2,code)==CaptureResult::captured&&code==pad_lt);
    Config c;require(bind_pad(c.pad,0,code));
    require(!bind_pad(c.pad,1,code));require(parse_config(serialize_config(c))==c);
    TriggerCycle cycle;p={true};require(cycle.update(p,true,false,c.pad)==0);
    p.lt=255;require(cycle.update(p,true,false,c.pad)==0);
    p.lt=0;p.rt=255;require(cycle.update(p,true,false,c.pad)==1);
    require(cycle.update(p,true,false,c.pad)==0);
    p.rt=64;cycle.update(p,true,false,c.pad);p.lt=p.rt=255;
    require(cycle.update(p,true,false,c.pad)==0);
    p={true};cycle.update(p,true,false,default_pad_bindings);p.lt=153;
    require(cycle.update(p,true,false,default_pad_bindings)==-1);
    p={true};cycle.update(p,true,false,c.pad);p.rt=255;
    require(cycle.update(p,true,true,c.pad)==0);
    out<<"CONTROLLER_NAVIGATION=PASS SINGLE_STEP DEADZONE_16000_12000 REPEAT_300_110_MS CONFIRM BACK DISCONNECT\n"
       <<"CONTROLLER_REBINDING=PASS NEUTRAL_FIRST DUPLICATES_REJECTED CONFIG_ROUNDTRIP\n"
       <<"TRIGGER_CYCLE=PASS LT_RT_TOGGLE HYSTERESIS_153_64 HOLD_ONCE BOTH_SUPPRESSED CAPTURE_SUPPRESSED GAMEPLAY_PRIORITY\n";
}
struct Canvas::Impl {
    HDC dc{CreateCompatibleDC(nullptr)};HBITMAP bitmap{};HGDIOBJ original{};
    std::uint8_t* data{};int w{},h{},font_size{};
    std::array<HFONT,4> fonts{};
    ~Impl(){
        if(dc)SelectObject(dc,GetStockObject(SYSTEM_FONT));
        if(original)SelectObject(dc,original);
        if(bitmap)DeleteObject(bitmap);
        for(auto f:fonts)if(f)DeleteObject(f);
        if(dc)DeleteDC(dc);
    }
    void resize(int width,int height,double scale){
        if(width<1||height<1||width>8192||height>8192||std::int64_t(width)*height>33554432)throw std::runtime_error("UI surface size outside bound");
        if(!dc)throw std::runtime_error("UI drawing context unavailable");
        if(w!=width||h!=height){
            if(original)SelectObject(dc,original);
            if(bitmap){DeleteObject(bitmap);bitmap=nullptr;}
            BITMAPINFO bmi{};bmi.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};
            bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,reinterpret_cast<void**>(&data),nullptr,0);
            if(!bitmap)throw std::runtime_error("UI surface allocation failed");
            auto old=SelectObject(dc,bitmap);if(!original)original=old;
            w=width;h=height;
        }
        int size=std::max(1,int(std::lround(16*scale)));
        if(size!=font_size){
            SelectObject(dc,GetStockObject(SYSTEM_FONT));
            for(auto& f:fonts){if(f)DeleteObject(f);f=nullptr;}
            constexpr double ratios[]{1.,.82,2.5,1.375};
            for(unsigned i=0;i<fonts.size();++i)fonts[i]=CreateFontW(-std::max(1,int(std::lround(size*ratios[i]))),0,0,0,i==2?FW_SEMIBOLD:FW_NORMAL,
                FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            font_size=size;
        }
        SetBkMode(dc,TRANSPARENT);
    }
    void fill(Rect r,COLORREF color){
        RECT box{r.x,r.y,r.x+r.w,r.y+r.h};SetDCBrushColor(dc,color);FillRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    void text(Rect r,std::wstring_view str,COLORREF color,unsigned font=0,unsigned flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){
        SelectObject(dc,fonts[font]);SetTextColor(dc,color);RECT box{r.x,r.y,r.x+r.w,r.y+r.h};
        DrawTextW(dc,str.data(),int(str.size()),&box,flags|DT_NOPREFIX);
    }
};
Canvas::Canvas():impl_(std::make_unique<Impl>()){}
Canvas::~Canvas()=default;
void Canvas::draw(const State& s,const Config& c,const Layout& l,int w,int h,std::span<const std::uint8_t> game){
    auto& p=*impl_;p.resize(w,h,l.scale);
    constexpr COLORREF shell=RGB(16,20,27),panel=RGB(25,31,40),card=RGB(36,44,55),ink=RGB(247,247,247),muted=RGB(185,190,198),accent=RGB(218,38,52);
    p.fill({0,0,w,h},shell);
    if(s.session&&s.page!=Page::home&&game.size()==512u*480u*4u){
        const auto v=game_viewport(w,h,c.integer_scaling,l.scale);
        BITMAPINFO bmi{};bmi.bmiHeader={sizeof(BITMAPINFOHEADER),512,-480,1,32,BI_RGB,0,0,0,0,0};
        SetStretchBltMode(p.dc,COLORONCOLOR);
        StretchDIBits(p.dc,v.x,v.y,v.w,v.h,0,0,512,480,game.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
        GdiFlush();
        if(s.page!=Page::game||s.loading||!s.error.empty())for(std::size_t i=0,n=std::size_t(w)*h;i<n;++i)for(unsigned k=0;k<3;++k)p.data[i*4+k]=std::uint8_t(p.data[i*4+k]*3/4);
    }
    auto remaster_word=[&](auto r){
        static constexpr unsigned char g[8][7]={
            {30,17,17,30,20,18,17},
            {31,16,16,30,16,16,31},
            {17,27,21,21,17,17,17},
            {14,17,17,31,17,17,17},
            {15,16,16,14,1,1,30},
            {31,4,4,4,4,4,4},
            {31,16,16,30,16,16,31},
            {30,17,17,30,20,18,17}
        };
        static constexpr COLORREF colors[8]={
            RGB(230,45,55),RGB(242,113,39),RGB(246,198,55),RGB(136,207,67),
            RGB(57,190,191),RGB(64,139,225),RGB(135,88,214),RGB(222,75,177)
        };
        const int px=std::max(1,std::min(r.h/9,r.w/55));
        const int glyph_w=5*px,gap=2*px,total=8*glyph_w+7*gap;
        const int ox=r.x+std::max(0,(r.w-total)/2);
        const int oy=r.y+std::max(0,(r.h-7*px)/2);
        for(int gi=0;gi<8;++gi){
            const int gx=ox+gi*(glyph_w+gap);
            for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx){
                if(!(g[gi][yy]&(1u<<(4-xx))))continue;
                auto shadow=r;shadow.x=gx+xx*px+std::max(1,px/3);shadow.y=oy+yy*px+std::max(1,px/3);shadow.w=px;shadow.h=px;
                auto cell=shadow;cell.x=gx+xx*px;cell.y=oy+yy*px;
                p.fill(shadow,RGB(7,9,13));p.fill(cell,colors[gi]);
            }
        }
    };
    struct LR{unsigned char x,y,n,c;};
    static constexpr COLORREF logo_colors[8]={RGB(247,247,247), RGB(218,38,52), RGB(64,139,225), RGB(109,204,246), RGB(246,198,55), RGB(242,113,39), RGB(190,198,210), RGB(222,75,177)};
    static constexpr LR logo_runs[]={
            LR{2,3,7,0},
            LR{9,3,1,6},
            LR{24,3,1,3},
            LR{25,3,7,0},
            LR{35,3,1,2},
            LR{36,3,7,0},
            LR{43,3,1,1},
            LR{54,3,1,3},
            LR{55,3,7,0},
            LR{65,3,1,2},
            LR{66,3,22,0},
            LR{88,3,1,1},
            LR{95,3,1,3},
            LR{96,3,29,0},
            LR{125,3,1,6},
            LR{2,4,7,0},
            LR{9,4,1,6},
            LR{24,4,1,3},
            LR{25,4,7,0},
            LR{35,4,1,2},
            LR{36,4,7,0},
            LR{43,4,1,1},
            LR{54,4,1,3},
            LR{55,4,7,0},
            LR{65,4,1,2},
            LR{66,4,22,0},
            LR{88,4,1,1},
            LR{95,4,1,3},
            LR{96,4,29,0},
            LR{125,4,1,6},
            LR{2,5,7,0},
            LR{9,5,1,6},
            LR{24,5,1,3},
            LR{25,5,7,0},
            LR{35,5,1,2},
            LR{36,5,7,0},
            LR{43,5,1,1},
            LR{54,5,1,3},
            LR{55,5,7,0},
            LR{65,5,1,2},
            LR{66,5,22,0},
            LR{88,5,1,1},
            LR{95,5,1,3},
            LR{96,5,29,0},
            LR{125,5,1,6},
            LR{2,6,10,0},
            LR{12,6,1,1},
            LR{24,6,1,3},
            LR{25,6,9,0},
            LR{34,6,1,6},
            LR{35,6,1,2},
            LR{36,6,10,0},
            LR{54,6,1,3},
            LR{55,6,9,0},
            LR{64,6,1,6},
            LR{65,6,1,2},
            LR{66,6,25,0},
            LR{95,6,1,3},
            LR{96,6,32,0},
            LR{128,6,1,1},
            LR{2,7,9,0},
            LR{11,7,1,7},
            LR{12,7,1,1},
            LR{24,7,1,3},
            LR{25,7,8,0},
            LR{33,7,2,6},
            LR{35,7,1,2},
            LR{36,7,9,0},
            LR{45,7,1,3},
            LR{54,7,1,3},
            LR{55,7,8,0},
            LR{63,7,2,6},
            LR{65,7,1,2},
            LR{66,7,23,0},
            LR{89,7,2,6},
            LR{95,7,1,3},
            LR{96,7,31,0},
            LR{127,7,1,7},
            LR{128,7,1,1},
            LR{2,8,7,0},
            LR{9,8,1,6},
            LR{10,8,1,2},
            LR{11,8,1,7},
            LR{12,8,1,1},
            LR{24,8,1,3},
            LR{25,8,7,0},
            LR{33,8,2,6},
            LR{35,8,1,2},
            LR{36,8,7,0},
            LR{43,8,1,1},
            LR{44,8,1,6},
            LR{45,8,1,3},
            LR{54,8,1,3},
            LR{55,8,7,0},
            LR{63,8,2,6},
            LR{65,8,1,2},
            LR{66,8,22,0},
            LR{88,8,1,1},
            LR{89,8,2,6},
            LR{95,8,1,3},
            LR{96,8,29,0},
            LR{125,8,1,6},
            LR{126,8,1,2},
            LR{127,8,1,7},
            LR{128,8,1,1},
            LR{2,9,7,0},
            LR{9,9,1,6},
            LR{10,9,1,2},
            LR{11,9,1,7},
            LR{12,9,1,1},
            LR{24,9,1,3},
            LR{25,9,7,0},
            LR{33,9,2,6},
            LR{35,9,1,2},
            LR{36,9,7,0},
            LR{43,9,1,1},
            LR{44,9,1,6},
            LR{45,9,1,3},
            LR{54,9,1,3},
            LR{55,9,7,0},
            LR{63,9,2,6},
            LR{65,9,1,2},
            LR{66,9,22,0},
            LR{88,9,1,1},
            LR{89,9,2,6},
            LR{95,9,1,3},
            LR{96,9,29,0},
            LR{125,9,1,6},
            LR{126,9,1,2},
            LR{127,9,1,7},
            LR{128,9,1,1},
            LR{2,10,7,0},
            LR{9,10,1,6},
            LR{10,10,1,2},
            LR{11,10,1,7},
            LR{12,10,1,1},
            LR{24,10,1,3},
            LR{25,10,7,0},
            LR{33,10,2,6},
            LR{35,10,1,2},
            LR{36,10,7,0},
            LR{43,10,1,1},
            LR{44,10,1,6},
            LR{45,10,1,3},
            LR{54,10,1,3},
            LR{55,10,7,0},
            LR{63,10,2,6},
            LR{65,10,1,2},
            LR{66,10,22,0},
            LR{88,10,1,1},
            LR{89,10,2,6},
            LR{95,10,1,3},
            LR{96,10,29,0},
            LR{125,10,1,6},
            LR{126,10,1,2},
            LR{127,10,1,7},
            LR{128,10,1,1},
            LR{2,13,7,0},
            LR{9,13,1,6},
            LR{10,13,1,2},
            LR{11,13,1,7},
            LR{12,13,1,1},
            LR{24,13,1,3},
            LR{25,13,7,0},
            LR{33,13,2,6},
            LR{35,13,1,2},
            LR{36,13,7,0},
            LR{43,13,1,1},
            LR{44,13,1,6},
            LR{45,13,1,3},
            LR{50,13,1,2},
            LR{51,13,7,0},
            LR{58,13,1,1},
            LR{63,13,2,6},
            LR{65,13,1,2},
            LR{66,13,7,0},
            LR{73,13,1,1},
            LR{84,13,1,3},
            LR{85,13,7,0},
            LR{96,13,1,2},
            LR{97,13,1,7},
            LR{98,13,1,1},
            LR{106,13,1,2},
            LR{107,13,7,0},
            LR{114,13,1,5},
            LR{126,13,1,2},
            LR{127,13,1,7},
            LR{128,13,1,1},
            LR{2,14,7,0},
            LR{9,14,1,6},
            LR{10,14,1,2},
            LR{11,14,1,7},
            LR{12,14,1,1},
            LR{24,14,1,3},
            LR{25,14,7,0},
            LR{33,14,2,6},
            LR{35,14,1,2},
            LR{36,14,7,0},
            LR{43,14,1,1},
            LR{44,14,1,6},
            LR{45,14,1,3},
            LR{50,14,1,2},
            LR{51,14,7,0},
            LR{58,14,1,1},
            LR{63,14,2,6},
            LR{65,14,1,2},
            LR{66,14,7,0},
            LR{73,14,1,1},
            LR{84,14,1,3},
            LR{85,14,7,0},
            LR{96,14,1,2},
            LR{97,14,1,7},
            LR{98,14,1,1},
            LR{106,14,1,2},
            LR{107,14,7,0},
            LR{114,14,1,5},
            LR{126,14,1,2},
            LR{127,14,1,7},
            LR{128,14,1,1},
            LR{2,15,7,0},
            LR{9,15,1,6},
            LR{10,15,1,2},
            LR{11,15,1,7},
            LR{12,15,1,1},
            LR{24,15,1,3},
            LR{25,15,7,0},
            LR{33,15,2,6},
            LR{35,15,1,2},
            LR{36,15,7,0},
            LR{43,15,1,1},
            LR{44,15,1,6},
            LR{45,15,1,3},
            LR{50,15,1,2},
            LR{51,15,7,0},
            LR{58,15,1,1},
            LR{63,15,2,6},
            LR{65,15,1,2},
            LR{66,15,7,0},
            LR{73,15,1,1},
            LR{84,15,1,3},
            LR{85,15,7,0},
            LR{96,15,1,2},
            LR{97,15,1,7},
            LR{98,15,1,1},
            LR{106,15,1,2},
            LR{107,15,7,0},
            LR{114,15,1,5},
            LR{126,15,1,2},
            LR{127,15,1,7},
            LR{128,15,1,1},
            LR{2,16,7,0},
            LR{9,16,1,6},
            LR{10,16,1,2},
            LR{11,16,1,7},
            LR{12,16,1,1},
            LR{24,16,1,3},
            LR{25,16,7,0},
            LR{33,16,2,6},
            LR{35,16,1,2},
            LR{36,16,7,0},
            LR{43,16,1,1},
            LR{44,16,1,6},
            LR{45,16,1,3},
            LR{50,16,1,2},
            LR{51,16,7,0},
            LR{58,16,1,1},
            LR{59,16,1,6},
            LR{60,16,3,0},
            LR{63,16,2,6},
            LR{65,16,1,2},
            LR{66,16,7,0},
            LR{73,16,1,1},
            LR{74,16,1,6},
            LR{75,16,19,0},
            LR{94,16,1,4},
            LR{96,16,1,2},
            LR{97,16,1,7},
            LR{98,16,16,0},
            LR{114,16,1,5},
            LR{115,16,1,3},
            LR{116,16,11,0},
            LR{127,16,1,7},
            LR{128,16,1,1},
            LR{2,17,7,0},
            LR{9,17,1,6},
            LR{10,17,1,2},
            LR{11,17,1,7},
            LR{12,17,1,1},
            LR{24,17,1,3},
            LR{25,17,7,0},
            LR{33,17,2,6},
            LR{35,17,1,2},
            LR{36,17,7,0},
            LR{43,17,1,1},
            LR{44,17,1,6},
            LR{45,17,1,3},
            LR{50,17,1,2},
            LR{51,17,7,0},
            LR{58,17,1,1},
            LR{59,17,1,6},
            LR{60,17,1,3},
            LR{61,17,3,0},
            LR{64,17,1,6},
            LR{65,17,1,2},
            LR{66,17,7,0},
            LR{73,17,1,1},
            LR{74,17,1,6},
            LR{75,17,1,3},
            LR{76,17,17,0},
            LR{93,17,1,5},
            LR{94,17,1,4},
            LR{96,17,1,2},
            LR{97,17,17,0},
            LR{114,17,1,5},
            LR{115,17,2,3},
            LR{117,17,11,0},
            LR{128,17,1,1},
            LR{2,18,7,0},
            LR{9,18,1,6},
            LR{10,18,1,2},
            LR{11,18,1,7},
            LR{12,18,1,1},
            LR{24,18,1,3},
            LR{25,18,7,0},
            LR{33,18,2,6},
            LR{35,18,1,2},
            LR{36,18,7,0},
            LR{43,18,1,1},
            LR{44,18,1,6},
            LR{45,18,1,3},
            LR{50,18,1,2},
            LR{51,18,7,0},
            LR{58,18,1,1},
            LR{59,18,1,6},
            LR{60,18,1,3},
            LR{65,18,1,2},
            LR{66,18,7,0},
            LR{73,18,1,1},
            LR{74,18,1,6},
            LR{75,18,1,3},
            LR{84,18,1,3},
            LR{85,18,7,0},
            LR{93,18,1,5},
            LR{94,18,1,4},
            LR{106,18,1,2},
            LR{107,18,7,0},
            LR{114,18,1,5},
            LR{115,18,2,3},
            LR{2,19,7,0},
            LR{9,19,1,6},
            LR{10,19,1,2},
            LR{11,19,1,7},
            LR{12,19,1,1},
            LR{24,19,1,3},
            LR{25,19,7,0},
            LR{33,19,2,6},
            LR{35,19,1,2},
            LR{36,19,7,0},
            LR{43,19,1,1},
            LR{44,19,1,6},
            LR{45,19,1,3},
            LR{50,19,1,2},
            LR{51,19,7,0},
            LR{58,19,1,1},
            LR{59,19,1,6},
            LR{60,19,1,3},
            LR{65,19,1,2},
            LR{66,19,7,0},
            LR{73,19,1,1},
            LR{74,19,1,6},
            LR{75,19,1,3},
            LR{84,19,1,3},
            LR{85,19,7,0},
            LR{93,19,1,5},
            LR{94,19,1,4},
            LR{106,19,1,2},
            LR{107,19,7,0},
            LR{114,19,1,5},
            LR{115,19,2,3},
            LR{2,20,7,0},
            LR{9,20,1,6},
            LR{10,20,1,2},
            LR{11,20,1,7},
            LR{12,20,1,1},
            LR{24,20,1,3},
            LR{25,20,7,0},
            LR{33,20,2,6},
            LR{35,20,1,2},
            LR{36,20,7,0},
            LR{43,20,1,1},
            LR{44,20,1,6},
            LR{45,20,1,3},
            LR{50,20,1,2},
            LR{51,20,7,0},
            LR{58,20,1,1},
            LR{59,20,1,6},
            LR{60,20,1,3},
            LR{65,20,1,2},
            LR{66,20,7,0},
            LR{73,20,1,1},
            LR{74,20,1,6},
            LR{75,20,1,3},
            LR{84,20,1,3},
            LR{85,20,7,0},
            LR{93,20,1,5},
            LR{94,20,1,4},
            LR{106,20,1,2},
            LR{107,20,7,0},
            LR{114,20,1,5},
            LR{115,20,2,3},
            LR{2,22,7,0},
            LR{9,22,1,6},
            LR{10,22,1,2},
            LR{11,22,1,7},
            LR{12,22,1,1},
            LR{24,22,1,3},
            LR{25,22,7,0},
            LR{33,22,2,6},
            LR{35,22,1,2},
            LR{36,22,18,0},
            LR{54,22,1,4},
            LR{59,22,1,6},
            LR{60,22,1,3},
            LR{65,22,1,2},
            LR{66,22,22,0},
            LR{88,22,1,1},
            LR{93,22,1,5},
            LR{94,22,1,4},
            LR{106,22,1,2},
            LR{107,22,7,0},
            LR{114,22,1,5},
            LR{115,22,2,3},
            LR{2,23,7,0},
            LR{9,23,1,6},
            LR{10,23,1,2},
            LR{11,23,1,7},
            LR{12,23,1,1},
            LR{24,23,1,3},
            LR{25,23,7,0},
            LR{33,23,2,6},
            LR{35,23,1,2},
            LR{36,23,18,0},
            LR{54,23,1,4},
            LR{59,23,1,6},
            LR{60,23,1,3},
            LR{65,23,1,2},
            LR{66,23,22,0},
            LR{88,23,1,1},
            LR{93,23,1,5},
            LR{94,23,1,4},
            LR{106,23,1,2},
            LR{107,23,7,0},
            LR{114,23,1,5},
            LR{115,23,2,3},
            LR{2,24,7,0},
            LR{9,24,1,6},
            LR{10,24,1,2},
            LR{11,24,1,7},
            LR{12,24,1,1},
            LR{24,24,1,3},
            LR{25,24,7,0},
            LR{33,24,2,6},
            LR{35,24,1,2},
            LR{36,24,18,0},
            LR{54,24,1,4},
            LR{59,24,1,6},
            LR{60,24,1,3},
            LR{65,24,1,2},
            LR{66,24,22,0},
            LR{88,24,1,1},
            LR{93,24,1,5},
            LR{94,24,1,4},
            LR{106,24,1,2},
            LR{107,24,7,0},
            LR{114,24,1,5},
            LR{115,24,2,3},
            LR{2,25,7,0},
            LR{9,25,1,6},
            LR{10,25,1,2},
            LR{11,25,1,7},
            LR{12,25,1,1},
            LR{24,25,1,3},
            LR{25,25,7,0},
            LR{33,25,2,6},
            LR{35,25,1,2},
            LR{36,25,18,0},
            LR{54,25,1,4},
            LR{59,25,1,6},
            LR{60,25,1,3},
            LR{65,25,1,2},
            LR{66,25,22,0},
            LR{88,25,1,1},
            LR{93,25,1,5},
            LR{94,25,1,4},
            LR{106,25,1,2},
            LR{107,25,7,0},
            LR{114,25,1,5},
            LR{115,25,2,3},
            LR{2,26,7,0},
            LR{9,26,1,6},
            LR{10,26,1,2},
            LR{11,26,1,7},
            LR{12,26,1,1},
            LR{24,26,1,3},
            LR{25,26,7,0},
            LR{33,26,2,6},
            LR{35,26,1,2},
            LR{36,26,18,0},
            LR{54,26,1,4},
            LR{55,26,1,2},
            LR{60,26,1,3},
            LR{65,26,1,2},
            LR{66,26,22,0},
            LR{88,26,1,1},
            LR{89,26,1,6},
            LR{94,26,1,4},
            LR{106,26,1,2},
            LR{107,26,7,0},
            LR{114,26,1,5},
            LR{115,26,2,3},
            LR{2,27,7,0},
            LR{9,27,1,6},
            LR{10,27,1,2},
            LR{11,27,1,7},
            LR{12,27,1,1},
            LR{24,27,1,3},
            LR{25,27,7,0},
            LR{33,27,2,6},
            LR{35,27,1,2},
            LR{36,27,18,0},
            LR{54,27,1,4},
            LR{55,27,2,2},
            LR{57,27,1,1},
            LR{65,27,1,2},
            LR{66,27,22,0},
            LR{88,27,1,1},
            LR{89,27,2,6},
            LR{106,27,1,2},
            LR{107,27,7,0},
            LR{114,27,1,5},
            LR{115,27,2,3},
            LR{2,28,7,0},
            LR{9,28,1,6},
            LR{10,28,1,2},
            LR{11,28,1,7},
            LR{12,28,1,1},
            LR{24,28,1,3},
            LR{25,28,7,0},
            LR{33,28,2,6},
            LR{35,28,1,2},
            LR{36,28,18,0},
            LR{54,28,1,4},
            LR{55,28,2,2},
            LR{57,28,1,1},
            LR{65,28,1,2},
            LR{66,28,22,0},
            LR{88,28,1,1},
            LR{89,28,2,6},
            LR{106,28,1,2},
            LR{107,28,7,0},
            LR{114,28,1,5},
            LR{115,28,2,3},
            LR{2,29,7,0},
            LR{9,29,1,6},
            LR{10,29,1,2},
            LR{11,29,1,7},
            LR{12,29,1,1},
            LR{24,29,1,3},
            LR{25,29,7,0},
            LR{33,29,2,6},
            LR{35,29,1,2},
            LR{36,29,18,0},
            LR{54,29,1,4},
            LR{55,29,2,2},
            LR{57,29,1,1},
            LR{65,29,1,2},
            LR{66,29,22,0},
            LR{88,29,1,1},
            LR{89,29,2,6},
            LR{106,29,1,2},
            LR{107,29,7,0},
            LR{114,29,1,5},
            LR{115,29,2,3},
            LR{2,30,7,0},
            LR{9,30,1,6},
            LR{10,30,1,2},
            LR{11,30,1,7},
            LR{12,30,1,1},
            LR{24,30,1,3},
            LR{25,30,7,0},
            LR{33,30,2,6},
            LR{35,30,1,2},
            LR{36,30,18,0},
            LR{54,30,1,4},
            LR{55,30,2,2},
            LR{57,30,1,1},
            LR{65,30,1,2},
            LR{66,30,22,0},
            LR{88,30,1,1},
            LR{89,30,2,6},
            LR{106,30,1,2},
            LR{107,30,7,0},
            LR{114,30,1,5},
            LR{115,30,2,3},
            LR{3,32,2,6},
            LR{5,32,1,2},
            LR{6,32,7,0},
            LR{13,32,1,5},
            LR{20,32,1,2},
            LR{21,32,7,0},
            LR{28,32,1,5},
            LR{33,32,2,6},
            LR{35,32,1,2},
            LR{36,32,7,0},
            LR{43,32,1,1},
            LR{50,32,1,2},
            LR{51,32,7,0},
            LR{58,32,1,1},
            LR{65,32,1,2},
            LR{66,32,7,0},
            LR{73,32,1,1},
            LR{84,32,1,3},
            LR{85,32,7,0},
            LR{106,32,1,2},
            LR{107,32,7,0},
            LR{114,32,1,5},
            LR{115,32,2,3},
            LR{3,33,2,6},
            LR{5,33,1,2},
            LR{6,33,7,0},
            LR{13,33,1,5},
            LR{20,33,1,2},
            LR{21,33,7,0},
            LR{28,33,1,5},
            LR{33,33,2,6},
            LR{35,33,1,2},
            LR{36,33,7,0},
            LR{43,33,1,1},
            LR{50,33,1,2},
            LR{51,33,7,0},
            LR{58,33,1,1},
            LR{65,33,1,2},
            LR{66,33,7,0},
            LR{73,33,1,1},
            LR{84,33,1,3},
            LR{85,33,7,0},
            LR{106,33,1,2},
            LR{107,33,7,0},
            LR{114,33,1,5},
            LR{115,33,2,3},
            LR{3,34,2,6},
            LR{5,34,1,2},
            LR{6,34,7,0},
            LR{13,34,1,5},
            LR{20,34,1,2},
            LR{21,34,7,0},
            LR{28,34,1,5},
            LR{33,34,2,6},
            LR{35,34,1,2},
            LR{36,34,7,0},
            LR{43,34,1,1},
            LR{50,34,1,2},
            LR{51,34,7,0},
            LR{58,34,1,1},
            LR{65,34,1,2},
            LR{66,34,7,0},
            LR{73,34,1,1},
            LR{84,34,1,3},
            LR{85,34,7,0},
            LR{106,34,1,2},
            LR{107,34,7,0},
            LR{114,34,1,5},
            LR{115,34,2,3},
            LR{3,35,2,6},
            LR{5,35,1,2},
            LR{6,35,7,0},
            LR{13,35,1,5},
            LR{20,35,1,2},
            LR{21,35,7,0},
            LR{28,35,1,5},
            LR{33,35,2,6},
            LR{35,35,1,2},
            LR{36,35,7,0},
            LR{43,35,1,1},
            LR{50,35,1,2},
            LR{51,35,7,0},
            LR{58,35,1,1},
            LR{65,35,1,2},
            LR{66,35,7,0},
            LR{73,35,1,1},
            LR{84,35,1,3},
            LR{85,35,7,0},
            LR{106,35,1,2},
            LR{107,35,7,0},
            LR{114,35,1,5},
            LR{115,35,2,3},
            LR{3,36,1,6},
            LR{5,36,1,2},
            LR{6,36,7,0},
            LR{13,36,1,5},
            LR{15,36,1,3},
            LR{20,36,1,2},
            LR{21,36,7,0},
            LR{28,36,1,5},
            LR{29,36,1,3},
            LR{34,36,1,6},
            LR{35,36,1,2},
            LR{36,36,7,0},
            LR{43,36,1,1},
            LR{44,36,1,6},
            LR{50,36,1,2},
            LR{51,36,7,0},
            LR{58,36,1,1},
            LR{60,36,1,3},
            LR{65,36,1,2},
            LR{66,36,7,0},
            LR{73,36,1,1},
            LR{74,36,1,6},
            LR{84,36,1,3},
            LR{85,36,7,0},
            LR{94,36,1,4},
            LR{106,36,1,2},
            LR{107,36,7,0},
            LR{114,36,1,5},
            LR{115,36,2,3},
            LR{5,37,1,2},
            LR{6,37,7,0},
            LR{13,37,1,5},
            LR{14,37,2,3},
            LR{20,37,1,2},
            LR{21,37,7,0},
            LR{28,37,1,5},
            LR{29,37,2,3},
            LR{35,37,1,2},
            LR{36,37,7,0},
            LR{43,37,1,1},
            LR{44,37,1,6},
            LR{45,37,1,3},
            LR{50,37,1,2},
            LR{51,37,7,0},
            LR{58,37,1,1},
            LR{59,37,1,6},
            LR{60,37,1,3},
            LR{65,37,1,2},
            LR{66,37,7,0},
            LR{73,37,1,1},
            LR{74,37,1,6},
            LR{75,37,1,3},
            LR{84,37,1,3},
            LR{85,37,7,0},
            LR{93,37,1,5},
            LR{94,37,1,4},
            LR{106,37,1,2},
            LR{107,37,7,0},
            LR{114,37,1,5},
            LR{115,37,2,3},
            LR{5,38,1,2},
            LR{6,38,7,0},
            LR{13,38,1,5},
            LR{14,38,2,3},
            LR{20,38,1,2},
            LR{21,38,7,0},
            LR{28,38,1,5},
            LR{29,38,2,3},
            LR{35,38,1,2},
            LR{36,38,7,0},
            LR{43,38,1,1},
            LR{44,38,1,6},
            LR{45,38,1,3},
            LR{50,38,1,2},
            LR{51,38,7,0},
            LR{58,38,1,1},
            LR{59,38,1,6},
            LR{60,38,1,3},
            LR{65,38,1,2},
            LR{66,38,7,0},
            LR{73,38,1,1},
            LR{74,38,1,6},
            LR{75,38,1,3},
            LR{84,38,1,3},
            LR{85,38,7,0},
            LR{93,38,1,5},
            LR{94,38,1,4},
            LR{106,38,1,2},
            LR{107,38,7,0},
            LR{114,38,1,5},
            LR{115,38,2,3},
            LR{5,39,1,2},
            LR{6,39,7,0},
            LR{13,39,1,5},
            LR{14,39,2,3},
            LR{20,39,1,2},
            LR{21,39,7,0},
            LR{28,39,1,5},
            LR{29,39,2,3},
            LR{35,39,1,2},
            LR{36,39,7,0},
            LR{43,39,1,1},
            LR{44,39,1,6},
            LR{45,39,1,3},
            LR{50,39,1,2},
            LR{51,39,7,0},
            LR{58,39,1,1},
            LR{59,39,1,6},
            LR{60,39,1,3},
            LR{65,39,1,2},
            LR{66,39,7,0},
            LR{73,39,1,1},
            LR{74,39,1,6},
            LR{75,39,1,3},
            LR{84,39,1,3},
            LR{85,39,7,0},
            LR{93,39,1,5},
            LR{94,39,1,4},
            LR{106,39,1,2},
            LR{107,39,7,0},
            LR{114,39,1,5},
            LR{115,39,2,3},
            LR{5,40,1,2},
            LR{6,40,7,0},
            LR{13,40,1,5},
            LR{14,40,2,3},
            LR{20,40,1,2},
            LR{21,40,7,0},
            LR{28,40,1,5},
            LR{29,40,2,3},
            LR{35,40,1,2},
            LR{36,40,7,0},
            LR{43,40,1,1},
            LR{44,40,1,6},
            LR{45,40,1,3},
            LR{50,40,1,2},
            LR{51,40,7,0},
            LR{58,40,1,1},
            LR{59,40,1,6},
            LR{60,40,1,3},
            LR{65,40,1,2},
            LR{66,40,7,0},
            LR{73,40,1,1},
            LR{74,40,1,6},
            LR{75,40,1,3},
            LR{84,40,1,3},
            LR{85,40,7,0},
            LR{93,40,1,5},
            LR{94,40,1,4},
            LR{106,40,1,2},
            LR{107,40,7,0},
            LR{114,40,1,5},
            LR{115,40,2,3},
            LR{7,42,1,5},
            LR{8,42,1,4},
            LR{9,42,1,3},
            LR{10,42,14,0},
            LR{24,42,1,6},
            LR{29,42,2,3},
            LR{35,42,1,2},
            LR{36,42,7,0},
            LR{43,42,1,1},
            LR{44,42,1,6},
            LR{45,42,1,3},
            LR{54,42,1,3},
            LR{55,42,7,0},
            LR{65,42,1,2},
            LR{66,42,7,0},
            LR{73,42,1,1},
            LR{74,42,1,6},
            LR{75,42,1,3},
            LR{84,42,1,3},
            LR{85,42,7,0},
            LR{93,42,1,5},
            LR{94,42,1,4},
            LR{106,42,1,2},
            LR{107,42,7,0},
            LR{114,42,1,5},
            LR{115,42,2,3},
            LR{7,43,1,5},
            LR{8,43,1,4},
            LR{9,43,1,3},
            LR{10,43,14,0},
            LR{24,43,1,6},
            LR{29,43,2,3},
            LR{35,43,1,2},
            LR{36,43,7,0},
            LR{43,43,1,1},
            LR{44,43,1,6},
            LR{45,43,1,3},
            LR{54,43,1,3},
            LR{55,43,7,0},
            LR{65,43,1,2},
            LR{66,43,7,0},
            LR{73,43,1,1},
            LR{74,43,1,6},
            LR{75,43,1,3},
            LR{84,43,1,3},
            LR{85,43,7,0},
            LR{93,43,1,5},
            LR{94,43,1,4},
            LR{106,43,1,2},
            LR{107,43,7,0},
            LR{114,43,1,5},
            LR{115,43,2,3},
            LR{7,44,1,5},
            LR{8,44,1,4},
            LR{9,44,1,3},
            LR{10,44,14,0},
            LR{24,44,1,6},
            LR{29,44,2,3},
            LR{35,44,1,2},
            LR{36,44,7,0},
            LR{43,44,1,1},
            LR{44,44,1,6},
            LR{45,44,1,3},
            LR{54,44,1,3},
            LR{55,44,7,0},
            LR{65,44,1,2},
            LR{66,44,7,0},
            LR{73,44,1,1},
            LR{74,44,1,6},
            LR{75,44,1,3},
            LR{84,44,1,3},
            LR{85,44,7,0},
            LR{93,44,1,5},
            LR{94,44,1,4},
            LR{106,44,1,2},
            LR{107,44,7,0},
            LR{114,44,1,5},
            LR{115,44,2,3},
            LR{7,45,1,5},
            LR{8,45,1,4},
            LR{9,45,1,3},
            LR{10,45,14,0},
            LR{24,45,1,6},
            LR{29,45,2,3},
            LR{35,45,1,2},
            LR{36,45,7,0},
            LR{43,45,1,1},
            LR{44,45,1,6},
            LR{45,45,1,3},
            LR{54,45,1,3},
            LR{55,45,7,0},
            LR{65,45,1,2},
            LR{66,45,7,0},
            LR{73,45,1,1},
            LR{74,45,1,6},
            LR{75,45,1,3},
            LR{84,45,1,3},
            LR{85,45,7,0},
            LR{93,45,1,5},
            LR{94,45,1,4},
            LR{106,45,1,2},
            LR{107,45,7,0},
            LR{114,45,1,5},
            LR{115,45,2,3},
            LR{7,46,1,5},
            LR{9,46,1,3},
            LR{10,46,14,0},
            LR{24,46,1,6},
            LR{25,46,1,2},
            LR{26,46,1,1},
            LR{30,46,1,3},
            LR{35,46,1,2},
            LR{36,46,7,0},
            LR{43,46,1,1},
            LR{44,46,1,6},
            LR{45,46,1,3},
            LR{54,46,1,3},
            LR{55,46,7,0},
            LR{64,46,1,6},
            LR{65,46,1,2},
            LR{66,46,7,0},
            LR{73,46,1,1},
            LR{74,46,1,6},
            LR{75,46,1,3},
            LR{84,46,1,3},
            LR{85,46,7,0},
            LR{93,46,1,5},
            LR{94,46,1,4},
            LR{106,46,1,2},
            LR{107,46,7,0},
            LR{114,46,1,5},
            LR{115,46,2,3},
            LR{9,47,1,3},
            LR{10,47,14,0},
            LR{24,47,1,6},
            LR{25,47,1,2},
            LR{26,47,1,7},
            LR{27,47,1,1},
            LR{35,47,1,2},
            LR{36,47,7,0},
            LR{43,47,1,1},
            LR{44,47,1,6},
            LR{45,47,1,3},
            LR{54,47,1,3},
            LR{55,47,7,0},
            LR{63,47,2,6},
            LR{65,47,1,2},
            LR{66,47,7,0},
            LR{73,47,1,1},
            LR{74,47,1,6},
            LR{75,47,1,3},
            LR{84,47,1,3},
            LR{85,47,7,0},
            LR{93,47,1,5},
            LR{94,47,1,4},
            LR{106,47,1,2},
            LR{107,47,7,0},
            LR{114,47,1,5},
            LR{115,47,2,3},
            LR{9,48,1,3},
            LR{10,48,14,0},
            LR{24,48,1,6},
            LR{25,48,1,2},
            LR{26,48,1,7},
            LR{27,48,1,1},
            LR{35,48,1,2},
            LR{36,48,7,0},
            LR{43,48,1,1},
            LR{44,48,1,6},
            LR{45,48,1,3},
            LR{54,48,1,3},
            LR{55,48,7,0},
            LR{63,48,2,6},
            LR{65,48,1,2},
            LR{66,48,7,0},
            LR{73,48,1,1},
            LR{74,48,1,6},
            LR{75,48,1,3},
            LR{84,48,1,3},
            LR{85,48,7,0},
            LR{93,48,1,5},
            LR{94,48,1,4},
            LR{106,48,1,2},
            LR{107,48,7,0},
            LR{114,48,1,5},
            LR{115,48,2,3},
            LR{9,49,1,3},
            LR{10,49,14,0},
            LR{24,49,1,6},
            LR{25,49,1,2},
            LR{26,49,1,7},
            LR{27,49,1,1},
            LR{35,49,1,2},
            LR{36,49,7,0},
            LR{43,49,1,1},
            LR{44,49,1,6},
            LR{45,49,1,3},
            LR{54,49,1,3},
            LR{55,49,7,0},
            LR{63,49,2,6},
            LR{65,49,1,2},
            LR{66,49,7,0},
            LR{73,49,1,1},
            LR{74,49,1,6},
            LR{75,49,1,3},
            LR{84,49,1,3},
            LR{85,49,7,0},
            LR{93,49,1,5},
            LR{94,49,1,4},
            LR{106,49,1,2},
            LR{107,49,7,0},
            LR{114,49,1,5},
            LR{115,49,2,3},
            LR{9,50,1,3},
            LR{10,50,14,0},
            LR{24,50,1,6},
            LR{25,50,1,2},
            LR{26,50,1,7},
            LR{27,50,1,1},
            LR{35,50,1,2},
            LR{36,50,7,0},
            LR{43,50,1,1},
            LR{44,50,1,6},
            LR{45,50,1,3},
            LR{54,50,1,3},
            LR{55,50,7,0},
            LR{63,50,2,6},
            LR{65,50,1,2},
            LR{66,50,7,0},
            LR{73,50,1,1},
            LR{74,50,1,6},
            LR{75,50,1,3},
            LR{84,50,1,3},
            LR{85,50,7,0},
            LR{93,50,1,5},
            LR{94,50,1,4},
            LR{106,50,1,2},
            LR{107,50,7,0},
            LR{114,50,1,5},
            LR{115,50,2,3},
            LR{10,52,1,2},
            LR{11,52,1,7},
            LR{12,52,1,1},
            LR{25,52,1,2},
            LR{26,52,1,7},
            LR{27,52,1,1},
            LR{36,52,1,2},
            LR{37,52,2,5},
            LR{44,52,1,6},
            LR{45,52,1,3},
            LR{55,52,2,2},
            LR{57,52,1,1},
            LR{63,52,2,6},
            LR{66,52,1,2},
            LR{67,52,2,5},
            LR{74,52,1,6},
            LR{75,52,1,3},
            LR{85,52,2,2},
            LR{87,52,1,1},
            LR{93,52,1,5},
            LR{94,52,1,4},
            LR{108,52,1,5},
            LR{109,52,1,4},
            LR{115,52,2,3},
            LR{10,53,1,2},
            LR{11,53,1,7},
            LR{12,53,1,1},
            LR{25,53,1,2},
            LR{26,53,1,7},
            LR{27,53,1,1},
            LR{36,53,1,2},
            LR{37,53,2,5},
            LR{44,53,1,6},
            LR{45,53,1,3},
            LR{55,53,2,2},
            LR{57,53,1,1},
            LR{63,53,2,6},
            LR{66,53,1,2},
            LR{67,53,2,5},
            LR{74,53,1,6},
            LR{75,53,1,3},
            LR{85,53,2,2},
            LR{87,53,1,1},
            LR{93,53,1,5},
            LR{94,53,1,4},
            LR{108,53,1,5},
            LR{109,53,1,4},
            LR{115,53,2,3},
            LR{10,54,1,2},
            LR{11,54,1,7},
            LR{12,54,1,1},
            LR{25,54,1,2},
            LR{26,54,1,7},
            LR{27,54,1,1},
            LR{36,54,1,2},
            LR{37,54,2,5},
            LR{44,54,1,6},
            LR{45,54,1,3},
            LR{55,54,2,2},
            LR{57,54,1,1},
            LR{63,54,2,6},
            LR{66,54,1,2},
            LR{67,54,2,5},
            LR{74,54,1,6},
            LR{75,54,1,3},
            LR{85,54,2,2},
            LR{87,54,1,1},
            LR{93,54,1,5},
            LR{94,54,1,4},
            LR{108,54,1,5},
            LR{109,54,1,4},
            LR{115,54,2,3},
            LR{10,55,1,2},
            LR{11,55,1,7},
            LR{12,55,14,0},
            LR{26,55,1,7},
            LR{27,55,1,1},
            LR{36,55,1,2},
            LR{37,55,1,5},
            LR{38,55,7,0},
            LR{45,55,1,3},
            LR{55,55,2,2},
            LR{57,55,6,0},
            LR{63,55,2,6},
            LR{66,55,1,2},
            LR{67,55,1,5},
            LR{68,55,7,0},
            LR{75,55,1,3},
            LR{85,55,2,2},
            LR{87,55,6,0},
            LR{93,55,1,5},
            LR{94,55,1,4},
            LR{108,55,1,5},
            LR{109,55,7,0},
            LR{116,55,1,3},
            LR{10,56,1,2},
            LR{11,56,16,0},
            LR{27,56,1,1},
            LR{36,56,1,2},
            LR{37,56,9,0},
            LR{55,56,1,2},
            LR{56,56,8,0},
            LR{64,56,1,6},
            LR{66,56,1,2},
            LR{67,56,9,0},
            LR{85,56,1,2},
            LR{86,56,8,0},
            LR{94,56,1,4},
            LR{108,56,9,0}
    };
    auto vkrt_logo=[&](int x,int y,int width,int height,bool signature){
        constexpr int sw=132,sh=69;
        for(const auto& run:logo_runs){
            const int x0=x+(int(run.x)*width)/sw;
            const int x1=x+((int(run.x)+int(run.n))*width+sw-1)/sw;
            const int y0=y+(int(run.y)*height)/sh;
            const int y1=y+((int(run.y)+1)*height+sh-1)/sh;
            p.fill(l.rect(x0,y0,std::max(1,x1-x0),std::max(1,y1-y0)),logo_colors[run.c]);
        }
        if(signature)p.text(l.rect(x,y+height+4,width,22),L"Francisco Triemstra 2026",ink,0);
    };
    auto text=[&](int x,int y,int width,int height,std::wstring_view value,unsigned font=0,COLORREF color=RGB(247,247,247)){
        if(value==L"Remaster"){remaster_word(l.rect(x,y,width,height));return;}
        p.text(l.rect(x,y,width,height),value,color,font);
    };
    if(s.page==Page::game){
        p.fill({0,0,w,int(std::lround(56*l.scale))},shell);
        vkrt_logo(48,5,90,43,false);
        text(170,8,512,40,s.visual_notice.empty()?std::wstring(L"Visual: ")+visual_name(c.visual_mode):s.visual_notice);
    }else if(s.page==Page::pause){
        p.fill(l.rect(24,64,352,568),panel);
        vkrt_logo(48,70,145,60,false);text(48,139,304,58,L"Paused",2);
        text(48,588,304,22,L"R3 / Start / Esc to resume",1,muted);
        p.fill(l.rect(402,222,298,254),panel);
        text(426,228,250,24,L"Save states",3);
        text(426,447,250,22,s.tape_locked?L"Unavailable during tape sessions":L"Battery saves are automatic.",1,muted);
    }else if(s.page==Page::home){
        p.fill(l.rect(402,116,534,510),panel);
        vkrt_logo(48,24,255,105,true);
        // Explicit two lines retain predictable baselines at every DPI.
        text(48,163,326,52,L"Make time",2);text(48,213,326,52,L"for a game.",2);
        if(!s.session)text(48,356,310,24,L"Or drop a .nes file here",0,muted);
        text(48,419,310,22,L"DEFAULT VISUAL MODE",1,muted);
        text(48,449,310,30,visual_name(c.visual_mode),3);
        text(426,134,486,28,L"Recent ROMs",3);
        if(c.recents.empty()){
            p.fill(l.rect(450,278,438,1),card);
            text(450,320,424,32,L"Your next session starts here.",3);
            text(450,365,424,26,L"Open a ROM to add it to this list.",0,muted);
            text(450,400,424,26,L"Only file paths are remembered.",0,muted);
        }
        if(s.notice.empty())text(48,641,864,24,L"Your files. Your settings. All local.",1,muted);
        if(s.selection>=0&&unsigned(s.selection)<l.count){
            auto cmd=unsigned(l.items[std::size_t(s.selection)].command);
            if(cmd>=unsigned(Command::recent0)&&cmd<unsigned(Command::recent0)+c.recents.size())
                text(426,604,486,20,c.recents[cmd-unsigned(Command::recent0)],1,muted);
        }
    }else if(s.page!=Page::game){
        p.fill(l.rect(24,190,912,410),panel);
        text(48,55,700,54,L"Settings",2);
        vkrt_logo(758,42,140,66,false);
        if(s.page==Page::video){
            text(48,204,864,28,std::wstring(L"Active: ")+visual_name(c.visual_mode)+L"  /  saved for next launch",0,muted);
            text(48,316,864,24,L"F1 Original   /   F4 Remaster   /   LT or RT toggles in game",1,muted);
            text(48,510,864,24,L"Integer scaling keeps whole presentation pixels when they fit.",0,muted);
            text(48,548,864,24,L"Alt + Enter also toggles fullscreen.",0,muted);
        }else if(s.page==Page::audio){
            text(48,228,864,32,L"Audio output",3);
            text(48,290,864,32,s.audio_status);
            text(242,354,122,54,std::to_wstring(c.volume)+L"%",3);
            text(48,446,864,28,L"The active system output is selected automatically.",0,muted);
        }else if(s.page==Page::controls){
            text(48,191,424,26,L"Keyboard",0,muted);text(500,191,412,26,L"Controller",0,muted);
            text(48,469,864,24,s.pad_binding>=0?L"Listening... Release controls, then press a button or trigger.":L"D-pad / left stick: navigate. A: confirm. B: back. R3: menu / resume.",0,muted);
            text(48,495,864,24,s.pad_binding>=0?L"R3 / Esc or tap B cancels; hold B for 0.6 s to bind B.":s.binding>=0?L"R3 / Esc / B cancels. Duplicate keys are rejected.":L"LT / RT: Original / Remaster unless rebound. R3 is reserved for the menu.",1,muted);
            text(500,534,412,24,!c.gamepad_enabled?L"Disabled":s.gamepad_connected?L"Controller connected":L"Controller disconnected",1,muted);
        }else if(s.page==Page::advanced){
            text(48,405,864,28,L"Presenter",3);text(48,453,864,28,s.presenter);
            text(48,509,864,26,L"Diagnostic presenter overrides apply to this launch only.",0,muted);
            text(48,545,864,26,L"The saved production default is always COMPOSED.",0,muted);
        }
    }
    for(unsigned i=0;i<l.count;++i){
        const auto& item=l.items[i];auto r=item.rect;
        const bool focus=int(i)==s.selection;
        p.fill(r,item.primary?accent:item.selected?RGB(72,25,32):card);
        if(item.selected||focus){
            int border=std::max(1,int(std::lround(l.scale*2)));
            p.fill({r.x,r.y,r.w,border},accent);p.fill({r.x,r.y+r.h-border,r.w,border},accent);
            p.fill({r.x,r.y,border,r.h},accent);p.fill({r.x+r.w-border,r.y,border,r.h},accent);
        }
        int inset=std::max(1,int(16*l.scale));r.x+=inset;r.w=std::max(0,r.w-2*inset);
        if(item.label==L"Remaster")remaster_word(r);else p.text(r,item.label,!item.enabled?muted:ink);
    }
    if(s.page==Page::game&&c.stats&&!s.statistics.empty()){
        auto box=l.rect(24,18,912,34);p.fill(box,panel);text(36,18,888,34,s.statistics,1);
    }
    if(!s.notice.empty()&&s.error.empty()){
        const bool settings=s.page==Page::video||s.page==Page::audio||s.page==Page::controls||s.page==Page::advanced;
        text(48,settings?578:638,864,24,s.notice,1,muted);
    }
    if(s.loading){
        p.fill(l.rect(240,262,480,162),panel);text(274,281,412,46,L"Opening ROM...",3);
        text(274,343,412,48,L"Checking the file before switching sessions.",0,muted);
    }
    if(!s.error.empty()){
        p.fill(l.rect(212,216,536,284),panel);
        text(242,240,476,42,L"Could not complete that action",3);
        p.text(l.rect(242,305,476,105),s.error,ink,0,DT_WORDBREAK);
        // Error action must remain above its panel.
        if(l.count){const auto& i=l.items[0];p.fill(i.rect,accent);p.text(i.rect,i.label,shell,0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    }
    GdiFlush();
    for(std::size_t i=3,n=std::size_t(w)*h*4;i<n;i+=4)p.data[i]=255;
}
void Canvas::blit(void* target) const {const auto& p=*impl_;BitBlt(static_cast<HDC>(target),0,0,p.w,p.h,p.dc,0,0,SRCCOPY);}
std::span<const std::uint8_t> Canvas::pixels() const {const auto& p=*impl_;return {p.data,std::size_t(p.w)*p.h*4};}
std::uint64_t Canvas::hash() const {std::uint64_t h=14695981039346656037ull;for(auto c:pixels())h=(h^c)*1099511628211ull;return h;}
void Canvas::write_bmp(const std::filesystem::path& path) const {
    const auto& p=*impl_;std::ofstream f(path,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("cannot write UI screenshot");
    auto put=[&](std::uint32_t n,int bytes){for(int i=0;i<bytes;++i)f.put(char(n>>(8*i)));};
    f.write("BM",2);put(54+p.w*p.h*4,4);put(0,4);put(54,4);put(40,4);put(p.w,4);put(std::uint32_t(-p.h),4);
    put(1,2);put(32,2);put(0,4);put(p.w*p.h*4,4);put(0,4);put(0,4);put(0,4);put(0,4);
    f.write(reinterpret_cast<const char*>(p.data),std::streamsize(p.w)*p.h*4);f.close();if(!f)throw std::runtime_error("UI screenshot write failed");
}
std::array<std::uint64_t,4> ui_probe(const std::filesystem::path& directory){
    if(!directory.empty())std::filesystem::create_directories(directory);
    Config c;State s;Canvas canvas;
    std::vector<std::uint8_t> abstract(512*480*4,255);
    // Abstract source-free viewport witness; deliberately not a game mock-up.
    for(int y=0;y<480;++y)for(int x=0;x<512;++x){
        auto i=(y*512+x)*4;bool band=((x/64+y/60)%2)==0;
        abstract[i]=std::uint8_t(49+(band?12:0));abstract[i+1]=std::uint8_t(48+y/12);abstract[i+2]=std::uint8_t(27+x/16);
    }
    constexpr Page pages[]{Page::home,Page::pause,Page::video,Page::controls};
    constexpr const char* names[]{"home.bmp","pause.bmp","video.bmp","controls.bmp"};
    std::array<std::uint64_t,4> hashes{};
    for(unsigned i=0;i<4;++i){
        s.page=pages[i];s.session=i==1;s.selection=0;
        auto l=layout(s,c,1100,800,96);canvas.draw(s,c,l,1100,800,abstract);hashes[i]=canvas.hash();
        if(!directory.empty())canvas.write_bmp(directory/names[i]);
    }
    if(!directory.empty()){
        std::ofstream out(directory/"manifest.txt",std::ios::binary);
        out<<"UI_PROBE_VERSION=1\nWIDTH=1100\nHEIGHT=800\nDPI=96\nFONT=Segoe UI\nCONFIG_IO=NONE\nROM=NONE\nHASH=FNV1A64_BGRA\n";
        for(unsigned i=0;i<4;++i)out<<names[i]<<"="<<hashes[i]<<"\n";
        out.close();if(!out)throw std::runtime_error("UI manifest write failed");
    }
    return hashes;
}
}
