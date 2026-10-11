#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <frontend_product.hpp>
#include <frontend_ui.hpp>
#include <runtime_audio.hpp>
#include "vkrt/nes_native_s0/mmc3.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

using namespace vkrt::nes;
namespace ui=vkrt::nes::frontend;
namespace host=vkrt::nes::host;
namespace {
std::uint64_t checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;if(failures<=20)std::cerr<<name<<'\n';}}
template<class F> void rejects(F f,const char* name){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,name);}
void put(Bytes& bytes,std::size_t offset,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes[offset+i]=std::uint8_t(value>>(8*i));}
void checksum(Bytes& bytes){put(bytes,32,bytes.size()-44,4);put(bytes,36,identity_hash(std::span<const std::uint8_t>(bytes).subspan(44)),8);}
Bytes fixture(unsigned mapper,bool chr_ram=false){
    const auto base=synthetic_nrom();Bytes rom(16+32768+(chr_ram?0:8192));
    std::copy_n(base.begin(),16,rom.begin());rom[4]=2;rom[5]=chr_ram?0:1;rom[6]=std::uint8_t((mapper<<4)|3);
    std::copy_n(base.begin()+16,16384,rom.begin()+16);std::copy_n(base.begin()+16,16384,rom.begin()+16+16384);
    if(!chr_ram)std::copy_n(base.end()-8192,8192,rom.end()-8192);
    return rom;
}
void compare_frames(const Frame& a,const Frame& b){
    check(frame_hash(a)==frame_hash(b),"frame digest continuation");
    for(unsigned i=0;i<pixels;++i){
        check(a.indices[i]==b.indices[i],"continued pixel");check(a.effects[i]==b.effects[i],"continued effects");
        check(a.samples[i].background==b.samples[i].background&&a.samples[i].sprite==b.samples[i].sprite,"continued fetched provenance");
    }
    check(bool(a.native)==bool(b.native),"native presence");
    if(a.native&&b.native){
        check(a.native->palette==b.native->palette&&a.native->oam==b.native->oam,"native palette OAM");
        check(a.native->spans.size()==b.native->spans.size(),"native span count");
        for(std::size_t i=0;i<std::min(a.native->spans.size(),b.native->spans.size());++i){
            const auto& x=a.native->spans[i];const auto& y=b.native->spans[i];
            check(x.begin==y.begin&&x.count==y.count&&x.shape==y.shape&&x.source==y.source&&x.layer==y.layer&&x.unstable==y.unstable,"native span continuation");
        }
    }
}
void states(){
    for(auto [board,chr_ram]:std::array<std::pair<unsigned,bool>,9>{{{0,false},{0,true},{1,false},{1,true},{2,true},{3,false},{4,false},{4,true},{7,true}}}){
        auto rom=fixture(board,chr_ram);
        Machine uninterrupted(rom),restored(rom);std::string error;
        for(unsigned round=0;round<3;++round){
            uninterrupted.run_frames(1);uninterrupted.apu.pcm.clear();
            const unsigned instructions=round==0?43:round==1?3000:8000;
            for(unsigned i=0;i<instructions;++i)uninterrupted.step();
            if(round)check(uninterrupted.ppu.scanline<240,"snapshot during visible scanline");
            uninterrupted.controllers[0].buttons=std::uint8_t(0x81+round);
            uninterrupted.controllers[0].write(1);uninterrupted.controllers[0].write(0);uninterrupted.controllers[0].read();
            for(unsigned i=0;i<8192;++i)uninterrupted.mapper->cpu_write(std::uint16_t(0x6000+i),std::uint8_t(i*7+round));
            if(board==1){ // In-flight MMC1 shift and consecutive-write history.
                for(unsigned bit=0;bit<5;++bit)uninterrupted.mapper->cpu_write(0x8000,std::uint8_t(31>>bit)&1);
                for(unsigned bit=0;bit<5;++bit)uninterrupted.mapper->cpu_write(0xa000,std::uint8_t(1>>bit)&1);
                uninterrupted.mapper->cpu_write_cycle(0xa000,1,uninterrupted.cycles-2);
                uninterrupted.mapper->cpu_write_cycle(0xa000,0,uninterrupted.cycles-1);
            }
            if(board==4){
                uninterrupted.mapper->cpu_write(0x8000,2);uninterrupted.mapper->cpu_write(0x8001,3);
                uninterrupted.mapper->cpu_write(0x8000,6);uninterrupted.mapper->cpu_write(0x8001,2);
                uninterrupted.mapper->cpu_write(0xc000,3);uninterrupted.mapper->cpu_write(0xe001,0);
                uninterrupted.mapper->ppu_fetch(0,0,FetchKind::dummy);uninterrupted.mapper->m2_falling();
            }
            if(board==2||board==3||board==7)uninterrupted.mapper->cpu_write(0x8000,0x11);
            if(chr_ram)for(unsigned i=0;i<8192;i+=7)uninterrupted.mapper->ppu_write(std::uint16_t(i),std::uint8_t(i+round));
            // Pending OAM and DMC DMA plus active IRQ/sound generation.
            uninterrupted.write(0x4010,0x8f);uninterrupted.write(0x4012,0);uninterrupted.write(0x4013,2);uninterrupted.write(0x4015,31);
            uninterrupted.write(0x4014,2);
            const auto state=uninterrupted.save_state();
            const auto canonical=uninterrupted.serialize();
            check(restored.load_state(state,error),"state load all supported boards");
            check(restored.serialize()==canonical,"canonical explicit round trip");
            check(restored.save_state()==state,"complete state round trip including partial frame");
            check(restored.rom_identity()==uninterrupted.rom_identity(),"stable identity");
            for(unsigned frame=0;frame<2;++frame){
                uninterrupted.controllers[0].buttons=restored.controllers[0].buttons=std::uint8_t(frame*47+round);
                uninterrupted.run_frames(1);restored.run_frames(1);
                check(uninterrupted.serialize()==restored.serialize(),"repeated canonical continuation");
                compare_frames(*uninterrupted.frame(),*restored.frame());
                check(uninterrupted.apu.pcm.size()==restored.apu.pcm.size(),"continued PCM count");
                for(std::size_t i=0;i<std::min(uninterrupted.apu.pcm.size(),restored.apu.pcm.size());++i)
                    check(uninterrupted.apu.pcm[i]==restored.apu.pcm[i],"continued PCM sample");
                uninterrupted.apu.pcm.clear();restored.apu.pcm.clear();
            }
            check(restored.save_state()==uninterrupted.save_state(),"continued complete snapshot");
        }
    }
    // A distinctive unused PRG sequence must never be embedded in a state file.
    auto rom=fixture(0);Bytes marker(256);for(unsigned i=0;i<marker.size();++i)marker[i]=std::uint8_t(i*73+9);
    std::copy(marker.begin(),marker.end(),rom.begin()+16+0x1600);
    Machine m(rom);m.run_frames(2);auto good=m.save_state();const auto before=m.serialize();std::string error;
    check(std::search(good.begin(),good.end(),marker.begin(),marker.end())==good.end(),"immutable PRG payload omitted");
    auto reject_unchanged=[&](std::span<const std::uint8_t> bytes){
        const auto frame=m.frame();const auto cycles=m.cycles;
        check(!m.load_state(bytes,error)&&!error.empty(),"malformed state rejected");
        check(m.cycles==cycles&&m.frame()==frame,"rejection retains active machine and published frame");
    };
    for(unsigned offset:{0u,8u,12u,20u,22u,23u,24u,28u,32u,36u,44u,129u}){
        auto bad=good;bad[offset]^=0x55;reject_unchanged(bad);check(m.serialize()==before,"rejection canonical state unchanged");
    }
    auto other_rom=rom;other_rom[17]^=1;Machine other(other_rom);
    check(!other.load_state(good,error),"different ROM rejected");
    for(std::size_t cut=0;cut<good.size();cut+=4093)reject_unchanged(std::span<const std::uint8_t>(good).first(cut));
    for(std::size_t cut=0;cut<128;++cut)reject_unchanged(std::span<const std::uint8_t>(good).first(cut));
    reject_unchanged(std::span<const std::uint8_t>(good).first(good.size()-1));
    // Valid checksum and lengths force the decoder to encounter missing fields.
    for(std::size_t cut:{44u,45u,64u,512u,8192u,32768u,65536u}){
        Bytes bad(good.begin(),good.begin()+cut);checksum(bad);reject_unchanged(bad);
    }
    auto bad=good;bad[44+7]=2;checksum(bad);reject_unchanged(bad); // Invalid boolean, even with valid integrity.
    bad=good;bad.push_back(0);checksum(bad);reject_unchanged(bad); // Trailing field rejected.
    Bytes huge(max_state_bytes+1);reject_unchanged(huge);
    check(m.serialize()==before&&m.save_state()==good,"all failures leave complete active machine unchanged");
    // No frame yet; partial power-on pipeline and warm reset snapshots.
    Machine fresh(rom);auto initial=fresh.save_state();fresh.run_frames(1);
    check(fresh.load_state(initial,error)&&!fresh.frame(),"pre-first-frame restore");
    fresh.reset();auto reset=fresh.save_state();check(fresh.load_state(reset,error)&&fresh.save_state()==reset,"warm reset restore");
}
void storage(const std::filesystem::path& root){
    ui::ProductStore store(root);std::string error;
    for(unsigned board:{0u,1u,2u,3u,4u,7u}){
        Machine m(fixture(board,board==2||board==7));auto ram=m.mapper->battery_ram();
        for(unsigned i=0;i<ram.size();++i)ram[i]=std::uint8_t(i*29+board);
        const auto bytes=m.save_battery();store.save_battery(m);
        check(ui::read_product_file(store.battery_path(m),8236)==bytes,"SRAM exact disk bytes");
        std::fill(ram.begin(),ram.end(),0);check(store.load_battery(m,error),"SRAM exact load");
        for(unsigned i=0;i<ram.size();++i)check(ram[i]==std::uint8_t(i*29+board),"SRAM exact byte");
        for(std::size_t cut=0;cut<bytes.size();cut+=13){
            check(!m.load_battery(std::span<const std::uint8_t>(bytes).first(cut),error),"truncated SRAM rejected");
        }
        auto corrupt=bytes;corrupt[2048]^=1;check(!m.load_battery(corrupt,error)&&m.save_battery()==bytes,"SRAM checksum failure retains RAM");
        ui::atomic_product_write(store.battery_path(m),corrupt);
        check(!store.load_battery(m,error)&&m.save_battery()==bytes,"disk corruption retains RAM");
        store.save_battery(m);
        auto path=store.battery_path(m);auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        {std::ofstream f(temp);f<<"occupied";}
        ram[1]^=0xff;rejects([&]{store.save_battery(m);},"exclusive temporary collision");
        check(ui::read_product_file(path,8236)==bytes,"collision retains prior complete save");
        check(ui::read_product_file(temp,32)==Bytes({'o','c','c','u','p','i','e','d'}),"collision retains someone else's temporary file");
        std::filesystem::remove(temp);store.save_battery(m);
        check(!std::filesystem::exists(temp)&&ui::read_product_file(path,8236)==m.save_battery(),"atomic replace and temporary cleanup");
    }
    Machine m(fixture(0));m.run_frames(1);
    std::array<Bytes,4> states;
    for(unsigned slot=1;slot<=4;++slot){m.ram[12]=std::uint8_t(slot);states[slot-1]=m.save_state();store.save_state(m,slot);}
    for(unsigned slot=1;slot<=4;++slot){check(store.load_state(m,slot,error)&&m.ram[12]==slot,"numbered slots independent");check(m.save_state()==states[slot-1],"slot exact round trip");}
    rejects([&]{store.save_state(m,0);},"slot zero rejected");rejects([&]{store.save_state(m,5);},"slot five rejected");
    check(!ui::ProductStore(root/L"missing").load_state(m,1,error),"missing slot safe");
    Machine no_battery(synthetic_nrom());check(!no_battery.has_battery()&&no_battery.save_battery().empty(),"nonbattery has no save");
    store.save_battery(no_battery);check(!std::filesystem::exists(store.battery_path(no_battery)),"nonbattery creates no file");
    // Replacement failure after a successful temp write cleans only its own temp.
    const auto blocked=root/L"directory-target";std::filesystem::create_directory(blocked);
    rejects([&]{ui::atomic_product_write(blocked,Bytes{1,2,3});},"replace directory fails safely");
    auto temp=blocked;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    check(std::filesystem::is_directory(blocked)&&!std::filesystem::exists(temp),"failed replace retains target and removes owned temp");
}
std::int64_t pcm(std::span<const std::uint8_t> bytes,unsigned bits){
    std::uint32_t value=0;for(unsigned i=0;i<bits/8;++i)value|=std::uint32_t(bytes[i])<<(8*i);
    return (value&(std::uint32_t(1)<<(bits-1)))?std::int64_t(value)-(std::int64_t(1)<<bits):value;
}
void volume(){
    for(unsigned bits:{16u,24u,32u})for(unsigned percent:{0u,25u,50u,100u}){
        runtime::EndpointFormat format;format.encoding=runtime::EndpointEncoding::pcm;format.bits=format.valid_bits=bits;
        host::OutputGain gain;gain.set(percent,false);std::array<std::uint8_t,8> out{};
        for(unsigned i=0;i<1000;++i)gain.apply(out,1,format);
        check(gain.current()==percent/100.,"gain settles to exact percentage");
        for(int sample=-32768;sample<=32767;++sample){
            const auto raw=std::int64_t(sample)*(std::int64_t(1)<<(bits-16));
            for(unsigned channel=0;channel<2;++channel)for(unsigned i=0;i<bits/8;++i)out.at(channel*(bits/8)+i)=std::uint8_t(std::uint64_t(raw)>>(8*i));
            gain.apply(out,1,format);
            const auto expected=std::int64_t(double(raw)*(percent/100.));
            check(pcm(out,bits)==expected,"exhaustive output gain PCM");
            check(pcm(std::span<const std::uint8_t>(out).subspan(bits/8),bits)==expected,"identical channel gain");
        }
        gain.set(100,true);for(unsigned i=0;i<1000;++i){out.fill(0x7f);gain.apply(out,1,format);}
        check(pcm(out,bits)==0&&gain.current()==0,"mute settles to silence");
    }
    for(unsigned rate:{32000u,44100u,48000u,96000u,192000u}){
        runtime::EndpointFormat f;f.rate=rate;host::OutputGain gain;std::array<std::uint8_t,8> bytes{};
        for(unsigned i=0;i<20000;++i){
            const unsigned target=(i/137)%101;gain.set(target,false);const auto previous=gain.current();
            float sample=i%5==0?std::numeric_limits<float>::quiet_NaN():i%5==1?std::numeric_limits<float>::infinity():.75f;
            std::memcpy(bytes.data(),&sample,4);std::memcpy(bytes.data()+4,&sample,4);
            gain.apply(bytes,1,f);std::memcpy(&sample,bytes.data(),4);
            check(std::isfinite(sample)&&std::abs(sample)<=1,"finite bounded float output");
            check(gain.current()>=0&&gain.current()<=1,"bounded smoothed gain");
            check(std::abs(previous-gain.current())<=1./(rate*.005)+1e-12,"maximum per-sample slew");
            check(gain.current()>=std::min(previous,target/100.)&&gain.current()<=std::max(previous,target/100.),"ramp has no overshoot");
        }
        gain.set(std::numeric_limits<unsigned>::max(),false);for(unsigned i=0;i<2000;++i)gain.apply(bytes,1,f);
        check(gain.current()==1,"volume clamp");
    }
    runtime::EndpointFormat f;host::OutputGain gain;gain.set(0,false);Bytes sentinel(7,0x55);
    gain.apply(sentinel,1,f);check(sentinel==Bytes(7,0x55),"short output buffer untouched");
    for(unsigned bits:{24u,32u})for(unsigned valid:{16u,20u,24u})if(valid<=bits){
        runtime::EndpointFormat format;format.bits=bits;format.valid_bits=valid;format.encoding=runtime::EndpointEncoding::pcm;
        host::OutputGain level;level.set(25,false);Bytes bytes(format.frame_bytes());
        for(unsigned i=0;i<1000;++i)level.apply(bytes,1,format);
        const auto padding=(std::uint32_t(1)<<(bits-valid))-1;
        for(int value=-32768;value<=32767;value+=31){
            runtime::encode_endpoint(value/32768.f,format,bytes);
            const auto before=pcm(bytes,bits);level.apply(bytes,1,format);
            check((std::uint32_t(pcm(bytes,bits))&padding)==0,"padded PCM stays left aligned");
            check(std::abs(double(pcm(bytes,bits))-before*.25)<=padding+1.,"padded PCM gain quantization bound");
        }
    }
    // Muting the final bytes cannot alter resampling, FIFO occupancy or DRC.
    runtime::AudioStream audible(44100),muted(44100);audible.configure(f,480);muted.configure(f,480);
    std::array<std::int16_t,735> input{};for(unsigned i=0;i<input.size();++i)input[i]=std::int16_t(i*83%32768);
    Bytes left(480*f.frame_bytes()),right(left.size());host::OutputGain silence;silence.set(100,true);
    for(unsigned tick=0;tick<120;++tick){
        audible.submit(input);muted.submit(input);audible.render(left,480);muted.render(right,480);silence.apply(right,480,f);
        check(audible.occupancy()==muted.occupancy()&&audible.correction_ppm==muted.correction_ppm,"mute leaves cadence and DRC unchanged");
        check(audible.underruns==muted.underruns&&audible.dropped==muted.dropped,"mute leaves FIFO accounting unchanged");
        check(std::all_of(right.begin(),right.end(),[](auto b){return b==0;}),"muted output buffer silent while stream consumes");
    }
}
void input_and_config(const std::filesystem::path& root){
    for(unsigned mask=0;mask<65536;++mask){
        unsigned expected=(mask&0x1000?1:0)|(mask&0x6000?2:0)|(mask&0x20?4:0)|(mask&0x10?8:0)|
            (mask&1?16:0)|(mask&2?32:0)|(mask&4?64:0)|(mask&8?128:0);
        if((expected&48)==48)expected&=~48u;if((expected&192)==192)expected&=~192u;
        check(ui::pad_buttons(mask,0,0)==expected,"XInput complete button truth table");
        ui::PadModel pad;pad.update(true,true,mask,0,0);check(pad.buttons()==expected&&pad.connected(),"connect model");
        pad.update(true,false,mask,32767,-32768);check(pad.buttons()==0&&!pad.connected(),"disconnect clears stale controls");
        pad.update(true,true,mask,0,0);check(pad.buttons()==expected&&pad.connected(),"reconnect model");
        pad.update(false,true,mask,0,0);check(pad.buttons()==0&&!pad.connected(),"disabled pad model");
    }
    for(int x:{-32768,-16001,-16000,-1,0,1,16000,16001,32767})for(int y:{-32768,-16001,-16000,-1,0,1,16000,16001,32767})
        check(ui::pad_buttons(0,x,y)==((x<-16000?64:x>16000?128:0)|(y<-16000?32:y>16000?16:0)),"stick threshold boundaries");
    for(unsigned keyboard=0;keyboard<256;++keyboard)for(unsigned controller=0;controller<256;++controller){
        auto expected=keyboard|controller;if((expected&48)==48)expected&=~48u;if((expected&192)==192)expected&=~192u;
        check(ui::combine_buttons(std::uint8_t(keyboard),std::uint8_t(controller))==expected,"combined controls OR then neutral opposites");
    }
    for(unsigned v=0;v<=100;++v)for(unsigned flags=0;flags<16;++flags){
        ui::Config c;c.volume=v;c.mute=flags&1;c.gamepad_enabled=flags&2;c.state_slot=1+flags/4;
        c.visual_mode=flags%2?3:0;ui::remember(c,L"C:\\library\\\u65e5\u672c\u8a9e.nes");
        const auto text=ui::serialize_config(c);check(ui::parse_config(text)==c,"all product preferences round trip");
        check(text.find("NES\x1a")==std::string::npos&&text.find("rom_contents")==std::string::npos,"config contains no ROM payload");
    }
    check(ui::parse_config("version=1\nend=1\n")==ui::Config{},"legacy config gains safe defaults");
    auto malformed=ui::parse_config("version=1\nvolume=999\nmute=-1\ngamepad=9\nstate_slot=99\nend=1\n");
    check(malformed==ui::Config{},"malformed product values safe");
    ui::Config c;c.volume=25;c.mute=true;c.gamepad_enabled=false;c.state_slot=3;std::wstring error;
    check(ui::save_config(root/L"\u8a2d\u5b9a.ini",c,error)&&ui::load_config(root/L"\u8a2d\u5b9a.ini",error)==c,"Unicode product config disk roundtrip");
    ui::State s;s.session=true;s.page=ui::Page::pause;
    check(ui::route(s,c,ui::Command::save_state).save_state&&ui::route(s,c,ui::Command::load_state).load_state,"real state command routes");
    s.tape_locked=true;check(!ui::route(s,c,ui::Command::load_state).load_state,"tape load denied");s.tape_locked=false;
    for(unsigned i=0;i<4;++i){ui::route(s,c,ui::Command::slot_next);check(c.state_slot>=1&&c.state_slot<=4,"compact slot wraps");}
    c.volume=0;ui::route(s,c,ui::Command::volume_down);check(c.volume==0,"volume lower UI bound");
    c.volume=100;ui::route(s,c,ui::Command::volume_up);check(c.volume==100,"volume upper UI bound");
    for(unsigned mode=0;mode<4;++mode){
        const bool allowed=mode==0||mode==3;const auto previous=c.visual_mode;
        check(ui::shortcut_mode(112+mode)==(allowed?int(mode):-1),"product F1/F4 shortcuts");
        ui::route(s,c,ui::Command(unsigned(ui::Command::mode0)+mode));
        check(c.visual_mode==(allowed?mode:previous),"product video route");
    }
    check(ui::route(s,c,ui::Command::open).open,"legacy open");check(ui::route(s,c,ui::Command::reset).reset,"legacy reset");
    check(ui::route(s,c,ui::Command::quit).quit,"legacy quit");ui::route(s,c,ui::Command::settings);check(s.page==ui::Page::video,"legacy settings");
    ui::route(s,c,ui::Command::controls);check(s.page==ui::Page::controls,"legacy controls");
    ui::Canvas canvas;
    for(auto page:{ui::Page::audio,ui::Page::controls,ui::Page::pause}){
        s.page=page;s.notice=L"Saved state in slot 3.";auto layout=ui::layout(s,c,1100,800,96);
        canvas.draw(s,c,layout,1100,800);canvas.write_bmp(root/(page==ui::Page::audio?L"audio.bmp":page==ui::Page::controls?L"controls.bmp":L"pause.bmp"));
    }
}
}
int main(){
    try{
        const auto parent=std::filesystem::temp_directory_path();
        const auto root=parent/(L"vkrt-m5r1-\u65e5\u672c\u8a9e-"+std::to_wstring(GetCurrentProcessId()));
        if(!root.is_absolute()||!std::filesystem::equivalent(root.parent_path(),parent)||!std::filesystem::create_directory(root))throw std::runtime_error("test root must be new and inside temp");
        states();storage(root);volume();input_and_config(root);
        // Preserve reviewable screenshots in this isolated test root. No ROM files.
    }catch(const std::exception& e){check(false,e.what());}
    check(checks>=1000000,"meaningful product check floor");
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
