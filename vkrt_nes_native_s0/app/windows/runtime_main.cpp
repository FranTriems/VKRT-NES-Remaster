#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <runtime_audio.hpp>
#include <runtime_presenter.hpp>
#include <frontend_ui.hpp>
#include <frontend_product.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/hash.hpp"
#include "vkrt/nes_native_s0/runtime.hpp"

using namespace vkrt::nes;
using namespace vkrt::nes::runtime;
using namespace vkrt::nes::host;
namespace ui=vkrt::nes::frontend;
namespace {
constexpr int kRenderW=512,kRenderH=480;
using clock_type=std::chrono::steady_clock;
Bytes read_rom(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot open ROM");
    auto n=f.tellg();
    if(n<16||n>std::streamoff(32*1024*1024+528))throw std::runtime_error("ROM size outside bounded runtime range");
    Bytes b(static_cast<std::size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);
    if(!f)throw std::runtime_error("ROM read incomplete");return b;
}
unsigned infer_sample_rate(const Bytes& bytes){
    Machine probe(bytes);probe.run_frames(1);probe.apu.pcm.clear();
    std::uint64_t samples=0;constexpr unsigned frames=240;
    for(unsigned i=0;i<frames;++i){probe.run_frames(1);samples+=probe.apu.pcm.size();probe.apu.pcm.clear();}
    const double hz=double(kNtscFrameRateNumerator)/double(kNtscFrameRateDenominator);
    const auto rate=unsigned(std::llround(double(samples)*hz/frames));
    if(rate<20000||rate>96000)throw std::runtime_error("APU PCM rate inference outside 20..96 kHz");
    return rate;
}


struct Options {
    std::filesystem::path rom,record,replay,ui_smoke,persistence_root;
    std::wstring presenter{L"composed"};
};
Options parse(int argc,wchar_t** argv){
    Options o;
    for(int i=1;i<argc;++i){
        std::wstring a=argv[i];
        if(a==L"--record"&&i+1<argc)o.record=argv[++i];
        else if(a==L"--replay"&&i+1<argc)o.replay=argv[++i];
        else if(a==L"--presenter"&&i+1<argc){
            o.presenter=argv[++i];
            if(o.presenter!=L"composed"&&o.presenter!=L"flip"&&o.presenter!=L"gdi")
                throw std::runtime_error("presenter must be composed, flip, or gdi");
        }else if(!a.starts_with(L"--")&&o.rom.empty())o.rom=a;
        else throw std::runtime_error("unknown or incomplete launch option");
    }
    if(!o.record.empty()&&!o.replay.empty())throw std::runtime_error("record and replay are mutually exclusive");
    if(o.rom.empty()&&(!o.record.empty()||!o.replay.empty()))throw std::runtime_error("record/replay requires a ROM path");
    return o;
}
double p95(std::deque<double> v){
    if(v.empty())return 0;std::vector<double>x(v.begin(),v.end());std::sort(x.begin(),x.end());
    return x[std::min(x.size()-1,std::size_t(std::ceil(x.size()*.95)-1))];
}
int probe(const std::filesystem::path& rompath){
    auto bytes=read_rom(rompath);const auto rate=infer_sample_rate(bytes);
    Machine m(bytes);ImmutableFrame prev;
    for(int i=0;i<3;++i){m.run_frames(1);prev=observe(*m.frame(),prev.get());m.apu.pcm.clear();}
    const auto im=render(*prev,Mode::MODERN_REMASTER,kRenderW,kRenderH);
    if(im.rgb.empty())throw std::runtime_error("runtime probe render empty");
    RationalFramePacer pacer;std::uint64_t ns=0;for(int i=0;i<100000;++i)ns+=pacer.next_period_ns();
    ReplayTape tape(sha256(bytes));tape.append({0,0});tape.append({1,kReplayWarmReset});
    const auto temp=std::filesystem::temp_directory_path()/"vkrt_nes_runtime_probe.vkrtrpl";
    tape.save(temp);auto round=ReplayTape::load(temp);std::error_code ec;std::filesystem::remove(temp,ec);
    if(round.frames()!=tape.frames())throw std::runtime_error("runtime replay probe mismatch");
    std::cout<<"PROBE=PASS\nAUDIO_RATE="<<rate<<"\nPACER_100K_NS="<<ns<<"\nROM_SHA256="<<sha256(bytes)<<"\n";
    return 0;
}
int stress(const std::filesystem::path& path){
    const auto bytes=read_rom(path);const auto rate=infer_sample_rate(bytes);
    Machine machine(bytes);std::vector<std::int16_t> pcm;
    for(int i=0;i<600;++i){machine.run_frames(1);pcm.insert(pcm.end(),machine.apu.pcm.begin(),machine.apu.pcm.end());machine.apu.pcm.clear();}
    std::uint64_t underruns=0,dropped=0;int maximum=0;
    for(int drift:{-1000,1000}){
        auto result=simulate_audio(rate,48000,480,drift,240,pcm);
        underruns+=result.underruns;dropped+=result.dropped;maximum=std::max(maximum,result.max_correction_ppm);
        std::cout<<"CLOCK_PPM="<<drift<<" CHECKSUM="<<result.checksum<<" FIFO_MS="<<result.final_occupancy_ms<<"\n";
    }
    const bool pass=underruns==0&&dropped==0&&maximum<=3000;
    std::cout<<"STRESS_SIM="<<(pass?"PASS":"FAIL")<<"\nUNDERRUN_SAMPLES="<<underruns
             <<"\nDROPPED_SAMPLES="<<dropped<<"\nMAX_DRC_PPM="<<maximum<<"\nSIMULATED_SECONDS=480\nPHYSICAL_ENDPOINT_OPENED=NO\n";
    return pass?0:1;
}
int polish_probe(const std::filesystem::path& rom,const std::filesystem::path& output){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto bytes=read_rom(rom);
    const auto parent=std::filesystem::absolute(output);std::filesystem::create_directories(parent);
    const auto root=parent/L"polish-probe";
    require(std::filesystem::create_directory(root),"Polish probe requires a new output namespace");
    std::ostringstream report;report<<"POLISH_PROBE_VERSION=1\nUSER_STORAGE=NOT_ACCESSED\nROM_SHA256="<<sha256(bytes)<<"\n";
    constexpr const char* frozen_f1[]{"69a4510ca6feddea6349fb211f63ffaf11d263dd003383efa79826a804b9931a","d6db1a8adf1e7352e92558ba3b05539ff93d99b324b6d1ec9b8a05ad742918b1"};
    constexpr const char* frozen_f4[]{"eb055743e782240122a0b3d584248178f16e982989990df5f61565788822d457","e472225b75133850260951b74d710db5861693b17f12e78fbd7510597b9d452d"};
    for(unsigned fixture=0;fixture<2;++fixture){
        Machine m(fixture?synthetic_nrom():bytes);ImmutableFrame previous;
        for(unsigned frame=1;frame<=120;++frame){
            m.run_frames(1);m.apu.pcm.clear();
            if(frame!=60&&frame!=120)continue;
            const auto canonical=m.serialize(),state=m.save_state();
            auto observed=observe(*m.frame(),previous.get());previous=observed;
            std::array<Image,4> images;
            // Legacy renderers remain available to this offline diagnostic only.
            constexpr Mode diagnostic_modes[]{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD,Mode::MODERN_REMASTER};
            for(unsigned mode=0;mode<4;++mode){
                images[mode]=render_reference(*observed,diagnostic_modes[mode],512,480);
                const auto name=std::string(fixture?"fixture-":"rom-")+std::to_string(frame)+"-F"+std::to_string(mode+1);
                write_ppm(root/(name+".ppm"),images[mode]);report<<name<<"="<<sha256(images[mode].rgb)<<"\n";
                require(m.serialize()==canonical&&m.save_state()==state,"Visual mode changed machine state");
            }
            require(render_reference(*observed,Mode::EXACT_DIGITAL,512,480)==images[0],"F1 roundtrip mismatch");
            report<<(fixture?"FIXTURE":"ROM")<<"_"<<frame<<"_F1_NE_F3="<<(images[0]!=images[2]?"PASS":"NO_ELIGIBLE_EDGES")<<"\n";
            if(fixture){
                require(images[0]!=images[2]&&images[2]!=images[3],"Semantic HD no-op on qualified fixture");
                const auto n=frame/60-1;
                require(sha256(images[0].rgb)==frozen_f1[n]&&sha256(images[3].rgb)==frozen_f4[n],"Frozen qualified output changed");
            }
        }
    }
    ui::controller_diagnostics(report);
    ui::Config bindings;bindings.pad[0]=ui::pad_lt;bindings.pad[1]=ui::pad_rt;bindings.visual_mode=3;
    std::wstring warning;
    require(ui::save_config(root/L"bindings.ini",bindings,warning),"Polish controller config write failed");
    require(ui::load_config(root/L"bindings.ini",warning)==bindings&&warning.empty(),"Polish controller config disk roundtrip failed");
    report<<"CONTROLLER_BINDINGS_DISK_ROUNDTRIP=PASS\n";
    report<<"F1_NE_F3=PASS\nF3_NE_F4=PASS\nF1_ROUNDTRIP_BYTE_EXACT=PASS\nMACHINE_STATE_INVARIANT=PASS\nF1_F4_BASELINE_HASHES=PASS\nPOLISH_PROBE=PASS\n";
    std::ofstream manifest(root/L"manifest.txt",std::ios::binary);manifest<<report.str();manifest.close();
    require(bool(manifest),"Polish manifest write failed");std::cout<<report.str();return 0;
}

// Session ownership is a frontend concern. The canonical frame, replay/reset
// ordering, observer queue and endpoint service remain the R5.2 runtime path.
class Session {
public:
    Session(Bytes bytes,unsigned sample_rate,const Options& options,ui::Input& input,HWND window):
        options_(options),machine_(std::make_unique<Machine>(bytes)),
        recording_(sha256(bytes)),audio(sample_rate),
        stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),ready_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){
        if(!stop_.value||!ready_.value)throw std::runtime_error("runtime event creation failed");
        if(!options_.replay.empty()){
            replay_=ReplayTape::load(options_.replay);
            if(replay_.rom_sha256()!=recording_.rom_sha256())throw std::runtime_error("replay ROM SHA mismatch");
        }
        audio.paused.store(true);
        if(!locked_tape()){
            std::string warning;ui::ProductStore store(options_.persistence_root);
            if(!store.load_battery(*machine_,warning))persistence_notice="Battery save was not loaded: "+warning;
            battery_saved_=machine_->save_battery();
        }
        try{
            observer_=std::jthread([this]{
                try{
                    ImmutableFrame previous;std::uint64_t epoch=0;Ticket ticket;
                    HANDLE events[]{stop_.value,ready_.value};
                    while(!quit.load()){
                        if(!pending_.pop(ticket)){
                            const auto result=WaitForMultipleObjects(2,events,FALSE,INFINITE);
                            if(result==WAIT_OBJECT_0)break;
                            if(result==WAIT_FAILED)throw std::runtime_error("observer event wait failed");
                            continue;
                        }
                        while(pending_.size_approx()>=2){Ticket newer;if(!pending_.pop(newer))break;ticket=std::move(newer);++semantic_drops;}
                        if(ticket.epoch!=epoch){previous.reset();epoch=ticket.epoch;}
                        const auto begin=clock_type::now();
                        previous=observe(*ticket.frame,previous.get());ticket.frame.reset();
                        {std::lock_guard lock(publication_mutex_);if(ticket.epoch==timeline_.load())mailbox.publish(previous);}
                        observer_us.store(std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now()-begin).count());
                    }
                }catch(const std::exception& e){fail(e);}
            });
            emulation=std::jthread([this,&input,window]{
                try{
                    Mmcss scheduling(L"Games");DeadlineTimer timer;
                    auto& machine=*machine_;RationalFramePacer phase;
                    auto next=now_ns();bool was_paused=true;std::size_t replay_index=0;
                    while(!quit.load()){
                        if(paused.load()){
                            if(!was_paused){audio.paused.store(true);audio.flush();was_paused=true;}
                            if(!timer.wait(now_ns()+5000000,stop_.value))break;
                            next=now_ns();continue;
                        }
                        if(was_paused){was_paused=false;audio.paused.store(false);next=now_ns();}
                        next=bounded_deadline(next+phase.next_period_ns(),now_ns());
                        if(!timer.wait(next,stop_.value))break;
                        std::lock_guard machine_lock(machine_mutex_);
                        if(paused.load())continue;
                        ReplayFrame in{};bool reset=false;
                        if(!options_.replay.empty()){
                            if(replay_index>=replay_.frames().size()){quit.store(true);SetEvent(stop_.value);break;}
                            in=replay_.frames()[replay_index++];reset=(in.commands&kReplayWarmReset)!=0;
                        }else{
                            in.buttons=input.sample(window);reset=reset_requested.exchange(false);in.commands=reset?kReplayWarmReset:0;
                            if(!options_.record.empty())recording_.append(in);
                        }
                        if(reset){machine.reset();machine.apu.pcm.clear();audio.flush();++timeline_;}
                        machine.controllers[0].buttons=in.buttons;
                        const auto begin=clock_type::now();machine.run_frames(1);
                        audio.submit(machine.apu.pcm);machine.apu.pcm.clear();
                        if(pending_.push({machine.frame(),timeline_.load()}))SetEvent(ready_.value);else ++semantic_drops;
                        emu_work_us.store(std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now()-begin).count());++emu_frames;
                    }
                }catch(const std::exception& e){fail(e);}
            });
            auto stat_start=clock_type::now();
            started_=stat_start;
        }catch(...){stop();throw;}
    }
    ~Session(){stop();}
    void stop() noexcept {
        quit.store(true);SetEvent(stop_.value);SetEvent(ready_.value);
        if(emulation.joinable())emulation.join();if(observer_.joinable())observer_.join();audio.stop();
    }
    void flush_battery(){
        if(locked_tape())return;
        Bytes bytes;std::filesystem::path path;
        {std::lock_guard lock(machine_mutex_);
            bytes=machine_->save_battery();
            if(bytes.empty()||bytes==battery_saved_)return;
            path=ui::ProductStore(options_.persistence_root).battery_path(*machine_);
        }
        ui::atomic_product_write(path,bytes);battery_saved_=std::move(bytes);
    }
    void state_action(bool save,unsigned slot){
        if(locked_tape())throw std::runtime_error("States are unavailable during recording or replay");
        paused.store(true);
        std::lock_guard machine_lock(machine_mutex_);
        audio.paused.store(true);
        ui::ProductStore store(options_.persistence_root);
        if(save)store.save_state(*machine_,slot);
        else{
            std::string error;if(!store.load_state(*machine_,slot,error))throw std::runtime_error(error);
            audio.flush();reset_requested.store(false);
            std::lock_guard publication_lock(publication_mutex_);++timeline_;
            mailbox.publish(machine_->frame()?observe(*machine_->frame()):ImmutableFrame{});
        }
    }
    void finish(){
        stop();flush_battery();
        if(!saved_&&!options_.record.empty()){recording_.save(options_.record);saved_=true;std::cout<<"REPLAY_SAVED="<<ui::utf8(options_.record.wstring())<<"\n";}
    }
    std::string error(){std::lock_guard<std::mutex> lock(error_mutex_);return error_;}
    bool locked_tape() const {return !options_.record.empty()||!options_.replay.empty();}
    bool replaying() const {return !options_.replay.empty();}
    PresentationProfile profile() const {return presentation_profile(recording_.rom_sha256());}
    std::string persistence_notice;
    std::atomic<bool> paused{true},quit{false},reset_requested{false};
    std::atomic<std::uint64_t> emu_frames{0},emu_work_us{0},observer_us{0},semantic_drops{0};
    LatestMailbox<ImmutableFrame> mailbox;
