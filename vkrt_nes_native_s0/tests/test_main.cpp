#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
using namespace vkrt::nes;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char*name){++checks;if(!ok){++failures;if(failures<30)std::cerr<<"FAIL "<<name<<'\n';}}
template<class F>void rejects(F f,const char*name){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,name);}
Frame blank(){Frame f;f.number=1;for(int i=0;i<pixels;++i){f.samples[i].x=i%width;f.samples[i].y=i/width;f.samples[i].dot=i%width+1;}return f;}
void color(Frame&f,int x,int y,std::uint8_t c){auto i=y*width+x;f.indices[i]=c;f.samples[i].color=c;}
void cartridge(){
    auto rom=synthetic_nrom();auto c=inspect_ines(rom);check(c.mapper==0&&c.prg_size==16384&&c.chr_size==8192,"valid generated iNES");
    auto m=load_cartridge(rom);check(m->cpu_read(0x8000)==0x78&&m->cpu_read(0xc000)==0x78,"NROM-128 PRG mirror");
    check(m->ppu_read(16)==255,"CHR ROM mapping");m->ppu_write(16,0);check(m->ppu_read(16)==255,"CHR ROM write protection");
    auto alternate=rom;alternate.back()^=1;check(load_cartridge(alternate)->chr_source()!=m->chr_source(),"cartridge domains distinguish CHR origins");
    m->cpu_write(0x6000,0xa7);check(m->cpu_read(0x6000)==0xa7,"NROM PRG RAM");m->cpu_write(0x8000,0);check(m->cpu_read(0x8000)==0x78,"PRG ROM immutable");
    auto wide=rom;wide[4]=2;wide.insert(wide.begin()+16+16384,16384,0x55);auto n=load_cartridge(wide);check(n->cpu_read(0xc000)==0x55,"NROM-256 distinct banks");
    auto chr=rom;chr[5]=0;chr.resize(16+16384);auto ram=load_cartridge(chr);ram->ppu_write(8191,91);check(ram->ppu_read(8191)==91&&ram->chr_epoch()==1,"CHR RAM epoch");ram->ppu_write(8191,91);check(ram->chr_epoch()==1,"identical CHR write stable epoch");
    auto trainer=rom;trainer[6]|=4;trainer.insert(trainer.begin()+16,512,0xa5);auto trained=load_cartridge(trainer);check(trained->cpu_read(0x7000)==0xa5,"trainer mapped to 7000");
    auto nes2=rom;nes2[7]=8;check(inspect_ines(nes2).nes2,"NES2 detection");check(load_cartridge(nes2)->cpu_read(0x8000)==0x78,"bounded NES2 NROM subset");
    nes2[9]=15;rejects([&]{inspect_ines(nes2);},"NES2 exponent bound");
    for(std::size_t size:{std::size_t(0),std::size_t(3),std::size_t(15),std::size_t(16),rom.size()-1})rejects([&]{inspect_ines(std::span(rom).first(size));},"truncated iNES");
    auto bad=rom;bad[0]=0;rejects([&]{load_cartridge(bad);},"bad magic");bad=rom;bad.push_back(0);rejects([&]{inspect_ines(bad);},"trailing bytes");
    bad=rom;bad[6]|=0x50;rejects([&]{load_cartridge(bad);},"unsupported mapper (5 remains outside the baseline)");bad=rom;bad[7]=4;rejects([&]{inspect_ines(bad);},"reserved format");
    bad=rom;bad[4]=0;rejects([&]{inspect_ines(bad);},"zero PRG");bad=rom;bad[12]=1;rejects([&]{inspect_ines(bad);},"dirty reserved header");
    for(auto mirror:{Mirroring::horizontal,Mirroring::vertical,Mirroring::four_screen}){
        auto r=rom;r[6]=mirror==Mirroring::horizontal?0:(mirror==Mirroring::vertical?1:8);auto cart=load_cartridge(r);Ppu p;
        p.write_memory(*cart,0x2000,11);p.write_memory(*cart,0x2400,22);p.write_memory(*cart,0x2800,33);p.write_memory(*cart,0x2c00,44);
        check(p.read_memory(*cart,0x2000)==(mirror==Mirroring::horizontal?22:(mirror==Mirroring::vertical?33:11)),"nametable mirroring");
        check(p.read_memory(*cart,0x3000)==p.read_memory(*cart,0x2000),"3000 nametable mirror");
    }
}
void cpu(){
    Machine m(synthetic_nrom());m.apu.irq_inhibit=true;
    auto program=[&](std::initializer_list<std::uint8_t> bytes,unsigned pc=0x300){m.cpu={};m.cpu.pc=static_cast<std::uint16_t>(pc);m.ram.fill(0);std::copy(bytes.begin(),bytes.end(),m.ram.begin()+pc);m.trace.clear();};
    program({0xa9,0x7f,0x69,1,0x85,0x10,0xa2,4,0xb5,0x0c});check(m.step()==2&&m.cpu.a==127,"LDA immediate");
    check(m.step()==2&&m.cpu.a==128&&(m.cpu.p&0xc0)==0xc0,"ADC signed overflow");check(m.step()==3&&m.ram[16]==128,"STA zero page");m.step();check(m.step()==4&&m.cpu.a==128,"LDA zero page indexed");
    program({0xbd,0xff,0x00});m.cpu.x=1;m.ram[0x100]=0x31;m.trace_enabled=true;check(m.step()==5&&m.cpu.a==0x31,"indexed page cross");check(m.trace[3].dummy&&m.trace[3].address==0,"page cross dummy address");m.trace_enabled=false;
    program({0xbd,0xfe,0});m.cpu.x=1;check(m.step()==4,"indexed no page cross");
    program({0x9d,0xfe,0});m.cpu.x=1;check(m.step()==5,"indexed store always dummy");
    program({0xa1,0xfe});m.cpu.x=1;m.ram[255]=0x11;m.ram[0]=1;m.ram[0x111]=0x72;check(m.step()==6&&m.cpu.a==0x72,"indexed indirect zero-page wrap");
    program({0xb1,0xff});m.cpu.y=1;m.ram[255]=255;m.ram[0]=0;m.ram[0x100]=0x43;check(m.step()==6&&m.cpu.a==0x43,"indirect indexed wrap and cross");
    program({0xd0,2});check(m.step()==3&&m.cpu.pc==0x304,"taken branch");
    program({0xd0,2});m.cpu.p|=2;check(m.step()==2&&m.cpu.pc==0x302,"untaken branch");
    program({0xd0,0xfc},0x3fe);check(m.step()==4&&m.cpu.pc==0x3fc,"branch backward page crossing");
    program({0x6c,0xff,0});m.ram[0xff]=0x34;m.ram[0]=0x12;m.ram[0x100]=0x56;check(m.step()==5&&m.cpu.pc==0x1234,"NMOS JMP indirect page-wrap behavior");
    program({0x20,0x10,3});m.ram[0x310]=0x60;check(m.step()==6&&m.cpu.pc==0x310&&m.cpu.sp==0xfb,"JSR stack");check(m.step()==6&&m.cpu.pc==0x303,"RTS return");
    program({0x08,0x28});m.cpu.p=0x29;m.step();check(m.ram[0x1fd]==0x39,"PHP stack B U bits");m.cpu.p=0x24;m.step();check(m.cpu.p==0x29,"PLP B not stored");
    program({0x06,0x10});m.ram[0x10]=0x81;m.trace_enabled=true;check(m.step()==5&&m.ram[0x10]==2&&(m.cpu.p&1),"ASL zero page");check(m.trace[3].write&&m.trace[3].value==0x81&&m.trace[4].value==2,"RMW old/new writes");m.trace_enabled=false;
    program({0x00,0xea});check(m.step()==7&&m.cpu.pc==0x8800&&m.ram[0x1fc]==2&&(m.ram[0x1fb]&0x10),"BRK PC and B flag");
    program({0xea});m.cpu.nmi_pending=true;check(m.step()==7&&m.cpu.pc==0x8800&&!(m.ram[0x1fb]&16),"NMI stack/vector");
    program({0xea});m.cpu.irq_line=true;m.cpu.p&=~4;check(m.step()==7&&m.cpu.pc==0x8800,"IRQ architecture");
    program({0xea});m.cpu.irq_line=true;check(m.step()==2&&m.cpu.pc==0x301,"IRQ masked");
    program({0xf8,0xa9,0x09,0x69,1});m.step();m.step();m.step();check(m.cpu.a==10&&(m.cpu.p&8),"NES decimal arithmetic disabled");
    // Independent binary arithmetic oracle, both carries and all operands.
    program({0x69,0});
    for(unsigned op:{0x69u,0xe9u})for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)for(unsigned carry=0;carry<2;++carry){
        m.cpu.pc=0x300;m.ram[0x300]=static_cast<std::uint8_t>(op);m.ram[0x301]=static_cast<std::uint8_t>(b);m.cpu.a=static_cast<std::uint8_t>(a);m.cpu.p=0x2c|carry;m.cpu.nmi_pending=false;
        m.step();int result=op==0x69?a+b+carry:int(a)-int(b)-int(1-carry);auto byte=std::uint8_t(result);
        bool overflow=op==0x69?((~(a^b)&(a^byte)&128)!=0):(((a^b)&(a^byte)&128)!=0);
        unsigned expected=0x2c|(byte==0?2:0)|(byte&128)|(overflow?64:0)|((op==0x69?result>255:result>=0)?1:0);
        check(m.cpu.a==byte&&m.cpu.p==expected,"exhaustive binary ADC/SBC and decimal-disabled flags");
    }
    constexpr std::uint8_t official[]={
        0x00,0x01,0x05,0x06,0x08,0x09,0x0a,0x0d,0x0e,0x10,0x11,0x15,0x16,0x18,0x19,0x1d,0x1e,
        0x20,0x21,0x24,0x25,0x26,0x28,0x29,0x2a,0x2c,0x2d,0x2e,0x30,0x31,0x35,0x36,0x38,0x39,0x3d,0x3e,
        0x40,0x41,0x45,0x46,0x48,0x49,0x4a,0x4c,0x4d,0x4e,0x50,0x51,0x55,0x56,0x58,0x59,0x5d,0x5e,
        0x60,0x61,0x65,0x66,0x68,0x69,0x6a,0x6c,0x6d,0x6e,0x70,0x71,0x75,0x76,0x78,0x79,0x7d,0x7e,
        0x81,0x84,0x85,0x86,0x88,0x8a,0x8c,0x8d,0x8e,0x90,0x91,0x94,0x95,0x96,0x98,0x99,0x9a,0x9d,
        0xa0,0xa1,0xa2,0xa4,0xa5,0xa6,0xa8,0xa9,0xaa,0xac,0xad,0xae,0xb0,0xb1,0xb4,0xb5,0xb6,0xb8,0xb9,0xba,0xbc,0xbd,0xbe,
        0xc0,0xc1,0xc4,0xc5,0xc6,0xc8,0xc9,0xca,0xcc,0xcd,0xce,0xd0,0xd1,0xd5,0xd6,0xd8,0xd9,0xdd,0xde,
        0xe0,0xe1,0xe4,0xe5,0xe6,0xe8,0xe9,0xea,0xec,0xed,0xee,0xf0,0xf1,0xf5,0xf6,0xf8,0xf9,0xfd,0xfe};
    check(std::size(official)==151,"official opcode reference cardinality");
    for(unsigned op=0;op<256;++op){program({static_cast<std::uint8_t>(op),0,0});bool valid=std::find(std::begin(official),std::end(official),op)!=std::end(official);
        if(valid){auto cycles=m.step();check(cycles>=2&&cycles<=7&&!m.cpu.halted,"official opcode bounded execution");}
        else {
            // M2 replaces M0's unsupported-opcode fault contract with digital
            // execution. The full cycle/ALU/bus oracle is in test_m2.cpp.
            bool kil=(op&15)==2&&op!=0x82&&op!=0xa2&&op!=0xc2&&op!=0xe2;
            auto elapsed=m.step();check(elapsed>=2&&elapsed<=8,"unofficial opcode bounded execution");
            check(m.cpu.halted==kil&&(!kil||m.cpu.fault_pc==0x300),"KIL alone latches halt and location");
        }
    }
}
void bus(){
    Machine m(synthetic_nrom());m.write(0x0007,123);check(m.read(0x0807)==123&&m.read(0x1007)==123&&m.read(0x1807)==123,"CPU RAM mirrors");
    m.controllers[0].buttons=0xa5;m.write(0x4016,1);check((m.read(0x4016)&1)==1,"controller high strobe");m.controllers[0].buttons=0xa4;check((m.read(0x4016)&1)==0,"controller live A while strobe");m.write(0x4016,0);m.controllers[0].buttons=0;
    unsigned value=0;for(unsigned i=0;i<8;++i)value|=(m.read(0x4016)&1)<<i;check(value==0xa4,"controller latched serial bits");check((m.read(0x4016)&1)==1&&(m.read(0x4016)&1)==1,"controller shift exhaustion ones");
    for(unsigned parity=0;parity<2;++parity){while((m.cycles&1)!=parity)m.read(0);for(unsigned i=0;i<256;++i)m.ram[0x200+i]=static_cast<std::uint8_t>(i^0x5a);
        m.ppu.oam_address=0xfc;m.write(0x4014,2);auto start=m.cycles;auto pt=m.ppu.ticks;auto stall=m.step();check(stall==514-(start&1)&&m.last_dma_stall==stall,"DMA halt on get needs alignment; halt on put does not");check(m.ppu.ticks-pt==3*stall,"DMA continues PPU scheduler");
        bool equal=true;for(unsigned i=0;i<256;++i)equal=equal&&m.ppu.oam[(i+0xfc)&255]==(i^0x5a);check(equal&&m.ppu.oam_address==0xfc,"DMA OAM wrap and bytes");
    }
    Machine a(synthetic_nrom()),b(synthetic_nrom());a.trace_enabled=true;b.trace_enabled=true;for(int i=0;i<50;++i){a.step();b.step();}check(a.trace==b.trace,"deterministic cycle trace");auto state=a.serialize();a.trace.clear();a.trace_enabled=false;check(a.serialize()==state,"trace excluded from canonical state");
    m.ram[9]=71;auto sp=m.cpu.sp;auto start=m.cycles;m.reset();check(m.ram[9]==71&&m.cpu.sp==std::uint8_t(sp-3)&&m.cycles-start==7&&m.cpu.pc==0x8000,"warm reset bus cycles and RAM preservation");
}
class FetchSpy final:public Mapper {
public:
    std::unique_ptr<Mapper> inner=load_cartridge(synthetic_nrom());
    struct Event{std::uint16_t a;std::uint64_t dot;FetchKind kind;};std::vector<Event> events;
    bool irq{};
    bool irq_line()const override{return irq;}
    std::uint8_t cpu_read(std::uint16_t a)const override{return inner->cpu_read(a);}void cpu_write(std::uint16_t a,std::uint8_t b)override{inner->cpu_write(a,b);}
    std::uint8_t ppu_read(std::uint16_t a)const override{return inner->ppu_read(a);}void ppu_write(std::uint16_t a,std::uint8_t b)override{inner->ppu_write(a,b);}
    void ppu_fetch(std::uint16_t a,std::uint64_t d,FetchKind k)override{events.push_back({a,d,k});}
    Mirroring mirroring()const override{return inner->mirroring();}std::uint64_t chr_source()const override{return inner->chr_source();}std::uint64_t chr_epoch()const override{return inner->chr_epoch();}void encode(Encoder&e)const override{inner->encode(e);}
};
void ppu(){
    auto m=load_cartridge(synthetic_nrom());Ppu p;
    p.write_memory(*m,0x3f10,0x25);check(p.read_memory(*m,0x3f00)==0x25&&p.read_memory(*m,0x3f30)==0x25,"universal palette mirrors");
    p.write_register(*m,6,0x20);p.write_register(*m,6,0);p.write_register(*m,7,0x34);check(p.v==0x2001&&p.read_memory(*m,0x2000)==0x34,"PPUDATA write increment");
    p.write_register(*m,6,0x20);p.write_register(*m,6,0);check(p.read_register(*m,7)==0&&p.read_register(*m,7)==0x34,"PPUDATA read buffer");
    p.write_register(*m,0,4);p.write_register(*m,6,0x20);p.write_register(*m,6,0);p.write_register(*m,7,7);check(p.v==0x2020,"PPUDATA stride 32");
    p.write_register(*m,5,0x2b);check(p.fine_x==3&&(p.t&31)==5&&p.write_toggle,"scroll coarse/fine X");p.status=0xe0;p.io_latch=0x1b;check(p.read_register(*m,2)==0xfb&&!p.write_toggle&&!(p.status&0x80),"PPUSTATUS latch/status reset");
    p.status|=0x80;p.write_register(*m,0,0x80);check(p.nmi_edge&&p.nmi_line,"NMI enable in vblank edge");p.read_register(*m,2);check(!p.nmi_line,"status clears NMI level");
    auto composition=[&](bool behind,bool bg,bool flip){Ppu q;q.scanline=80;q.dot=1;q.mask=0x1e;q.bg_pixel[0]=bg?1:0;q.bg_palette[0]=0;q.palette[1]=0x21;q.palette[17]=0x16;
        q.sprite_count=1;auto&s=q.sprites[0];s.slot=0;s.x=0;s.low=flip?1:128;s.flags=(behind?32:0)|(flip?64:0);s.y=79;s.tile=4;s.pattern=64;
        q.tick(*m);auto sample=q.working.samples[80*256];check(sample.sprite_opaque&&sample.sprite_zero,"sprite candidate provenance");check((q.status&64)==(bg?64:0),"sprite-zero hit overlap");return sample;};
    auto behind=composition(true,true,false);check(behind.layer==Layer::background&&behind.sprite_behind&&behind.color==0x21,"background wins behind sprite");
    check(composition(false,true,false).layer==Layer::sprite,"front sprite wins");check(composition(true,false,true).layer==Layer::sprite,"transparent BG exposes flipped behind sprite");
    Ppu edge;edge.scanline=50;edge.dot=256;edge.mask=0x1e;edge.bg_pixel.fill(1);edge.sprite_count=1;edge.sprites[0].slot=0;edge.sprites[0].x=255;edge.sprites[0].low=128;edge.tick(*m);check(!(edge.status&64),"sprite-zero excludes x255");
    FetchSpy spy;Ppu pipeline;pipeline.mask=0x1e;pipeline.palette[1]=0x21;pipeline.nametables.fill(1);
    for(int i=0;i<341+257;++i)pipeline.tick(spy);
    check(pipeline.working.samples[0].layer==Layer::background&&pipeline.working.samples[0].background.local_x==0&&pipeline.working.samples[7].background.local_x==7,"background pipeline aligns visible tile coordinates");
    check(pipeline.working.samples[8].background.nametable==0x2001,"second visible tile fetch provenance");
    const auto& provenance=pipeline.working.samples[8].background;
    check(provenance.chr_source==spy.chr_source()&&provenance.high_fetch_epoch==provenance.fetch_epoch+2,"cartridge and both pattern-fetch clocks observed");
    auto chr_rom=synthetic_nrom();chr_rom[5]=0;chr_rom.resize(16+16384);auto writable=load_cartridge(chr_rom);
    writable->ppu_write(16,128);Ppu mixed;mixed.mask=0x1e;mixed.scanline=0;mixed.dot=5;mixed.next_tile=1;mixed.next_nt=0x2000;
    mixed.tick(*writable);writable->ppu_write(24,128);for(int i=0;i<4;++i)mixed.tick(*writable);writable->ppu_write(16,0);
    check(mixed.bg_pixel[8]==3&&mixed.bg_source[8].chr_epoch==1&&mixed.bg_source[8].high_chr_epoch==2,"mixed-plane CHR writes preserve fetched data and source epochs");
    std::set<FetchKind> kinds;for(const auto&e:spy.events)kinds.insert(e.kind);check(kinds.contains(FetchKind::nametable)&&kinds.contains(FetchKind::attribute)&&kinds.contains(FetchKind::pattern_low)&&kinds.contains(FetchKind::pattern_high)&&kinds.contains(FetchKind::sprite)&&kinds.contains(FetchKind::dummy),"mapper-visible fetch kinds");
    check(std::is_sorted(spy.events.begin(),spy.events.end(),[](const auto&a,const auto&b){return a.dot<b.dot;}),"monotonic PPU fetch clock");
    Machine irq_machine(synthetic_nrom());auto irq_mapper=std::make_unique<FetchSpy>();irq_mapper->irq=true;irq_machine.mapper=std::move(irq_mapper);irq_machine.cpu.p&=~4;irq_machine.tick();
    check(irq_machine.cpu.irq_line&&irq_machine.step()==7&&irq_machine.cpu.pc==0x8800,"mapper IRQ seam routes independently of presentation");
    Ppu timing;timing.mask=0x18;auto tick_frame=[&](){auto old=timing.frame_number,start=timing.ticks;while(timing.frame_number==old)timing.tick(*m);return timing.ticks-start;};tick_frame();auto a=tick_frame(),b=tick_frame();check((a==89341&&b==89342)||(a==89342&&b==89341),"NTSC rendered odd-frame dot skip");
    Machine witness(synthetic_nrom());witness.run_frames(5);auto f=witness.frame();validate_frame(*f);check(witness.ram[0]>=3&&f->number==5,"CPU-driven NMI witness frames");check(witness.ppu.status&64,"witness sprite-zero overlap");
}
void apu(){
    Apu a,b;for(auto*p:{&a,&b}){p->write(0x4017,0x40);p->write(0x4015,15);p->write(0x4000,0xbf);p->write(0x4002,40);p->write(0x4003,8);p->write(0x4004,0x7a);p->write(0x4006,70);p->write(0x4007,8);p->write(0x4008,0xbf);p->write(0x400a,90);p->write(0x400b,8);p->write(0x400c,0x3f);p->write(0x400e,2);p->write(0x400f,8);}
    for(int i=0;i<60000;++i){a.tick();b.tick();}check(a.pcm==b.pcm&&!a.pcm.empty(),"APU deterministic PCM");auto mm=std::minmax_element(a.pcm.begin(),a.pcm.end());check(*mm.first!=*mm.second,"nonconstant audio");check(a.pcm.size()==std::uint64_t(60000)*44100/1789773,"rational sample clock");check(a.lfsr!=1&&a.triangle_sequence!=0,"noise and triangle evolution");
    a.write(0x4015,0);check((a.status()&15)==0,"channel disable clears lengths");for(int i=0;i<100;++i)a.tick();check(a.pcm.back()==0,"disabled channel PCM silence");
    Apu irq;for(int i=0;i<29829;++i)irq.tick();check(irq.frame_irq&&(irq.status()&64)&&!irq.frame_irq,"frame IRQ and status acknowledgement");
    Apu five;five.write(0x4017,0xc0);for(int i=0;i<40000;++i)five.tick();check(five.five_step&&!five.frame_irq,"five-step sequencer IRQ inhibition");
    Apu dmc;dmc.write(0x4010,0x8f);dmc.write(0x4011,0x20);dmc.write(0x4012,2);dmc.write(0x4013,1);dmc.write(0x4015,16);
    check(dmc.dmc_needs_fetch()&&dmc.dmc_fetch_address()==0xc080&&(dmc.status()&16),"DMC enable schedules bounded sample fetch");
    dmc.dmc_supply(0xff);check(dmc.dmc.fetches==1&&dmc.dmc.bytes_remaining==16&&dmc.dmc.output==0x20,"DMC sample reader/direct DAC state");
    for(int i=0;i<4000;++i)dmc.tick();check(dmc.dmc.output>0x20&&!dmc.pcm.empty(),"DMC delta output evolves");
    Apu irq_dmc;irq_dmc.write(0x4010,0x80);irq_dmc.write(0x4013,0);irq_dmc.write(0x4015,16);irq_dmc.dmc_supply(0);
    check(irq_dmc.dmc.irq&&(irq_dmc.status()&0x80),"DMC end-of-sample IRQ status");irq_dmc.write(0x4015,0);check(!irq_dmc.dmc.irq,"DMC status write acknowledges IRQ");
    Apu loop_dmc;loop_dmc.write(0x4010,0x40);loop_dmc.write(0x4013,0);loop_dmc.write(0x4015,16);loop_dmc.dmc_supply(0);
    check(loop_dmc.dmc.bytes_remaining==1&&!loop_dmc.dmc.irq,"DMC loop reloads one-byte sample");
    Machine dmc_machine(synthetic_nrom());dmc_machine.write(0x4010,0x0f);dmc_machine.write(0x4012,0);dmc_machine.write(0x4013,0);
    auto before_dmc_cycles=dmc_machine.cycles;dmc_machine.write(0x4015,16);
    check(dmc_machine.apu.dmc.fetches==0&&dmc_machine.cycles==before_dmc_cycles+1,"DMC cannot halt the enabling CPU write");
    for(unsigned i=0;i<4&&!dmc_machine.apu.dmc.fetches;++i)dmc_machine.read(0);
    check(dmc_machine.apu.dmc.fetches==1&&dmc_machine.apu.dmc.stall_cycles>=3&&dmc_machine.apu.dmc.stall_cycles<=4
        &&dmc_machine.apu.dmc.sample_buffer==dmc_machine.mapper->cpu_read(0xc000),"mapper-backed DMC fetch with parity-bounded stall");
    for(int channel=0;channel<4;++channel){Apu isolated;isolated.write(0x4017,0x40);isolated.write(0x4015,1<<channel);
        if(channel<2){isolated.write(0x4000+channel*4,0xbf);isolated.write(0x4002+channel*4,42);isolated.write(0x4003+channel*4,8);}
        else if(channel==2){isolated.write(0x4008,0xbf);isolated.write(0x400a,42);isolated.write(0x400b,8);}else{isolated.write(0x400c,0x3f);isolated.write(0x400e,2);isolated.write(0x400f,8);}
        for(int i=0;i<20000;++i)isolated.tick();auto range=std::minmax_element(isolated.pcm.begin(),isolated.pcm.end());check(*range.first!=*range.second,"each isolated pulse/triangle/noise produces waveform");
    }
}
void semantic(){
    static_assert(std::is_const_v<ImmutableFrame::element_type>);
    Source s;s.pattern=64;s.tile=4;s.palette=2;s.flags=64;s.oam_slot=3;auto key=asset_key(0,Layer::sprite,s);auto surface=surface_key(key,Layer::sprite,s);s.oam_slot=19;s.origin_x=100;
    check(asset_key(0,Layer::sprite,s)==key&&surface_key(key,Layer::sprite,s)==surface,"sprite keys independent of slot and position");++s.chr_epoch;check(asset_key(0,Layer::sprite,s)!=key,"CHR epoch invalidates asset identity");
    auto changed=asset_key(0,Layer::sprite,s);++s.chr_source;check(asset_key(0,Layer::sprite,s)!=changed,"CHR source domain enters AssetKey");changed=asset_key(0,Layer::sprite,s);++s.high_chr_epoch;check(asset_key(0,Layer::sprite,s)!=changed,"mixed-plane epoch enters AssetKey");
    auto raw=blank();auto sprite=[&](Frame&f,int x,int y,int slot){auto&sample=f.samples[y*width+x];sample.layer=Layer::sprite;sample.sprite.pattern=64;sample.sprite.tile=4;sample.sprite.local_x=2;sample.sprite.local_y=3;sample.sprite.oam_slot=static_cast<std::uint8_t>(slot);sample.sprite.origin_x=x-2;sample.sprite.origin_y=y-3;color(f,x,y,0x16);};
    sprite(raw,20,40,0);auto first=observe(raw);check(first->samples[40*width+20].confidence==0,"new sprite has no invented history");
    auto moved=blank();moved.number=2;sprite(moved,23,41,9);auto second=observe(moved,first.get());auto p=second->samples[41*width+23];
    check(p.previous_surface==first->samples[40*width+20].surface_key&&p.motion_x==-3&&p.motion_y==-1&&p.confidence==192&&p.reactive==Reactive::stable,"semantic motion survives OAM reorder");
    color(moved,23,41,0x17);auto appearance=observe(moved,first.get());check(appearance->samples[41*width+23].reactive==Reactive::appearance,"palette appearance reactive");
    sprite(moved,50,40,10);auto duplicate=observe(moved,first.get());check(duplicate->samples[41*width+23].reactive==Reactive::ambiguous&&duplicate->samples[41*width+23].confidence==0,"duplicate sprite identity rejects confidence");
    moved.number=4;auto gap=observe(moved,first.get());check(gap->samples[41*width+23].reactive==Reactive::history_gap,"history gap rejection");
    moved=blank();moved.number=2;sprite(moved,23,41,9);moved.samples[41*width+23].sprite.local_x=3;auto fresh=observe(moved,first.get());check(fresh->samples[41*width+23].reactive==Reactive::disoccluded,"newly visible sample disocclusion");
    Source bg;bg.nametable=0x2000;auto ba=asset_key(0,Layer::background,bg);auto bs=surface_key(ba,Layer::background,bg);++bg.nametable;check(surface_key(ba,Layer::background,bg)!=bs,"BG surface includes nametable cell");
    check(sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty known answer");Bytes abc{'a','b','c'};check(sha256(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc known answer");
}
void presentation(){
    auto raw=blank();for(auto&s:raw.samples){s.layer=Layer::background;s.background.pattern=16;s.background.tile=1;}
    for(int y=30;y<42;++y)for(int x=30;x<42;++x)if(x<33||x>=39||y<33||y>=39)color(raw,x,y,0x30);
    for(int x=80;x<89;++x)color(raw,x,40,0x30);
    auto f=observe(raw);auto regions=connected_regions(*f);check(std::accumulate(regions.regions.begin(),regions.regions.end(),0u,[](unsigned n,const Region&r){return n+r.area;})==pixels,"regions partition all samples");
    for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD}){auto a=render(*f,mode),b=render(*f,mode);check(a==b&&a.width==768&&a.height==720,"presentation deterministic and 3x");}
    auto exact=render(*f,Mode::EXACT_DIGITAL),hd=render(*f,Mode::SEMANTIC_HD);check(hd!=exact,"semantic contour coverage changes output");
    auto dark=digital_color(0,0),light=digital_color(0x30,0);bool topology=true,fine=true;
    for(int y=0;y<hd.height;++y)for(int x=0;x<hd.width;++x){auto at=std::size_t(y*hd.width+x)*3;int dd=0,dl=0;
        for(int c=0;c<3;++c){dd+=std::abs(int(hd.rgb[at+c])-dark[c]);dl+=std::abs(int(hd.rgb[at+c])-light[c]);}
        bool original=raw.indices[(y/3)*width+x/3]==0x30;topology=topology&&((dl<dd)==original);
        if(y/3==40&&x/3>=80&&x/3<89)for(int c=0;c<3;++c)fine=fine&&hd.rgb[at+c]==light[c];
    }
    check(topology,"HD ring hole/component/silhouette support exact at every subpixel");check(fine,"one-pixel text strokes protected");
    auto split=raw;for(auto&s:split.samples)if(s.color==0x30)s.background.pattern=32;auto semantic_split=observe(split);check(render(*semantic_split,Mode::SEMANTIC_HD)!=hd,"same raster different source identity changes reconstruction");
    check(render(*f,Mode::SEMANTIC_HD,901,803).rgb.size()==901u*803*3,"resolution-independent semantic raster");
    auto gray=digital_color(0x22,1);check(gray[0]==gray[1]&&gray[1]==gray[2],"grayscale palette mask");check(digital_color(0x22,0x20)!=digital_color(0x22,0),"emphasis affects display palette");
    rejects([&]{render(*f,Mode::TEMPORAL_RECONSTRUCTION);},"temporal rung explicitly disabled");rejects([&]{render(*f,Mode::NEURAL_RESIDUAL_EXPERIMENTAL);},"neural rung explicitly disabled");
    NeuralResidualPolicy policy;check(!policy.enabled&&!policy.geometry_authority&&!policy.silhouette_authority&&!policy.hud_authority&&!policy.low_frequency_authority,"neural has no structural authority");
}
void invariance(){
    std::array<std::string,3> reference{};Bytes state;std::array<std::string,3> images{};
    for(int iteration=0;iteration<2;++iteration)for(int mode=-1;mode<3;++mode){Machine m(synthetic_nrom());ImmutableFrame previous;
        for(int frame=0;frame<5;++frame){m.controllers[0].buttons=frame&1;m.run_frames(1);auto before=m.serialize();auto obs=observe(*m.frame(),previous.get());if(mode>=0)(void)render(*obs,static_cast<Mode>(mode));check(before==m.serialize(),"renderer cannot change canonical bytes");previous=obs;}
        std::array<std::string,3> hashes{m.state_hash(),frame_hash(*m.frame()),m.audio_hash()};std::array<std::string,3> outputs;for(int i=0;i<3;++i)outputs[i]=sha256(render(*previous,static_cast<Mode>(i)).rgb);
        if(iteration==0&&mode==-1){reference=hashes;state=m.serialize();images=outputs;}else{check(reference==hashes,"core/frame/audio metamorphic invariance");check(state==m.serialize(),"canonical state byte equality");check(images==outputs,"repeated-run presentation hashes");}
    }
    Machine a(synthetic_nrom()),b(synthetic_nrom());a.controllers[0].buttons=1;a.run_frames(5);b.run_frames(5);check(a.state_hash()!=b.state_hash()&&frame_hash(*a.frame())!=frame_hash(*b.frame()),"input negative control changes machine and frame");
    auto before=a.state_hash();a.apu.noise_divider^=1;check(before!=a.state_hash(),"APU hidden state enters digest");before=a.state_hash();a.ppu.bg_pixel[4]^=1;check(before!=a.state_hash(),"PPU pipeline enters digest");
}
void bounds(){
    auto f=blank();f.samples.pop_back();rejects([&]{observe(f);},"short semantic packet");f=blank();f.indices[0]=64;rejects([&]{render(f,Mode::EXACT_DIGITAL);},"inconsistent palette packet");f=blank();
    for(auto extent:std::array<std::array<int,2>,4>{{{0,0},{-1,720},{2147483647,2147483647},{2049,1920}}})rejects([&]{render(f,Mode::SEMANTIC_HD,extent[0],extent[1]);},"extent checked before allocation");
    rejects([&]{render(f,Mode::EXACT_DIGITAL,769,720);},"exact requires integer scale");rejects([&]{render(f,static_cast<Mode>(99));},"unknown renderer");
    Machine m(synthetic_nrom());rejects([&]{m.run_frames(601);},"frame count bounded");rejects([&]{m.run_frames(1,0);},"instruction budget bounded");
    auto rom=synthetic_nrom();for(unsigned i=0;i<256;++i){auto bad=rom;bad[4]=static_cast<std::uint8_t>(i);if(i!=1)rejects([&]{load_cartridge(bad);},"malformed PRG sizes bounded");}
    for(unsigned i=0;i<256;++i){auto bad=rom;bad[5]=static_cast<std::uint8_t>(i);if(i!=1)rejects([&]{load_cartridge(bad);},"malformed CHR sizes bounded");}
    auto nes2=rom;nes2[7]=8;nes2[9]=0xee;rejects([&]{inspect_ines(nes2);},"oversize NES2 before allocation");
    Apu cap;cap.pcm.resize(441000);cap.sample_phase=1789772;rejects([&]{cap.tick();},"PCM hard allocation bound");
}
}
int main(int argc,char**argv){
    const std::array<std::pair<const char*,std::function<void()>>,9> suites{{{"cartridge",cartridge},{"cpu",cpu},{"bus",bus},{"ppu",ppu},{"apu",apu},{"semantic",semantic},{"presentation",presentation},{"invariance",invariance},{"bounds",bounds}}};
    bool selected=false;for(const auto&[name,run]:suites)if(argc==1||(argc==2&&std::string(argv[1])==name)){selected=true;try{run();}catch(const std::exception&e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    if(!selected){std::cerr<<"unknown test suite\n";return 2;}
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
