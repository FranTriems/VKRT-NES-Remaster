#include "vkrt/nes_native_s0/mmc3.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace vkrt::nes;
namespace {
unsigned checks{}, failures{};
void check(bool b,const char* s){++checks;if(!b){++failures;std::cerr<<"FAIL "<<s<<'\n';}}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"bounded cartridge rejection");}
Bytes rom(unsigned prg=4,unsigned chr=4,std::uint8_t flags=0){
    Bytes r(16+prg*16384+chr*8192);r[0]='N';r[1]='E';r[2]='S';r[3]=26;r[4]=prg;r[5]=chr;r[6]=0x40|flags;
    for(unsigned b=0;b<prg*2;++b)std::fill_n(r.begin()+16+b*8192,8192,std::uint8_t(b));
    for(unsigned b=0;b<chr*8;++b)std::fill_n(r.begin()+16+prg*16384+b*1024,1024,std::uint8_t(b));
    return r;
}
Mmc3 cart(const Bytes& r){return Mmc3(r,inspect_ines(r));}
void bank(Mapper& m,unsigned r,unsigned value,unsigned mode=0){m.cpu_write(0x8000,r|mode);m.cpu_write(0x8001,value);}
Bytes encoded(const Mapper& m){Encoder e;m.encode(e);return e.bytes;}
void rise(Mapper& m,unsigned edges=3,std::uint16_t high=0x1000){
    m.ppu_fetch(0,0,FetchKind::dummy);for(unsigned i=0;i<edges;++i)m.m2_falling();m.ppu_fetch(high,1,FetchKind::pattern_low);
}
void banks(){
    auto r=rom();auto info=inspect_ines(r);check(info.mapper==4&&info.prg_size==65536&&info.chr_size==32768,"mapper 4 parse");
    auto m=cart(r);auto factory=load_cartridge(r);check(dynamic_cast<Mmc3*>(factory.get())!=nullptr,"genuine mapper factory");
    for(unsigned mode:{0u,64u})for(unsigned r6=0;r6<256;++r6)for(unsigned r7:{0u,1u,7u,8u,63u,255u}){
        bank(m,6,r6,mode);bank(m,7,r7,mode);
        // Oracle is a list of expected physical banks, independent of implementation helpers.
        const unsigned expected[4]={mode?6u:(r6&63)%8,(r7&63)%8,mode?(r6&63)%8:6u,7};
        for(unsigned slot=0;slot<4;++slot)for(unsigned offset:{0u,4095u,8191u})
            check(m.cpu_read(0x8000+slot*8192+offset)==expected[slot],"PRG mode, switches, fixed windows, bounds");
    }
    for(unsigned mode:{0u,128u})for(unsigned n=0;n<256;++n){
        const unsigned regs[6]={n,n^0x55,n+3,n+7,n+11,n+19};
        for(unsigned i=0;i<6;++i)bank(m,i,regs[i],mode);
        unsigned expected[8]={(n&254)%32,((n&254)+1)%32,((n^0x55)&254)%32,(((n^0x55)&254)+1)%32,
                              ((n+3)&255)%32,((n+7)&255)%32,((n+11)&255)%32,((n+19)&255)%32};
        for(unsigned slot=0;slot<8;++slot)for(unsigned offset:{0u,511u,1023u})
            check(m.ppu_read(slot*1024+offset)==expected[mode?(slot+4)%8:slot],"CHR paired alignment, singles, inversion, bounds");
    }
    bank(m,2,4);auto source=m.chr_source_at(0x1000),epoch=m.chr_epoch_at(0x1008);bank(m,2,5);
    check(source!=m.chr_source_at(0x1000)&&epoch!=m.chr_epoch_at(0x1008),"mapped physical source and high-plane identity");
    bank(m,2,4);check(source==m.chr_source_at(0x1000),"return to physical ROM bank preserves identity");
    m.ppu_write(0x1000,99);check(m.ppu_read(0x1000)==4,"CHR ROM immutable");
    auto ram=cart(rom(2,0));bank(ram,2,15);ram.ppu_write(0x1000,91);bank(ram,3,7);
    check(ram.ppu_read(0x1400)==91&&ram.chr_epoch()==1,"bounded banked CHR RAM aliases");
    ram.ppu_write(0x1400,91);check(ram.chr_epoch()==1,"identical CHR RAM write stable");
    for(unsigned flags:{0u,1u,8u}){auto mirror=cart(rom(2,1,flags));Ppu p;
        for(unsigned b:{0u,1u}){mirror.cpu_write(0xbffe,b);
            check(mirror.mirroring()==(flags==8?Mirroring::four_screen:(b?Mirroring::horizontal:Mirroring::vertical)),"mirroring control/four screen");
            for(unsigned t=0;t<4;++t)p.write_memory(mirror,0x2000+t*1024,10+t);
            unsigned expected=flags==8?10:(b?11:12);check(p.read_memory(mirror,0x2000)==expected,"PPU dynamic nametable mapping");
        }
    }
    m.cpu_write(0xa001,0x80);m.cpu_write(0x6000,42);m.cpu_write(0x7fff,17);
    m.cpu_write(0xbfff,0xc0);m.cpu_write(0x6000,99);check(m.cpu_read(0x6000)==42&&m.cpu_read(0x7fff)==17,"RAM write protection");
    m.cpu_write(0xa001,0);m.cpu_write(0x6000,98);check(m.cpu_read(0x6000)==0,"RAM disabled deterministic bus");
    m.cpu_write(0xa001,0x80);check(m.cpu_read(0x6000)==42,"disabled writes do not modify RAM");
    auto trained=rom(2,1,4);trained.insert(trained.begin()+16,512,0xa5);check(cart(trained).cpu_read(0x7000)==0xa5,"trainer policy");
    auto min=rom(2,0);check(load_cartridge(min)->cpu_read(0xe000)==3,"minimum supported size");
    auto max=rom(32,32);check(load_cartridge(max)->cpu_read(0xe000)==63,"maximum supported size");
    // Non-power-of-two sizes are explicitly bounded modulo the physical bank count.
    auto odd=cart(rom(3,3));bank(odd,6,63);bank(odd,2,255);check(odd.cpu_read(0x8000)==3&&odd.ppu_read(0x1000)==15,"non-power-of-two physical bounds");
    rejects([&]{load_cartridge(rom(1,1));});rejects([&]{load_cartridge(rom(33,1));});rejects([&]{load_cartridge(rom(2,33));});
    auto shortrom=r;shortrom.pop_back();rejects([&]{load_cartridge(shortrom);});
    auto nes2=r;nes2[7]=8;nes2[8]=0x10;rejects([&]{load_cartridge(nes2);});
}
void irq(){
    auto m=cart(rom());m.cpu_write(0xdffe,2);m.cpu_write(0xdfff,99);m.cpu_write(0xffff,0);
    check(m.snapshot().counter==0&&m.snapshot().reload&&!m.irq_line(),"C001 requests NEXT qualified reload");
    rise(m,2);check(m.snapshot().reload&&m.snapshot().counter==0&&!m.irq_line(),"two M2 edges reject");
    rise(m);check(m.snapshot().counter==2&&!m.snapshot().reload,"qualified rise loads latch");
    m.ppu_fetch(0x3000,999999,FetchKind::dummy);check(m.snapshot().counter==2,"held high does not re-clock");
    rise(m);check(m.snapshot().counter==1&&!m.irq_line(),"counter decrement");rise(m,3,0x3000);
    check(m.irq_line()&&m.snapshot().counter==0,"A13 high still clocks A12 and asserts");
    m.cpu_write(0xfffe,1);check(!m.irq_line()&&!m.snapshot().enabled&&m.stats().irq_acknowledgements==1,"E000 acknowledges pending level");
    rise(m);check(m.snapshot().counter==2&&!m.irq_line(),"counter continues while disabled");
    m.cpu_write(0xe001,1);check(m.snapshot().counter==2,"E001 does not reset");
    m.cpu_write(0xc001,0);rise(m);check(m.snapshot().counter==2,"reload instead of decrement");
    m.cpu_write(0xc000,0);check(m.snapshot().counter==2,"latch write does not alter counter");
    m.cpu_write(0xc001,0);rise(m);check(m.irq_line(),"Sharp/new zero latch asserts on reload");
    m.cpu_write(0xc001,0);check(m.irq_line()&&m.snapshot().reload,"reload request does not acknowledge pending IRQ");
    for(unsigned i=0;i<4;++i){m.cpu_write(0xe000,0);m.cpu_write(0xe001,0);rise(m);check(m.irq_line(),"zero latch repeats after acknowledge");}
    auto no_clock=cart(rom());no_clock.cpu_write(0xe001,0);no_clock.ppu_fetch(0,0,FetchKind::dummy);
    check(!no_clock.irq_line(),"enable at counter zero does not immediately assert");
    no_clock.ppu_fetch(0x1000,1000000,FetchKind::dummy);check(!no_clock.irq_line(),"arbitrarily many PPU dots without M2 cannot qualify");
    for(unsigned edges=0;edges<=5;++edges){auto f=cart(rom());f.cpu_write(0xe001,0);rise(f,edges);check(f.irq_line()==(edges>=3),"three falling M2 edge threshold");}
    // Same low duration, different M2 phase/history: two vs three falls before rise.
    auto a=cart(rom()),b=cart(rom());a.cpu_write(0xe001,0);b.cpu_write(0xe001,0);
    for(auto* f:{&a,&b}){f->ppu_fetch(0x1000,0,FetchKind::dummy);f->m2_falling();f->ppu_fetch(0,1,FetchKind::dummy);f->m2_falling();f->m2_falling();}
    a.ppu_fetch(0x1000,9,FetchKind::dummy);a.m2_falling();b.m2_falling();b.ppu_fetch(0x1000,9,FetchKind::dummy);
    check(!a.irq_line()&&b.irq_line(),"phase ordering changes qualification at same PPU duration");
    auto glitch=cart(rom());glitch.cpu_write(0xe001,0);rise(glitch,2);glitch.ppu_fetch(0,100,FetchKind::dummy);glitch.m2_falling();glitch.ppu_fetch(0x1000,200,FetchKind::dummy);
    check(!glitch.irq_line(),"rejected high pulse restarts low history");
    auto palette=cart(rom());palette.cpu_write(0xe001,0);Ppu p;for(int i=0;i<3;++i)palette.m2_falling();
    p.write_memory(palette,0x3f00,2);check(!palette.irq_line(),"internal palette write is not external A12");
    p.read_memory(palette,0x3000);check(palette.irq_line(),"external nametable A12 remains visible");
}
Bytes program(){
    auto r=rom();const auto base=16+7*8192;
    const std::uint8_t code[]={0x58,0x4c,0x01,0xe0};std::copy(std::begin(code),std::end(code),r.begin()+base);
    const std::uint8_t handler[]={0xe6,0x10,0xa9,0,0x8d,0,0xe0,0x40};std::copy(std::begin(handler),std::end(handler),r.begin()+base+0x100);
    for(unsigned i=0;i<3;++i){r[base+0x1ffa+i*2]=i==1?0:0;r[base+0x1ffb+i*2]=i==1?0xe0:0xe1;}
    return r;
}
void setup(Machine& m){m.apu.write(0x4017,0x40);m.ppu.ctrl=0x10;m.ppu.mask=0x1e;m.ppu.oam.fill(255);m.ppu.palette[1]=0x21;m.ppu.palette[2]=0x30;m.ppu.palette[3]=0x16;bank(*m.mapper,2,5);m.mapper->cpu_write(0xc000,2);m.mapper->cpu_write(0xc001,0);m.mapper->cpu_write(0xe001,0);}
void integration(){
    // Real fetch trains, with M2 at all three discrete phases. Empty 8x8 sprite
    // fetches must use PPUCTRL's sprite table, rather than forcing A12 high.
    for(unsigned phase=0;phase<3;++phase){auto m=cart(program());Ppu p;p.scanline=0;p.dot=0;p.mask=0x18;p.ctrl=0x08;p.oam.fill(255);
        m.cpu_write(0xc000,200);m.cpu_write(0xc001,0);
        for(unsigned t=0;t<341*4;++t){if(t%3==phase)m.m2_falling();p.tick(m);}
        check(m.stats().qualified_a12==4,"background-low/sprite-high train clocks once per line across M2 phases");
        check(m.stats().rejected_a12==28,"seven remaining sprite rises per line rejected by M2 history");
        p.mask=0;auto clocks=m.stats().qualified_a12;
        for(unsigned t=0;t<341*4;++t){if(t%3==phase)m.m2_falling();p.tick(m);}
        check(m.stats().qualified_a12==clocks,"disabled rendering does not synthesize line clocks");
    }
    // With the opposite tables the 337..5 nametable gap also survives three
    // falling M2 edges. Test the bus history, not an assumed scanline counter.
    {auto m=cart(program());Ppu p;p.scanline=0;p.mask=0x18;p.ctrl=0x10;p.oam.fill(255);
        for(unsigned t=0;t<341*4;++t){if(t%3==0)m.m2_falling();p.tick(m);}
        check(m.stats().qualified_a12==7,"opposite tables include qualified end-of-line nametable gaps");}
    Machine m(program());setup(m);m.step();check(!(m.cpu.p&4),"synthetic CLI");
    for(unsigned i=0;i<10000&&!m.cpu.irq_line;++i)m.step();
    auto pc=m.cpu.pc;check(m.cpu.irq_line&&m.mapper->irq_line(),"PPU-driven IRQ reaches CPU pin");
    check(m.step()==7&&m.cpu.pc==0xe100,"IRQ taken at instruction boundary");
    check(m.ram[0x1fd]==(pc>>8)&&m.ram[0x1fc]==(pc&255)&&!(m.ram[0x1fb]&16),"IRQ stack return/B flag");
    for(unsigned i=0;i<4;++i)m.step();check(m.ram[0x10]==1&&!m.mapper->irq_line()&&m.mapper->stats().irq_acknowledgements==1,"first-party CPU handler acknowledges mapper");
    Machine baseline(program()),presented(program());setup(baseline);setup(presented);ImmutableFrame old;
    for(unsigned f=0;f<3;++f){baseline.run_frames(1);presented.run_frames(1);auto before=presented.serialize();auto raw=presented.frame();auto h=frame_hash(*raw),audio=presented.audio_hash();
        auto observation=observe(*raw,old.get());check(observation->mapper_domain==4,"semantic mapper domain");
        for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD}){auto first=render(*observation,mode);check(first.rgb==render(*observation,mode).rgb,"MMC3 presentation repeat");}
        check(before==presented.serialize()&&h==frame_hash(*raw)&&audio==presented.audio_hash(),"MMC3 full-state presentation bracket");
        check(baseline.serialize()==presented.serialize(),"presentation-on/off continued execution invariance");old=observation;
    }
    auto identity=cart(rom());Ppu p;p.scanline=0;p.dot=5;p.ctrl=0x10;p.mask=0x18;p.next_tile=1;bank(identity,2,4);p.tick(identity);auto low_source=p.next_chr_source;
    bank(identity,2,7);for(int i=0;i<4;++i)p.tick(identity);
    check(p.bg_source[8].chr_source==low_source&&(p.bg_source[8].chr_epoch>>48)==4&&(p.bg_source[8].high_chr_epoch>>48)==7,"mixed-plane actual mapped provenance");
    auto s=p.bg_source[8],t=s;t.high_chr_epoch=4ULL<<48;check(asset_key(4,Layer::background,s)!=asset_key(4,Layer::background,t),"high-plane remap cannot alias semantic key");
}
void serialization(){
    auto a=cart(rom()),b=cart(rom());check(encoded(a)==encoded(b),"fresh deterministic encoding");
    auto both=[&](auto operation){auto before=encoded(a);operation(a);operation(b);check(encoded(a)==encoded(b),"repeat mutation encoding");check(before!=encoded(a),"mutable field represented in encoding");};
    for(unsigned r=0;r<8;++r)both([&](auto& m){bank(m,r,r+11);});
    both([](auto& m){m.cpu_write(0x8000,0xc7);});both([](auto& m){m.cpu_write(0xa000,0);});
    both([](auto& m){m.cpu_write(0x6000,55);});both([](auto& m){m.cpu_write(0xa001,0xc0);});
    both([](auto& m){m.cpu_write(0xc000,2);});both([](auto& m){m.cpu_write(0xc001,0);});both([](auto& m){m.cpu_write(0xe001,0);});
    both([](auto& m){m.m2_falling();});both([](auto& m){m.m2_falling();});both([](auto& m){m.m2_falling();});
    both([](auto& m){m.ppu_fetch(0x1000,0,FetchKind::dummy);});both([](auto& m){rise(m);});both([](auto& m){rise(m);});
    both([](auto& m){m.cpu_write(0xe000,0);});both([](auto& m){rise(m,0);});
    auto ram=cart(rom(2,0));auto before=encoded(ram);ram.ppu_write(0,42);check(before!=encoded(ram),"CHR RAM and write epoch serialized");
}
}
int main(int argc,char** argv){
    try {const std::string suite=argc>1?argv[1]:"all";
        if(suite=="banks"||suite=="all")banks();if(suite=="irq"||suite=="all")irq();
        if(suite=="integration"||suite=="all")integration();if(suite=="serialization"||suite=="all")serialization();
        check(checks!=0,"known suite");
    }catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