private:
    Options options_;
    std::unique_ptr<Machine> machine_;
    ReplayTape recording_,replay_;
public:
    EndpointAudio audio;
private:
    Handle stop_,ready_;
    struct Ticket {ImmutableFrame frame;std::uint64_t epoch{};};
    SpscRing<Ticket,4> pending_;
    std::mutex error_mutex_,machine_mutex_,publication_mutex_;std::string error_;
    std::atomic<std::uint64_t> timeline_{1};
    Bytes battery_saved_;
    std::jthread observer_,emulation;
    clock_type::time_point started_{};
    bool saved_{};
    void fail(const std::exception& e){
        {std::lock_guard<std::mutex> lock(error_mutex_);error_=e.what();}
        quit.store(true);SetEvent(stop_.value);
    }
};

class Application;
Application* application{};
LRESULT CALLBACK window_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK game_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK stats_proc(HWND,UINT,WPARAM,LPARAM);
unsigned window_dpi(HWND window){
    using GetDpi=UINT(WINAPI*)(HWND);
    static auto get=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    return get?get(window):96;
}
void dpi_awareness(){
    using SetDpi=BOOL(WINAPI*)(HANDLE);
    auto set=reinterpret_cast<SetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    if(!set||!set(reinterpret_cast<HANDLE>(-4)))SetProcessDPIAware();
}
class Application {
public:
    explicit Application(Options options):options_(std::move(options)){
        if(options_.ui_smoke.empty()){path_=ui::config_path();config_=ui::load_config(path_,state_.notice);}
        options_.persistence_root=options_.ui_smoke.empty()?path_.parent_path():options_.ui_smoke;
        input_.configure(config_.keys);input_.configure_pad(config_.pad);input_.enable_gamepad(config_.gamepad_enabled);
    }
    ~Application(){
        if(session_)session_->stop();
        // A staged session owns its bytes and starts paused. Awaiting destruction
        // cannot touch the live Machine or leave callbacks targeting a dead HWND.
        if(loading_.valid())try{loading_.get();}catch(...){}
        if(IsWindow(window_))DestroyWindow(window_);
        application=nullptr;
    }
    int run(){
        application=this;dpi_awareness();
        HINSTANCE instance=GetModuleHandleW(nullptr);
        auto register_class=[&](const wchar_t* name,WNDPROC proc){
            WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=name;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
            if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("window class unavailable");
        };
        register_class(L"VKRT_NES_HOME",window_proc);register_class(L"VKRT_NES_VIEWPORT",game_proc);register_class(L"VKRT_NES_STATS",stats_proc);
        RECT wr{0,0,1100,800};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_ACCEPTFILES);
        window_=CreateWindowExW(WS_EX_ACCEPTFILES,L"VKRT_NES_HOME",L"VKRT NES",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,instance,nullptr);
        if(!window_)throw std::runtime_error("main window unavailable");
        game_=CreateWindowExW(0,L"VKRT_NES_VIEWPORT",L"",WS_CHILD,0,0,512,480,window_,nullptr,instance,nullptr);
        stats_=CreateWindowExW(0,L"VKRT_NES_STATS",L"",WS_CHILD,0,0,512,32,window_,nullptr,instance,nullptr);
        if(!game_||!stats_)throw std::runtime_error("game viewport unavailable");
        if(options_.presenter!=L"gdi")presenter_.initialize(game_,options_.presenter==L"flip"?PresenterKind::FLIP_DIAG:PresenterKind::COMPOSED_BLT);
        state_.presenter=ui::from_utf8(presenter_.backend());
        if(options_.presenter==L"flip")state_.presenter+=L"  (diagnostic launch only)";
        if(options_.presenter==L"gdi")state_.presenter=L"GDI  (diagnostic launch override)";
        ShowWindow(window_,options_.ui_smoke.empty()?SW_SHOW:SW_HIDE);
        DragAcceptFiles(window_,TRUE);DragAcceptFiles(game_,TRUE);DragAcceptFiles(stats_,TRUE);fullscreen();
        if(!options_.rom.empty())open(options_.rom);
        auto stat_start=clock_type::now();std::uint64_t last_frames=0,rendered=0;
        while(running_){
            MSG msg{};
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                if(msg.message==WM_QUIT){running_=false;break;}
                TranslateMessage(&msg);DispatchMessageW(&msg);
            }
            if(!running_)break;
            if(loading_.valid()&&loading_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
                try{
                    auto candidate=loading_.get();
                    if(session_)session_->finish();
                    session_=std::move(candidate);state_.session=true;state_.replay=session_->replaying();state_.tape_locked=session_->locked_tape();
                    state_.profile=session_->profile();config_.visual_mode=profile_mode(state_.profile,config_.visual_mode);
                    session_->audio.set_volume(config_.volume,config_.mute);
                    if(!session_->persistence_notice.empty())state_.notice=ui::from_utf8(session_->persistence_notice);
                    state_.page=options_.ui_smoke.empty()?ui::Page::game:ui::Page::pause;
                    state_.binding=-1;sequence_=0;visible_.reset();frame_.clear();last_frames=rendered=0;
                    ui::remember(config_,opening_path_.wstring());persist();
                    SetWindowTextW(window_,(L"VKRT NES  |  "+opening_path_.filename().wstring()).c_str());
                    std::cout<<"SESSION_OPEN=PASS\nPRESENTER="<<presenter_.backend()<<"\nAUDIO_BACKEND="<<session_->audio.backend()<<"\n";
                }catch(const std::exception& e){state_.error=L"The ROM was not opened.\n"+message(e);
                    if(session_)state_.error+=L"\nYour previous session is still available.";}
                state_.loading=false;dirty_=true;sync_pause();
            }
            sync_pause();
            controller_tick(input_.snapshot(),GetTickCount64());
            bool fresh=false;
            if(session_){
                if(session_->quit.load()){
                    auto error=session_->error();
                    if(error.empty()&&session_->replaying()){running_=false;break;}
                    session_->finish();session_.reset();state_.session=false;state_.page=ui::Page::home;
                    state_.error=L"The session stopped: "+ui::from_utf8(error.empty()?"runtime stopped":error);dirty_=true;
                }else {fresh=session_->mailbox.consume_after(sequence_,visible_);if(fresh&&!visible_){frame_.clear();dirty_=true;}}
            }
            if(visible_&&(fresh||last_mode_!=config_.visual_mode)){
                const auto begin=clock_type::now();
                const auto image=render_profile(*visible_,ui::visual_mode(config_.visual_mode),state_.profile,kRenderW,kRenderH);
                frame_.resize(kRenderW*kRenderH*4);
                ui::compose_playfield(*visible_,image,frame_);
                last_mode_=config_.visual_mode;present_dirty_=true;if(!game_visible_)dirty_=true;++rendered;
                render_times_.push_back(std::chrono::duration<double,std::milli>(clock_type::now()-begin).count());
                if(render_times_.size()>240)render_times_.pop_front();
            }
            if(dirty_)refresh();
            if(!options_.ui_smoke.empty())smoke_step();
            if(game_visible_&&!frame_.empty()&&(fresh||present_dirty_)){
                presenter_.wait_ready();
                if(!presenter_.present(frame_)){
                    state_.presenter=ui::from_utf8(presenter_.backend());
                    InvalidateRect(game_,nullptr,FALSE);UpdateWindow(game_);
                }
                present_dirty_=false;
            }
            const auto now=clock_type::now();
            if(session_&&now-stat_start>=std::chrono::seconds(1)){
                const double seconds=std::chrono::duration<double>(now-stat_start).count();
                const auto frames=session_->emu_frames.load();const double emu=double(frames-last_frames)/seconds;last_frames=frames;
                auto& audio=session_->audio;
                state_.audio_status=ui::from_utf8(audio.backend())+L"  /  "+std::to_wstring(audio.rate.load())+L" Hz  /  "+std::to_wstring(audio.period.load())+L" frames";
                if(session_->paused.load())state_.audio_status+=L"  /  Paused";
                state_.statistics=L"EMU "+std::to_wstring(int(emu+.5))+L"  FPS "+std::to_wstring(int(rendered/seconds+.5))+L"  |  "+ui::from_utf8(audio.backend())+
                    L"  "+std::to_wstring(audio.rate.load())+L" Hz  |  U "+std::to_wstring(audio.stream.underruns.load())+L"  D "+std::to_wstring(audio.stream.dropped.load());
                if(config_.stats){InvalidateRect(stats_,nullptr,FALSE);dirty_=true;}
                if(state_.page==ui::Page::audio)dirty_=true;
                std::cout<<"EMU="<<emu<<" FPS="<<rendered/seconds<<" EMU_MS="<<session_->emu_work_us.load()/1000.
                    <<" OBSERVE_MS="<<session_->observer_us.load()/1000.<<" RENDER_P95_MS="<<p95(render_times_)
                    <<" AUDIO_BACKEND="<<audio.backend()<<" ENDPOINT_RATE="<<audio.rate.load()
                    <<" AUDIO_UNDERRUN_SAMPLES="<<audio.stream.underruns.load()<<" AUDIO_DROPPED_SAMPLES="<<audio.stream.dropped.load()
                    <<" DRC_PPM="<<audio.stream.correction_ppm.load()<<" SEMANTIC_DROPS="<<session_->semantic_drops.load()<<"\n";
                if(!audio.backend_id.load()){
                    state_.error=L"The audio output is unavailable. Reopen the ROM after checking Windows sound settings.";state_.page=ui::Page::pause;dirty_=true;
                }
                stat_start=now;rendered=0;
            }
            MsgWaitForMultipleObjects(0,nullptr,FALSE,session_?2:16,QS_ALLINPUT);
        }
        if(session_){
            session_->finish();
            if(!options_.ui_smoke.empty()){
                auto bytes=synthetic_nrom();bytes[6]|=2;Machine witness(bytes);std::string error;
                if(!ui::ProductStore(options_.ui_smoke).load_battery(witness,error)||witness.mapper->battery_ram()[85]!=0x6b)
                    throw std::runtime_error("Clean exit battery flush failed");
                std::cout<<"UI_SMOKE=PASS\nPRODUCT_STATE_ROUTING=PASS\nBATTERY_SWITCH_RELOAD=PASS\nBATTERY_EXIT_FLUSH=PASS\nCONFIG_IO=NONE\nPHYSICAL_QUALIFICATION=NONE\n";
            }
            std::cout<<"EMU_FRAMES="<<session_->emu_frames.load()<<"\nAUDIO_UNDERRUN_SAMPLES="<<session_->audio.stream.underruns.load()
                <<"\nAUDIO_DROPPED_SAMPLES="<<session_->audio.stream.dropped.load()<<"\n";
        }
        return 0;
    }
    LRESULT event(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        switch(msg){
        case WM_CLOSE:running_=false;return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);if(canvas_ready_)canvas_.blit(dc);EndPaint(hwnd,&ps);return 0;}
        case WM_SIZE:state_.minimized=wp==SIZE_MINIMIZED;dirty_=present_dirty_=true;sync_pause();
            if(game_&&!state_.minimized)refresh();return 0;
        case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lp);unsigned dpi=window_dpi(hwnd);
            RECT r{0,0,MulDiv(800,dpi,96),MulDiv(600,dpi,96)};AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_ACCEPTFILES);
            m->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
        case WM_DPICHANGED:if(!full_.active){auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}dirty_=true;return 0;
        case WM_ACTIVATEAPP:state_.focused=wp!=0;sync_pause();return 0;
        case WM_KEYDOWN:if(!(lp&(1ll<<30)))key(unsigned(wp));return 0;
        case WM_SYSKEYDOWN:if(wp==VK_RETURN&&(lp&(1ll<<29))&&!(lp&(1ll<<30))){execute(ui::Command::fullscreen);return 0;}break;
        case WM_LBUTTONUP:{int hit=ui::hit_test(layout_,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(hit>=0)execute(layout_.items[hit].command);return 0;}
        case WM_MOUSEMOVE:{int hit=ui::hit_test(layout_,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(hit>=0&&hit!=state_.selection){state_.selection=hit;dirty_=true;}return 0;}
        case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(wp);UINT n=DragQueryFileW(drop,0,nullptr,0);std::wstring path(n+1,L'\0');
            if(n&&DragQueryFileW(drop,0,path.data(),n+1)){path.resize(n);open(path);}DragFinish(drop);return 0;}
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    void paint_game(HWND hwnd){
        PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
        if(!presenter_.active()&&!frame_.empty()){
            BITMAPINFO bmi{};bmi.bmiHeader={sizeof(BITMAPINFOHEADER),512,-480,1,32,BI_RGB,0,0,0,0,0};SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,0,0,rc.right,rc.bottom,0,0,512,480,frame_.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
        }
        EndPaint(hwnd,&ps);
    }
    void paint_stats(HWND hwnd){PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);ui::paint_stats(dc,r.right,r.bottom,state_.statistics);EndPaint(hwnd,&ps);}
    HWND window() const {return window_;}
