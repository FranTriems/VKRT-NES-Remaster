#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace vkrt::nes;
namespace {
unsigned checks{}, failures{};
void check(bool b,const char* name){++checks;if(!b){++failures;if(failures<30)std::cerr<<"FAIL "<<name<<'\n';}}
Bytes rom(){
    auto r=synthetic_nrom();
    // Different NMI/IRQ targets make vector hijacking observable independently
    // of any external ROM. Both handlers are RTI.
    r[16+0x1000]=r[16+0x1100]=0x40;
    r[16+0x3ffa]=0;r[16+0x3ffb]=0x90;
    r[16+0x3ffe]=0;r[16+0x3fff]=0x91;
    return r;
}
void program(Machine& m,std::initializer_list<std::uint8_t> bytes,unsigned pc=0x300){
    m.cpu={};m.cpu.pc=std::uint16_t(pc);m.ram.fill(0);
    std::copy(bytes.begin(),bytes.end(),m.ram.begin()+pc);
    m.apu.frame_irq=false;m.apu.irq_inhibit=true;m.trace.clear();
}
unsigned nz(unsigned b){return (b==0?2:0)|(b&128);}
class PinMapper final:public Mapper {
public:
    std::unique_ptr<Mapper> inner=load_cartridge(rom());
    unsigned falls{}, assert_at{1}, release_at{2};
    void m2_falling()override{++falls;}
    bool irq_line()const override{return falls>=assert_at&&falls<release_at;}
    std::uint8_t cpu_read(std::uint16_t a)const override{return inner->cpu_read(a);}
    void cpu_write(std::uint16_t a,std::uint8_t b)override{inner->cpu_write(a,b);}
    std::uint8_t ppu_read(std::uint16_t a)const override{return inner->ppu_read(a);}
    void ppu_write(std::uint16_t a,std::uint8_t b)override{inner->ppu_write(a,b);}
    void ppu_fetch(std::uint16_t a,std::uint64_t t,FetchKind k)override{inner->ppu_fetch(a,t,k);}
    Mirroring mirroring()const override{return inner->mirroring();}
    std::uint64_t chr_source()const override{return inner->chr_source();}
    std::uint64_t chr_epoch()const override{return inner->chr_epoch();}
    void encode(Encoder& e)const override{inner->encode(e);e.u32(falls);e.u32(assert_at);e.u32(release_at);}
};
void opcodes(){
    // Independent no-page-cross cycle reference, including all twelve KILs.
    constexpr unsigned clocks[256]={
      7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
      6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
      6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
      6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
      2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
      2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,5,2,5,4,4,4,4,2,4,2,4,4,4,4,4,
      2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
      2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7};
    Machine m(rom());m.trace_enabled=true;
    for(unsigned op=0;op<256;++op){
        program(m,{std::uint8_t(op),0,0});
        if(op==0x10)m.cpu.p|=128;if(op==0x50)m.cpu.p|=64;
        if(op==0x90)m.cpu.p|=1;if(op==0xd0)m.cpu.p|=2;
        check(m.step()==clocks[op],"all 256 decode cycle counts");
        check(m.trace.size()==clocks[op],"each instruction clock has a bus transaction");
        if(m.cpu.halted){
            auto pc=m.cpu.pc;auto before=m.ppu.ticks;
            check(m.step()==1&&m.cpu.pc==pc&&m.ppu.ticks==before+3,"KIL holds PC while peripheral clocks continue");
            check(m.trace.back().dummy&&!m.trace.back().write&&m.trace.back().address==pc,"KIL held read bus");
            m.reset();check(!m.cpu.halted&&m.cpu.pc==0x8000,"reset releases KIL");
        }
    }
    m.trace_enabled=false;
    // Exhaustive digital ALU oracle for six composite RMW families. The oracle
    // computes carry/overflow from integer arithmetic, not CPU helpers.
    const unsigned ops[]={0x07,0x27,0x47,0x67,0xc7,0xe7};
    for(unsigned family=0;family<6;++family){
        program(m,{std::uint8_t(ops[family]),0x40});
        for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)for(unsigned c=0;c<2;++c){
            m.cpu.pc=0x300;m.cpu.a=std::uint8_t(a);m.cpu.p=0x6c|c;m.ram[0x40]=std::uint8_t(b);
            unsigned memory=0,acc=a,flags=0x6c;
            switch(family){
            case 0:memory=(b*2)&255;acc=a|memory;flags|=(b>>7)|nz(acc);break;
            case 1:memory=(b*2+c)&255;acc=a&memory;flags|=(b>>7)|nz(acc);break;
            case 2:memory=b/2;acc=a^memory;flags|=(b&1)|nz(acc);break;
            case 3:{memory=(b/2)|(c*128);unsigned sum=a+memory+(b&1);acc=sum&255;
                flags=0x2c|nz(acc)|(sum>255?1:0)|((~(a^memory)&(a^acc)&128)?64:0);break;}
            case 4:memory=(b-1)&255;flags|=(a>=memory?1:0)|nz((a-memory)&255);break;
            case 5:{memory=(b+1)&255;int difference=int(a)-int(memory)-int(1-c);acc=unsigned(difference)&255;
                flags=0x2c|nz(acc)|(difference>=0?1:0)|(((a^memory)&(a^acc)&128)?64:0);break;}
            }
            check(m.step()==5&&m.cpu.a==acc&&m.ram[0x40]==memory&&m.cpu.p==flags,"exhaustive composite binary ALU and decimal flag retention");
        }
    }
    // Each addressing form of every composite must read, old-write, new-write.
    constexpr unsigned forms[]={0x03,0x07,0x0f,0x13,0x17,0x1b,0x1f};
    for(unsigned group:{0u,0x20u,0x40u,0x60u,0xc0u,0xe0u})for(unsigned form:forms){
        program(m,{std::uint8_t(group+form),0x40,0});m.ram[0x40]=0x80;m.ram[0x41]=0;
        m.trace_enabled=true;m.step();const auto n=m.trace.size();
        check(!m.trace[n-3].write&&m.trace[n-2].write&&m.trace[n-1].write
            &&m.trace[n-3].address==m.trace[n-2].address&&m.trace[n-2].address==m.trace[n-1].address
            &&m.trace[n-3].value==m.trace[n-2].value,"all composite addressing forms preserve old-write bus side effect");
    }
    program(m,{0x13,0xff});m.cpu.y=1;m.ram[255]=255;m.ram[0]=0;m.ram[0x100]=0x81;
    check(m.step()==8&&m.trace[4].address==0&&m.trace[4].dummy&&m.ram[0x100]==2,"indirect RMW zero-page wrap and page-cross dummy");
    program(m,{0x1c,0xff,0});m.cpu.x=1;
    check(m.step()==5&&m.trace[3].dummy&&m.trace[3].address==0&&m.trace[4].address==0x100,"indexed NOP performs both page-cross reads");
    program(m,{0x0c,0x15,0x40});m.apu.frame_irq=true;
    check(m.step()==4&&!m.apu.frame_irq,"absolute NOP acknowledges APU status through real read");
    for(unsigned op:{0x0bu,0x2bu,0x4bu,0x6bu,0xcbu,0xabu,0x8bu,0xebu}){
        program(m,{std::uint8_t(op),0x93});m.cpu.a=0x7e;m.cpu.x=0x3f;m.cpu.p=0x2d;
        m.step();unsigned a=0x12,x=0x3f,p=0x2c;
        switch(op){
        case 0x0b:case 0x2b:break;
        case 0x4b:a=9;break;
        case 0x6b:a=0x89;p=0xac;break;
        case 0xcb:a=0x7e;x=0xab;p=0xac;break;
        case 0xab:a=x=0x93;p=0xad;break;
        case 0x8b:a=0x12;p=0x2d;break;
        case 0xeb:a=0xeb;p=0xec;break;
        }
        check(m.cpu.a==a&&m.cpu.x==x&&m.cpu.p==p,"immediate digital opcode known answers");
    }
    program(m,{0xa7,0x40,0x87,0x41});m.ram[0x40]=0xa5;m.step();m.cpu.x=0x0f;m.step();
    check(m.cpu.a==0xa5&&m.ram[0x41]==5,"LAX load and SAX wired-AND store");
    program(m,{0xbb,0xff,0});m.cpu.y=1;m.cpu.sp=0x3c;m.ram[0x100]=0xaa;
    check(m.step()==5&&m.cpu.a==0x28&&m.cpu.x==0x28&&m.cpu.sp==0x28,"LAS page-cross and stack mask");
    for(unsigned op:{0x93u,0x9bu,0x9cu,0x9eu,0x9fu})for(unsigned low:{0xfcu,0xffu}){
        program(m,{std::uint8_t(op),std::uint8_t(low),0x12});m.cpu.a=0xff;m.cpu.x=3;m.cpu.y=3;
        if(op==0x93){m.ram[low]=std::uint8_t(low);m.ram[(low+1)&255]=0x12;}
        m.step();auto last=m.trace.back();
        check(last.write&&last.value==3&&last.address==(low==255?0x302:0x12ff),"high-byte-masked stores corrupt high address only on carry");
        if(op==0x9b)check(m.cpu.sp==3,"TAS also writes stack pointer");
    }
}
void interrupts(){
    // CPU status strobe follows the APU edge, including each of the three
    // assertion clocks. This also tests the read's acknowledgement ordering.
    for(unsigned clock=29826;clock<=29830;++clock){
        Machine m(rom());program(m,{0xea});m.apu.irq_inhibit=false;
        m.apu.frame_cycle=clock;m.trace_enabled=true;
        const auto before=m.cycles, dots=m.ppu.ticks;
        const bool edge=clock>=29827&&clock<29830;
        auto status=m.read(0x4015);
        check(bool(status&64)==edge,"status strobe sees coincident frame IRQ assertion");
        check(!m.apu.frame_irq&&!m.cpu.irq_line&&m.cpu.irq_sample==edge,"status acknowledges live IRQ but preserves sampled pin");
        check(m.cycles==before+1&&m.ppu.ticks==dots+3&&m.trace.size()==1
            &&m.trace[0].cycle==before&&m.trace[0].value==status,"status read is exactly one traced bus clock");
        check(bool(m.read(0x4015)&64)==(clock>=29826&&clock<29829),"subsequent status read sees only a fresh assertion");
    }
    // Every branch condition, both directions, and every input edge around
    // the poll. Same-page taken branches retain the operand-fetch poll;
    // page correction polls again. A three-clock JMP has the ordinary poll.
    constexpr unsigned branches[]={0x10,0x30,0x50,0x70,0x90,0xb0,0xd0,0xf0};
    constexpr unsigned masks[]={128,128,64,64,1,1,2,2};
    for(unsigned b=0;b<8;++b)for(bool taken:{false,true})for(bool cross:{false,true})for(bool back:{false,true}){
        const unsigned pc=cross?(back?0x400:0x3fc):0x340;
        const unsigned clocks=taken?(cross?4:3):2;
        for(unsigned edge=1;edge<=clocks+1;++edge){
            Machine m(rom());program(m,{std::uint8_t(branches[b]),std::uint8_t(back?0xfc:4)},pc);
            m.cpu.p=0x20|((taken==bool(b&1))?masks[b]:0);
            auto pin=std::make_unique<PinMapper>();pin->assert_at=edge;pin->release_at=edge+1;m.mapper=std::move(pin);
            check(m.step()==clocks,"all branch conditions retain 2/3/4 clock timing");
            check(m.cpu.irq_pending==(edge==(taken&&cross?3u:1u)),"branch poll location across every phase and direction");
        }
    }
    for(unsigned edge=1;edge<=4;++edge){
        Machine m(rom());program(m,{0x4c,0x40,3});m.cpu.p=0x20;
        auto pin=std::make_unique<PinMapper>();pin->assert_at=edge;pin->release_at=edge+1;m.mapper=std::move(pin);
        check(m.step()==3&&m.cpu.irq_pending==(edge==2),"JMP does not inherit the taken-branch poll exception");
    }
    for(unsigned edge:{1u,2u}){
        Machine pin(rom());program(pin,{0xea,0xea});pin.cpu.p=0x20;
        auto source=std::make_unique<PinMapper>();source->assert_at=edge;source->release_at=edge+1;
        pin.mapper=std::move(source);pin.step();
        check(pin.cpu.irq_pending==(edge==1),"IRQ samples penultimate cycle of a two-cycle instruction");
        if(edge==1)check(!pin.cpu.irq_line&&pin.step()==7,"polled IRQ survives a falling pin before the boundary");
    }
    Machine m(rom());
    program(m,{0x58,0xea,0xea});m.apu.dmc.irq=true;
    check(m.step()==2&&!(m.cpu.p&4)&&!m.cpu.irq_pending,"CLI polls old I");
    check(m.step()==2&&m.cpu.irq_pending,"one complete instruction after CLI");
    check(m.step()==7&&m.cpu.pc==0x9100&&m.ram[0x1fc]==2&&!(m.ram[0x1fb]&16),"IRQ entry after delayed CLI uses B clear");
    program(m,{0x78,0xea});m.cpu.p=0x20;
    check(m.step()==2&&(m.cpu.p&4)&&m.cpu.irq_pending,"SEI polls old I before setting it");
    check(m.step()==7&&m.cpu.pc==0x9100,"sampled IRQ survives SEI");
    program(m,{0x28,0xea,0xea});m.ram[0x1fe]=0x20;
    check(m.step()==4&&!m.cpu.irq_pending&&!(m.cpu.p&4),"PLP clearing I has one instruction latency");
    m.step();check(m.cpu.irq_pending,"PLP latency expires");
    program(m,{0x40});m.ram[0x1fe]=0x20;m.ram[0x1ff]=0x44;m.ram[0x100]=3;
    check(m.step()==6&&m.cpu.pc==0x344&&m.cpu.irq_pending,"RTI polls the already restored I");
    m.apu.dmc.irq=false;
    // Taken same-page branches poll before their extra read, whereas a page
    // correction adds another polling opportunity.
    for(bool cross:{false,true}){
        program(m,{0xd0,2},cross?0x3fd:0x300);m.cpu.p=0x20;
        m.apu.irq_inhibit=false;m.apu.frame_cycle=29825;
        m.step();check(m.cpu.irq_pending==cross,"branch page correction interrupt polling");
    }
    // A rising NMI across every entry cycle: vector selection is after PCL push.
    for(bool brk:{false,true})for(unsigned delay=0;delay<7;++delay){
        program(m,{std::uint8_t(brk?0:0xea),0xea});m.cpu.p=0x20;
        if(!brk){m.cpu.irq_pending=true;m.cpu.poll_valid=true;}
        m.ppu.status=0;m.ppu.ctrl=128;m.ppu.nmi_line=false;m.ppu.scanline=240;m.ppu.dot=std::uint16_t(340-delay*3);
        m.step();bool hijacked=delay<3;
        check(m.cpu.pc==(hijacked?0x9000:0x9100),"NMI vector hijack cutoff during BRK/IRQ entry");
        check(bool(m.ram[0x1fb]&16)==brk,"hijack preserves original stacked B bit");
    }
    program(m,{0x8d,0,0x20,0xea,0xea});m.cpu.a=128;m.ppu.scanline=242;m.ppu.dot=10;m.ppu.status=128;m.ppu.ctrl=0;m.ppu.nmi_line=false;
    m.step();check(!m.cpu.nmi_pending,"enabling NMI on final write misses this instruction poll");
    m.step();check(m.cpu.nmi_pending&&m.cpu.pc==0x304,"NMI enable latency includes next instruction");
    check(m.step()==7&&m.cpu.pc==0x9000,"latched NMI entry");
    // Frame IRQ asserts on three consecutive CPU clocks and can reassert after
    // status acknowledgement; the fourth clock is outside the edge window.
    Apu apu;apu.frame_cycle=29827;
    for(unsigned i=0;i<3;++i){apu.tick();check((apu.status()&64)!=0&&!apu.frame_irq,"three frame IRQ assertion/acknowledgement edges");}
    apu.tick();check(!(apu.status()&64),"frame IRQ assertion window closes");
    for(unsigned parity=0;parity<2;++parity){
        Apu a;a.cycles=parity;a.frame_cycle=123;a.write(0x4017,0x80);
        unsigned delay=parity?4:5;
        for(unsigned i=1;i<delay;++i){a.tick();check(a.frame_cycle==123+i,"frame reset waits through write cycle and parity delay");}
        a.tick();check(a.frame_cycle==0,"frame sequencer reset edge");
    }
}
void vblank(){
    auto mapper=load_cartridge(rom());
    for(unsigned dot=0;dot<5;++dot){
        Ppu p;p.scanline=241;p.dot=0;p.ctrl=128;
        for(unsigned i=0;i<dot;++i)p.tick(*mapper);
        auto status=p.read_register(*mapper,2);
        check(bool(status&128)==(dot>=2),"status flag is set on dot 1, readable after it");
        while(p.dot<6)p.tick(*mapper);
        check(bool(p.status&128)==(dot==0),"only the immediately preceding status read suppresses vblank set");
    }
    Ppu clear;clear.scanline=261;clear.dot=0;clear.status=0xe0;clear.ctrl=128;clear.nmi_line=true;
    clear.tick(*mapper);check(clear.status==0xe0,"pre-render dot 0 preserves flags");
    clear.tick(*mapper);check(clear.status==0&&!clear.nmi_line,"pre-render dot 1 clears all status flags");
    for(unsigned dot=0;dot<4;++dot){
        Machine m(rom());program(m,{0xea});m.ppu.scanline=241;m.ppu.dot=0;m.ppu.ctrl=128;
        for(unsigned i=0;i<dot;++i)m.ppu.tick(*m.mapper);
        m.read(0x2002);m.tick();
        check(m.cpu.nmi_latched==(dot==0),"status read suppresses unsampled NMI level");
    }
    Machine latched(rom());latched.ppu.scanline=242;latched.ppu.status=128;
    latched.ppu.write_register(*latched.mapper,0,128);latched.tick();latched.read(0x2002);
    check(latched.cpu.nmi_latched&&!latched.ppu.nmi_line,"status cannot retract already sampled NMI");
    latched.cpu.nmi_latched=false;latched.ppu.status=128;
    latched.ppu.write_register(*latched.mapper,0,0);latched.tick();
    latched.ppu.write_register(*latched.mapper,0,128);latched.tick();
    check(latched.cpu.nmi_latched,"sampled low then enabled high during vblank rearms NMI");
    latched.cpu.nmi_latched=false;latched.tick();
    check(!latched.cpu.nmi_latched,"held NMI level does not retrigger");
    for(unsigned mask:{0u,8u,16u,24u})for(bool odd:{false,true}){
        Ppu p;p.scanline=261;p.dot=338;p.odd=odd;p.mask=std::uint8_t(mask);
        p.tick(*mapper);p.tick(*mapper);
        check((p.scanline==0)==(odd&&mask!=0),"odd skipped dot depends on either rendering enable");
    }
    for(bool initial:{false,true}){
        Ppu p;p.scanline=261;p.dot=338;p.odd=true;p.mask=initial?8:0;p.tick(*mapper);
        p.write_register(*mapper,1,initial?0:8);p.tick(*mapper);
        check((p.scanline==0)==initial,"render-enable sample precedes skip decision by one dot");
    }
}
void request(Machine& m){
    auto& d=m.apu.dmc;d.current_address=0xc123;d.bytes_remaining=1;d.sample_buffer_empty=true;
    d.fetch_pending=true;d.fetch_delay=0;d.timer=400;d.bits_remaining=8;d.reg[0]=0x80;
}
void dma(){
    // A held opcode fetch does not create an instruction-boundary poll.
    // IRQ still evolves during the halt, and the resumed NOP polls normally.
    for(unsigned parity=0;parity<2;++parity)for(int edge=-2;edge<=2;++edge){
        Machine m(rom());program(m,{0xea,0xea});m.cpu.p=0x20;m.cpu.poll_valid=true;
        m.write(0x4014,2);while((m.cycles&1)!=parity)m.write(0x40,0);
        const unsigned length=514-parity;
        auto pin=std::make_unique<PinMapper>();auto* spy=pin.get();
        pin->assert_at=unsigned(int(length)+edge);pin->release_at=length+20;m.mapper=std::move(pin);
        check(m.step()==length&&m.cpu.pc==0x300&&!m.cpu.irq_pending,"OAM halt does not invent an interrupt poll");
        check(m.cpu.irq_line==(edge<=0)&&spy->falls==length,"IRQ pin and mapper clocks evolve during OAM halt");
        check(m.step()==2&&m.cpu.pc==0x301&&m.cpu.irq_pending==(edge<=1),"first instruction after OAM polls at its normal clock");
        if(edge<=1)check(m.step()==7&&m.cpu.pc==0x9100&&m.ram[0x1fc]==1,"IRQ entry stacks the resumed instruction boundary");
    }
    {
        Machine m(rom());program(m,{0xea});m.cpu.poll_valid=true;m.write(0x4014,2);
        m.ppu.scanline=240;m.ppu.dot=339;m.ppu.ctrl=128;
        m.step();check(m.cpu.nmi_latched&&!m.cpu.nmi_pending,"NMI edge latch evolves during OAM halt without an instruction poll");
        check(m.step()==2&&m.cpu.nmi_pending,"resumed instruction polls the held NMI edge");
        check(m.step()==7&&m.cpu.pc==0x9000,"NMI captured during OAM vectors normally");
    }
    for(unsigned parity=0;parity<2;++parity){
        Machine m(rom());program(m,{0xea});m.write(0x4014,2);
        while((m.cycles&1)!=parity)m.write(0x40,0);
        request(m);auto source=std::make_unique<PinMapper>();auto* spy=source.get();m.mapper=std::move(source);
        auto before=m.cycles;auto duration=m.step();
        check(duration==516-parity&&m.last_dma_stall==duration,"shared OAM/DMC halt and one stolen get cost two extra clocks");
        check(spy->falls==m.cycles-before,"mapper M2 edges continue through DMA halt");
    }
    for(unsigned parity=0;parity<2;++parity){
        Machine m(rom());program(m,{0xea});while((m.cycles&1)!=parity)m.read(0);
        request(m);m.trace_enabled=true;auto before=m.cycles,ppu=m.ppu.ticks,apu=m.apu.cycles;
        m.write(0x40,0x5a);check(m.cycles==before+1&&!m.apu.dmc.fetches,"RDY cannot halt a write");
        before=m.cycles;const unsigned stall=(before&1)?4:3;m.read(0x40);
        check(m.cycles-before==stall+1&&m.apu.dmc.stall_cycles==stall,"DMC halt/dummy/alignment/get parity");
        const auto n=m.trace.size();check(m.trace[n-2].address==0xc123&&!(m.trace[n-2].cycle&1)&&m.trace.back().address==0x40,"DMC mapper get precedes CPU retry");
        check(m.apu.dmc.sample_buffer==m.mapper->cpu_read(0xc123)&&m.apu.dmc.irq,"DMA supplies mapper sample and terminal IRQ");
        check(m.ppu.ticks-ppu==3*(stall+2)&&m.apu.cycles-apu==stall+2,"every stalled clock advances APU and three PPU dots");
    }
    // Sweep a request through every OAM phase, including both transfer ends.
    // The data oracle is independent of DMA phase bookkeeping.
    for(unsigned parity=0;parity<2;++parity)for(unsigned offset=0;offset<=516;++offset){
        Machine m(rom());program(m,{0xea});for(unsigned i=0;i<256;++i)m.ram[0x200+i]=std::uint8_t(i^0xa7);
        m.ppu.oam_address=0xf9;m.write(0x4014,2);while((m.cycles&1)!=parity)m.write(0x40,0);
        request(m);m.apu.dmc.fetch_pending=false;m.apu.dmc.sample_buffer_empty=false;
        m.apu.dmc.bits_remaining=1;m.apu.dmc.timer=std::uint16_t(offset);m.apu.dmc.reg[0]=15;
        m.trace_enabled=true;auto before=m.cycles;m.step();
        unsigned writes=0,reads=0,fetches=0;
        for(const auto& t:m.trace){
            if(t.write&&t.address==0x2004){check((t.cycle&1)!=0,"OAM put parity after DMC collision");++writes;}
            if(!t.write&&t.address>=0x200&&t.address<0x300){check(!(t.cycle&1),"OAM get parity");++reads;}
            if(!t.write&&t.address==0xc123){check(!(t.cycle&1),"DMC get owns colliding read phase");++fetches;}
        }
        check(writes==256&&reads==256&&fetches<=1,"OAM collision never drops or duplicates a byte");
        check(m.cycles-before>=514-parity&&m.cycles-before<=520-parity,"bounded OAM collision extension");
        // Request on the buffer-consumption clock, two setup clocks, then
        // the next get. An interior stolen get costs a get/put pair; at the
        // tail, only the unfinished DMC phases extend the transfer.
        const unsigned baseline=514-parity, ready=offset+1;
        const unsigned get=ready+2+unsigned((before+ready+2)&1);
        const unsigned expected=ready>baseline?baseline:(get<baseline?baseline+2:get+1);
        check(m.cycles-before==expected&&fetches==unsigned(ready<=baseline),"exact OAM collision length and DMC fetch count at every offset");
        if(fetches)check(m.apu.dmc.stall_cycles==get-ready+1,"DMC occupied phase clocks are counted rather than fetches times four");
        for(unsigned i=0;i<256;++i)check(m.ppu.oam[(i+0xf9)&255]==std::uint8_t(i^0xa7),"all OAM bytes survive every collision offset");
    }
    Machine m(rom());program(m,{0xe6,0x40});request(m);m.apu.dmc.fetch_delay=4;m.trace_enabled=true;
    m.step();check(m.ram[0x40]==1&&!m.apu.dmc.fetches,"pending DMC cannot split consecutive RMW writes");
    m.read(0);check(m.apu.dmc.fetches==1,"pending DMC halts the next read");
    // OAM RDY also waits through both writes of an RMW; the last page wins.
    Machine rmw(rom());program(rmw,{0xfe,0xf5,0x3f});rmw.cpu.x=0x1f;rmw.trace_enabled=true;
    // Indexed dummy read of mirrored OAMDATA supplies open bus = 1 to $4014.
    rmw.ppu.oam[0]=1;
    for(unsigned i=0;i<256;++i)rmw.ram[0x200+i]=std::uint8_t(i^0x93);
    check(rmw.step()==7&&rmw.dma_pending&&rmw.dma_page==2,"OAM request cannot interrupt consecutive RMW writes");
    check(rmw.trace[5].write&&rmw.trace[6].write&&rmw.trace[5].value==1&&rmw.trace[6].value==2,"both OAM page writes appear on the bus");
    rmw.step();
    for(unsigned i=0;i<256;++i)check(rmw.ppu.oam[i]==std::uint8_t(i^0x93),"OAM uses final RMW page without duplicating transfer");
    // Buffer reload is requested on the output-unit clock itself, unlike the
    // delayed initial load. Check both alignments without a cartridge oracle.
    for(unsigned parity=0;parity<2;++parity){
        Apu a;a.cycles=parity;a.dmc.bytes_remaining=1;a.dmc.sample_buffer_empty=false;
        a.dmc.sample_buffer=0xa5;a.dmc.bits_remaining=1;a.dmc.timer=1;
        a.tick();check(!a.dmc.fetch_pending,"DMC does not request before buffer consumption");
        a.tick();check(a.dmc.fetch_pending&&!a.dmc.fetch_delay&&a.dmc.sample_buffer_empty
            &&a.dmc.shift_register==0xa5&&!a.dmc.silence,"DMC reload requests on buffer-consumption edge");
        Apu load;load.cycles=parity;load.write(0x4015,16);
        const unsigned delay=parity?3:2;
        check(load.dmc.fetch_pending&&load.dmc.fetch_delay==delay,"initial DMC load retains its phase-dependent request delay");
        for(unsigned i=0;i<delay;++i){load.tick();check(load.dmc.fetch_delay==delay-i-1,"initial load delay advances once per clock");}
    }
    Apu wrap;wrap.dmc.fetch_pending=true;wrap.dmc.bytes_remaining=2;wrap.dmc.current_address=0xffff;
    wrap.dmc_supply(0x81);check(wrap.dmc.current_address==0x8000&&wrap.dmc.bytes_remaining==1,"DMC address wraps into cartridge window");
}
void serialization(){
    Machine m(rom());program(m,{0xea});auto original=m.serialize();
    auto changed=[&](auto& field){auto saved=field;
        if constexpr(std::is_same_v<std::remove_reference_t<decltype(field)>,bool>)field=!saved;
        else field=static_cast<std::remove_reference_t<decltype(field)>>(saved+1);
        check(original!=m.serialize(),"new canonical field changes serialization");field=saved;check(original==m.serialize(),"field restoration restores canonical bytes");};
    changed(m.cpu.irq_pending);changed(m.cpu.poll_valid);changed(m.cpu.irq_sample);changed(m.cpu.irq_previous);
    changed(m.cpu.nmi_latched);changed(m.cpu.nmi_previous);changed(m.cpu.nmi_sampled_line);
    changed(m.ppu.suppress_vblank);changed(m.ppu.skip_rendering);changed(m.apu.dmc.fetch_delay);
    changed(m.apu.dmc.fetch_pending);changed(m.apu.dmc.sample_buffer_empty);changed(m.apu.dmc.bits_remaining);
    changed(m.apu.dmc.timer);changed(m.apu.dmc.current_address);changed(m.apu.dmc.bytes_remaining);
    changed(m.dmc_phase);changed(m.dma_active);changed(m.oam_latch_valid);changed(m.dma_index);changed(m.halted_address);changed(m.oam_latch);
    Machine a(rom()),b(rom());request(a);request(b);a.write(0x4014,2);b.write(0x4014,2);
    a.step();b.step();check(a.serialize()==b.serialize(),"DMA execution is byte deterministic");
    a.run_frames(2);auto bytes=a.serialize();auto observation=observe(*a.frame());
    for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD})render(*observation,mode);
    check(bytes==a.serialize(),"presentation preserves expanded canonical scheduler state");
}
}
int main(int argc,char**argv){
    try{
        std::string suite=argc>1?argv[1]:"all";
        if(suite=="opcodes"||suite=="all")opcodes();
        if(suite=="interrupts"||suite=="all")interrupts();
        if(suite=="vblank"||suite=="all")vblank();
        if(suite=="dma"||suite=="all")dma();
        if(suite=="serialization"||suite=="all")serialization();
        check(checks!=0,"known suite");
    }catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