private:
    Options options_;ui::Config config_;ui::State state_;ui::Layout layout_;ui::Canvas canvas_;ui::Input input_;
    ui::ControllerUiModel controller_;std::uint64_t visual_notice_until_{};
    std::filesystem::path path_,opening_path_;
    HWND window_{},game_{},stats_{};FlipPresenter presenter_;
    std::unique_ptr<Session> session_;
    std::future<std::unique_ptr<Session>> loading_;
    ui::FullscreenState full_;WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};LONG_PTR saved_style_{};
    bool running_{true},dirty_{true},present_dirty_{true},canvas_ready_{},game_visible_{};
    ImmutableFrame visible_;std::uint64_t sequence_{};unsigned last_mode_{99};std::vector<std::uint8_t> frame_;std::deque<double> render_times_;
    unsigned smoke_stage_{};Session* smoke_session_{};clock_type::time_point smoke_start_{clock_type::now()};
    static std::wstring message(const std::exception& e){try{return ui::from_utf8(e.what());}catch(...){return L"The operation failed.";}}
    void persist(){std::wstring error;if(options_.ui_smoke.empty()&&!ui::save_config(path_,config_,error))state_.notice=error;input_.configure(config_.keys);input_.configure_pad(config_.pad);input_.enable_gamepad(config_.gamepad_enabled);if(session_)session_->audio.set_volume(config_.volume,config_.mute);}
    void controller_tick(ui::PadSnapshot sample,std::uint64_t now){
        // Rebuild hit geometry before input so a just-opened page cannot execute
        // an item from the previous page. All device paths share this layout.
        RECT rc{};GetClientRect(window_,&rc);
        layout_=ui::layout(state_,config_,rc.right,rc.bottom,window_dpi(window_));
        const auto result=controller_.update(state_,config_,layout_,sample,now);
        if(result.changed){dirty_=true;sync_pause();}
        if(result.save)persist();
        if(result.command!=ui::Command::none)execute(result.command);
        if(visual_notice_until_&&now>=visual_notice_until_){visual_notice_until_=0;state_.visual_notice.clear();dirty_=true;}
    }
    void smoke_step(){
        // Noninteractive orchestration gate. Sessions stay paused and silent;
        // this is not a physical performance, sound or controller qualification.
        auto require=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
        require(clock_type::now()-smoke_start_<std::chrono::seconds(30),"UI smoke timeout");
        const auto rom=options_.ui_smoke/L"frontend-\u65e5\u672c\u8a9e.nes";
        if(smoke_stage_==0){
            require(!session_&&state_.page==ui::Page::home&&canvas_ready_,"no-ROM home launch");
            std::filesystem::create_directories(options_.ui_smoke);canvas_.write_bmp(options_.ui_smoke/"window-home.bmp");
            const bool focused=state_.focused;state_.focused=true;
            controller_tick({true},1000);controller_tick({true,2},1001);
            require(state_.selection==1,"runtime controller Home single-step");
            controller_tick({true,ui::pad_a},1002);
            require(state_.page==ui::Page::video,"runtime controller confirm Settings");
            execute(ui::Command::controls);controller_tick({true},1003);
            for(unsigned i=0;i<layout_.count;++i)if(layout_.items[i].command==ui::Command::pad_bind0)state_.selection=int(i);
            controller_tick({true,ui::pad_a},1004);
            require(state_.pad_binding==0,"runtime controller listening target");
            controller_tick({true,ui::pad_a},1005);require(state_.pad_binding==0,"runtime activation press excluded from capture");
            refresh();canvas_.write_bmp(options_.ui_smoke/"controller-listening.bmp");
            controller_tick({true},1006);controller_tick({true,0x8000},1007);
            require(config_.pad[0]==0x8000&&state_.pad_binding<0,"runtime controller binding captured and applied");
            controller_tick({true,ui::pad_a},1008);require(state_.pad_binding<0,"runtime rebind completion cannot activate again");
            controller_tick({true},1009);controller_tick({true,ui::pad_b},1010);
            require(state_.page==ui::Page::home,"runtime controller B returns Home");
            execute(ui::Command::controls);execute(ui::Command::defaults);execute(ui::Command::back);
            require(config_.pad==ui::default_pad_bindings,"runtime pad defaults restored");
            // Exercise page routing before a real session exists; the smoke's
            // machine/audio lanes must stay paused and silent throughout.
            state_.session=true;state_.page=ui::Page::game;
            controller_tick({true},1100);controller_tick({true,ui::pad_menu},1101);
            require(state_.page==ui::Page::pause&&ui::should_pause(state_,config_),"R3 opens pause through runtime dispatch");
            controller_tick({true,ui::pad_menu},1102);
            require(state_.page==ui::Page::pause,"R3 hold does not resume");
            controller_tick({true},1103);controller_tick({true,ui::pad_menu},1104);
            require(state_.page==ui::Page::game&&!ui::should_pause(state_,config_),"R3 resumes through runtime dispatch");
            controller_tick({true,ui::pad_menu},1105);
            require(state_.page==ui::Page::game,"R3 hold does not reopen pause");
            state_.session=false;state_.page=ui::Page::home;state_.focused=focused;
            config_.visual_mode=1; // Exercise install-time legacy normalization.
            std::cout<<"UI_TWO_MODE_CONTROLLER_MENU=PASS\n";
            std::cout<<"UI_CONTROLLER_NAV_REBIND=PASS\n";
            auto bytes=synthetic_nrom();bytes[6]|=2;std::ofstream out(rom,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();
            require(bool(out),"synthetic smoke fixture write");open(rom);++smoke_stage_;
        }else if(smoke_stage_==1&&!state_.loading){
            require(session_&&state_.error.empty()&&session_->paused.load(),"staged Unicode session opens paused");
            require(config_.visual_mode==0,"open-ROM install normalizes legacy mode");
            smoke_session_=session_.get();
            const bool focused=state_.focused;state_.focused=true;
            controller_tick({true},1200);controller_tick({true,2},1201);
            require(layout_.items[std::size_t(state_.selection)].command==ui::Command::home,"controller navigates to Home");
            controller_tick({true,ui::pad_a},1202);refresh();
            require(state_.page==ui::Page::home&&session_.get()==smoke_session_&&session_->paused.load(),"Home retains a paused session");
            require(!game_visible_&&!(GetWindowLongPtrW(game_,GWL_STYLE)&WS_VISIBLE)&&
                !(GetWindowLongPtrW(stats_,GWL_STYLE)&WS_VISIBLE),"Home hides playfield and stats children");
            canvas_.write_bmp(options_.ui_smoke/"session-home.bmp");
            execute(ui::Command::settings);execute(ui::Command::controls);key(VK_ESCAPE);
            require(state_.page==ui::Page::home,"Home settings return to Home with a live session");
            state_.focused=false;execute(ui::Command::resume);
            require(state_.page==ui::Page::game&&session_.get()==smoke_session_&&session_->paused.load(),"Home Resume retains session and focus gate");
            key(VK_ESCAPE);state_.focused=focused;
            require(state_.page==ui::Page::pause&&session_->paused.load(),"Resume followed by Escape restores pause");
            std::cout<<"UI_PAUSE_MENU_HOME=PASS\n";
            for(unsigned k=VK_F1;k<=VK_F4;++k){key(k);require(config_.visual_mode==(k==VK_F4?3u:0u),"runtime two-mode key routing");}
            key(VK_F2);key(VK_F3);require(config_.visual_mode==3,"runtime F2/F3 preserve Remaster");
            execute(ui::Command::controls);execute(ui::Command(unsigned(ui::Command::bind0)));key('Q');
            require(config_.keys[0]=='Q',"runtime key remap");execute(ui::Command::defaults);
            require(config_.keys==ui::default_bindings,"runtime restore default controls");
            execute(ui::Command::audio);execute(ui::Command::volume_down);execute(ui::Command::mute);
            require(config_.volume==95&&config_.mute,"runtime functional output settings");
            execute(ui::Command::controls);execute(ui::Command::gamepad);
            require(!config_.gamepad_enabled,"runtime gamepad setting");
            execute(ui::Command::back);execute(ui::Command::save_state);
            ui::ProductStore store(options_.ui_smoke);Machine witness(read_rom(rom));std::string error;
            require(store.load_state(witness,1,error),"UI saves real machine snapshot");
            witness.ram[19]=91;witness.mapper->battery_ram()[27]=0xa5;witness.mapper->battery_ram()[85]=0;
            const auto expected=witness.serialize();store.save_state(witness,2);
            execute(ui::Command::slot_next);execute(ui::Command::load_state);
            execute(ui::Command::slot_next);execute(ui::Command::save_state);
            require(store.load_state(witness,3,error)&&witness.serialize()==expected,"UI loads and saves complete machine state");
            ui::atomic_product_write(store.state_path(witness,4),Bytes{1,2,3});
            execute(ui::Command::slot_next);execute(ui::Command::load_state);
            require(!state_.notice.empty()&&state_.error.empty(),"bad state gives nonmodal feedback");
            execute(ui::Command::slot_next);execute(ui::Command::save_state);
            require(store.load_state(witness,1,error)&&witness.serialize()==expected,"UI rejected load retains machine");
            RECT before{},after{};GetWindowRect(window_,&before);execute(ui::Command::fullscreen);execute(ui::Command::fullscreen);GetWindowRect(window_,&after);
            require(EqualRect(&before,&after)&&!full_.active,"native reversible fullscreen");
            std::ofstream bad(options_.ui_smoke/L"invalid.nes",std::ios::binary);bad<<"invalid ROM bytes";bad.close();
            execute(ui::Command::home);
            open(options_.ui_smoke/L"invalid.nes");++smoke_stage_;
        }else if(smoke_stage_==2&&!state_.loading){
            require(session_.get()==smoke_session_&&!state_.error.empty()&&session_->paused.load(),"invalid open preserves running session");
            require(state_.page==ui::Page::home,"invalid open from Home retains the front page");
            require(config_.recents.size()==1,"invalid ROM excluded from recents");
            execute(ui::Command::dismiss);execute(ui::Command::back);
            // Deliver the same Unicode HDROP packet used by the Windows shell.
            const auto path=rom.wstring();const auto size=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
            auto memory=GlobalAlloc(GHND,size);require(memory!=nullptr,"drop packet allocation");
            auto* drop=static_cast<DROPFILES*>(GlobalLock(memory));drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;
            std::copy(path.begin(),path.end(),reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(drop)+sizeof(DROPFILES)));
            config_.visual_mode=2;
            GlobalUnlock(memory);SendMessageW(game_,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0);++smoke_stage_;
        }else if(smoke_stage_==3&&!state_.loading){
            require(session_&&session_.get()!=smoke_session_&&state_.error.empty(),"valid shell drop replaces session");
            require(config_.visual_mode==0,"drop-ROM install normalizes legacy mode");
            execute(ui::Command::save_state);
            Machine witness(read_rom(rom));std::string error;ui::ProductStore store(options_.ui_smoke);
            require(store.load_state(witness,config_.state_slot,error)&&witness.mapper->battery_ram()[27]==0xa5,
                "same-ROM switch flushes and reloads battery before play");
            require(config_.recents.size()==1,"drop dedupes recents");smoke_session_=session_.get();
            execute(ui::Command::mode3);execute(ui::Command(unsigned(ui::Command::recent0)));++smoke_stage_;
        }else if(smoke_stage_==4&&!state_.loading){
            require(session_&&session_.get()!=smoke_session_&&state_.error.empty(),"recent path opens validated session");
            require(config_.visual_mode==3,"recent-ROM install preserves Remaster");
            require(session_->emu_frames.load()==0&&session_->paused.load(),"smoke sessions stay silent and paused");
            Machine witness(read_rom(rom));ui::ProductStore store(options_.ui_smoke);std::string error;
            execute(ui::Command::save_state);require(store.load_state(witness,config_.state_slot,error),"exit fixture snapshot");
            witness.mapper->battery_ram()[85]=0x6b;store.save_state(witness,config_.state_slot);execute(ui::Command::load_state);
            std::filesystem::remove(rom);std::filesystem::remove(options_.ui_smoke/L"invalid.nes");
            execute(ui::Command::quit);++smoke_stage_;
        }
    }
    void sync_pause(){if(session_)session_->paused.store(ui::should_pause(state_,config_));}
    void fullscreen(){
        if(!window_||full_.active==config_.fullscreen)return;
        RECT r{};GetWindowRect(window_,&r);MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor);
        if(config_.fullscreen){GetWindowPlacement(window_,&placement_);saved_style_=GetWindowLongPtrW(window_,GWL_STYLE);}
        auto target=full_.transition(config_.fullscreen,{r.left,r.top,r.right-r.left,r.bottom-r.top},
            {monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top});
        SetWindowLongPtrW(window_,GWL_STYLE,config_.fullscreen?(saved_style_&~WS_OVERLAPPEDWINDOW)|WS_POPUP:saved_style_);
        SetWindowPos(window_,nullptr,target.x,target.y,target.w,target.h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        if(!config_.fullscreen)SetWindowPlacement(window_,&placement_);
        dirty_=present_dirty_=true;
    }
    void open(const std::filesystem::path& requested){
        if(state_.loading)return;
        if(session_&&session_->locked_tape()){
            state_.error=L"This session is recording or replaying. Quit to finish the tape before opening another ROM.";dirty_=true;sync_pause();return;
        }
        try{
            auto normalized=ui::normalized_rom_path(requested.wstring());
            if(normalized.empty())normalized=ui::normalized_rom_path(std::filesystem::absolute(requested).wstring());
            if(normalized.empty())throw std::runtime_error("Choose a .nes file with a valid Windows path (up to 1024 characters).");
            const std::filesystem::path path(normalized);
            opening_path_=path;state_.loading=true;if(state_.session&&state_.page!=ui::Page::home)state_.page=ui::Page::pause;
            state_.error.clear();state_.binding=-1;dirty_=true;sync_pause();
            if(session_)session_->flush_battery(); // Paused, before a same-ROM candidate reads its save.
            Options candidate=options_;candidate.rom=path;
            loading_=std::async(std::launch::async,[this,candidate]{
                auto bytes=read_rom(candidate.rom);
                auto validation=load_cartridge(bytes);validation.reset();
                const auto rate=infer_sample_rate(bytes);
                return std::make_unique<Session>(std::move(bytes),rate,candidate,input_,window_);
            });
        }catch(const std::exception& e){state_.loading=false;state_.error=message(e);dirty_=true;sync_pause();}
    }
    void open_dialog(){
        if(state_.session&&state_.page!=ui::Page::home)state_.page=ui::Page::pause;sync_pause();dirty_=true;refresh();
        std::array<wchar_t,32768> name{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;
        dialog.lpstrFilter=L"NES ROM (*.nes)\0*.nes\0\0";dialog.lpstrFile=name.data();dialog.nMaxFile=DWORD(name.size());
        dialog.lpstrTitle=L"Open ROM";dialog.lpstrDefExt=L"nes";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(GetOpenFileNameW(&dialog))open(name.data());
        else if(CommDlgExtendedError()){state_.error=L"Windows could not open the file picker. You can also drop a .nes file onto the window.";dirty_=true;}
        state_.focused=GetForegroundWindow()==window_;
    }
    void execute(ui::Command command){
        const auto previous_mode=config_.visual_mode;
        auto effect=ui::route(state_,config_,command);dirty_=present_dirty_=true;sync_pause();
        if(config_.visual_mode!=previous_mode){state_.visual_notice=std::wstring(L"Visual: ")+ui::visual_name(config_.visual_mode);visual_notice_until_=GetTickCount64()+2200;}
        if(effect.fullscreen)fullscreen();if(effect.save)persist();
        if((effect.save_state||effect.load_state)&&session_){
            state_.page=ui::Page::pause;sync_pause();
            try{
                session_->state_action(effect.save_state,config_.state_slot);
                state_.notice=(effect.save_state?L"Saved state in slot ":L"Loaded state from slot ")+std::to_wstring(config_.state_slot)+L".";
            }catch(const std::exception& e){state_.notice=message(e);}
        }
        if(effect.reset&&session_)session_->reset_requested.store(true);
        if(effect.quit)running_=false;
        if(effect.recent>=0)open(config_.recents[std::size_t(effect.recent)]);
        if(effect.open)open_dialog();
    }
    void key(unsigned k){
        if(k==VK_ESCAPE){ui::escape(state_);controller_.cancel(state_);dirty_=true;sync_pause();return;}
        if(int mode=ui::shortcut_mode(k);mode>=0){controller_.cancel(state_);state_.binding=-1;execute(ui::Command(unsigned(ui::Command::mode0)+unsigned(mode)));return;}
        if(state_.pad_binding>=0)return;
        if(state_.binding>=0){
            if(ui::bind_key(config_.keys,unsigned(state_.binding),k)){state_.binding=-1;state_.notice=L"Key binding saved.";persist();}
            else state_.notice=L"That key is reserved or already in use. Choose another key.";
            dirty_=true;return;
        }
        if(state_.loading)return;
        if(k=='P'&&state_.session){ui::escape(state_);dirty_=true;sync_pause();return;}
        if(k==VK_F8&&state_.session){execute(ui::Command::reset);return;}
        if(k==VK_TAB||((k==VK_DOWN||k==VK_UP)&&state_.page!=ui::Page::game)){
            ui::move_selection(state_,layout_,k==VK_UP||(k==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000))?-1:1);dirty_=true;
        }else if((k==VK_LEFT||k==VK_RIGHT)&&state_.page!=ui::Page::game){
            ui::navigate(state_,layout_,k==VK_LEFT?ui::Direction::left:ui::Direction::right);dirty_=true;
        }else if((k==VK_RETURN||k==VK_SPACE)&&state_.page!=ui::Page::game&&state_.selection>=0&&unsigned(state_.selection)<layout_.count){execute(layout_.items[std::size_t(state_.selection)].command);}
    }
    void refresh(){
        if(!window_||state_.minimized)return;
        RECT rc{};GetClientRect(window_,&rc);if(rc.right<1||rc.bottom<1)return;
        layout_=ui::layout(state_,config_,rc.right,rc.bottom,window_dpi(window_));
        if(state_.selection>=int(layout_.count))state_.selection=0;
        bool show=state_.session&&state_.page==ui::Page::game&&!state_.loading&&state_.error.empty();
        auto v=ui::game_viewport(rc.right,rc.bottom,config_.integer_scaling,layout_.scale);
        if(game_){
            SetWindowPos(game_,nullptr,v.x,v.y,v.w,v.h,SWP_NOZORDER|SWP_NOACTIVATE);
            if(show!=game_visible_){ShowWindow(game_,show?SW_SHOWNOACTIVATE:SW_HIDE);present_dirty_=true;}
            game_visible_=show;
        }
        if(stats_){
            SetWindowPos(stats_,HWND_TOP,v.x+8,v.y+8,std::max(1,v.w-16),30,SWP_NOACTIVATE);
            ShowWindow(stats_,show&&config_.stats?SW_SHOWNOACTIVATE:SW_HIDE);
        }
        // When running, the child owns the frozen image and the CPU canvas only
        // paints the shell. When paused, it composites the last complete image.
        canvas_.draw(state_,config_,layout_,rc.right,rc.bottom,show?std::span<const std::uint8_t>{}:std::span<const std::uint8_t>{frame_});
        canvas_ready_=true;dirty_=false;InvalidateRect(window_,nullptr,FALSE);UpdateWindow(window_);
    }
};
LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(application)try{return application->event(hwnd,msg,wp,lp);}catch(const std::exception& e){
        MessageBoxA(hwnd,e.what(),"VKRT NES",MB_OK|MB_ICONERROR);PostMessageW(hwnd,WM_CLOSE,0,0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK game_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT&&application){application->paint_game(hwnd);return 0;}
    if(msg==WM_LBUTTONDOWN&&application){SetFocus(application->window());return 0;}
    if(msg==WM_DROPFILES&&application)return SendMessageW(application->window(),msg,wp,lp);
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK stats_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT&&application){application->paint_stats(hwnd);return 0;}
    if(msg==WM_DROPFILES&&application)return SendMessageW(application->window(),msg,wp,lp);
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int entry(){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 1;
    struct Args {wchar_t** value;~Args(){LocalFree(value);}} args{argv};
    const bool headless=argc>=2&&(std::wstring_view(argv[1])==L"--polish-probe"||std::wstring_view(argv[1])==L"--product-probe"||std::wstring_view(argv[1])==L"--probe"||std::wstring_view(argv[1])==L"--stress-sim"||std::wstring_view(argv[1])==L"--ui-probe"||std::wstring_view(argv[1])==L"--ui-smoke");
    try{
        if(argc==4&&std::wstring_view(argv[1])==L"--polish-probe")return polish_probe(argv[2],argv[3]);
        if(argc==4&&std::wstring_view(argv[1])==L"--product-probe")return ui::product_probe(argv[2],argv[3]);
        if(argc==3&&std::wstring_view(argv[1])==L"--ui-probe"){ui::ui_probe(argv[2]);std::cout<<"UI_PROBE=PASS\n";return 0;}
        if(argc==3&&std::wstring_view(argv[1])==L"--ui-smoke"){Options o;o.ui_smoke=std::filesystem::absolute(argv[2]);Application app(o);return app.run();}
        if(argc==3&&std::wstring_view(argv[1])==L"--probe")return probe(argv[2]);
        if(argc==3&&std::wstring_view(argv[1])==L"--stress-sim")return stress(argv[2]);
        Application app(parse(argc,argv));return app.run();
    }catch(const std::exception& e){
        std::cerr<<"RUNTIME_FAIL: "<<e.what()<<"\n";
        if(!headless)MessageBoxA(nullptr,e.what(),"VKRT NES",MB_OK|MB_ICONERROR);return 1;
    }
}
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){return entry();}
