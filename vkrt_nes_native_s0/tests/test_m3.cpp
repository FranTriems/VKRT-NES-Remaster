#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace vkrt::nes;
namespace {
unsigned checks{}, failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;if(failures<30)std::cerr<<"FAIL "<<name<<'\n';}}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"unsupported board rejected");}
Bytes rom(unsigned mapper,unsigned prg16,unsigned chr8,unsigned sub=0,bool nes2=false){
    Bytes r(16+prg16*16384+chr8*8192);r[0]='N';r[1]='E';r[2]='S';r[3]=26;
    r[4]=std::uint8_t(prg16);r[5]=std::uint8_t(chr8);r[6]=std::uint8_t(mapper<<4);r[7]=nes2?8:0;r[8]=std::uint8_t(sub<<4);
    for(unsigned i=0;i<prg16;++i)std::fill_n(r.begin()+16+i*16384,16384,std::uint8_t(i));
    for(unsigned i=0;i<chr8*2;++i)std::fill_n(r.begin()+16+prg16*16384+i*4096,4096,std::uint8_t(i));
    return r;
}
Bytes encoded(const Mapper& m){Encoder e;m.encode(e);return e.bytes;}
Bytes encoded(const Ppu& p){Encoder e;p.encode(e);return e.bytes;}
void serial(Mapper& m,unsigned a,unsigned v){for(unsigned i=0;i<5;++i)m.cpu_write(std::uint16_t(a),std::uint8_t(v>>i)&1);}
void timed_serial(Mapper& m,unsigned a,unsigned v,std::uint64_t& cycle){
    for(unsigned i=0;i<5;++i){m.cpu_write_cycle(std::uint16_t(a),std::uint8_t(v>>i)&1,cycle);cycle+=2;}
}
void mirroring(Mapper& m,Mirroring mode){
    Ppu p;
    for(unsigned i=0;i<4;++i)p.write_memory(m,std::uint16_t(0x2000+i*1024),std::uint8_t(10+i));
    const std::array<unsigned,4> expected=mode==Mirroring::one_screen_lower||mode==Mirroring::one_screen_upper?
        std::array<unsigned,4>{13,13,13,13}:mode==Mirroring::horizontal?std::array<unsigned,4>{11,11,13,13}:
        mode==Mirroring::vertical?std::array<unsigned,4>{12,13,12,13}:std::array<unsigned,4>{10,11,12,13};
    for(unsigned i=0;i<4;++i)check(p.read_memory(m,std::uint16_t(0x2000+i*1024))==expected[i],"four logical nametables map correctly");
    if(mode==Mirroring::one_screen_lower)check(p.nametables[0]==13&&p.nametables[1024]==0,"physical one-screen A");
    if(mode==Mirroring::one_screen_upper)check(p.nametables[0]==0&&p.nametables[1024]==13,"physical one-screen B");
}
void mmc1(){
    auto m=load_cartridge(rom(1,16,16));
    check(m->cpu_read(0x8000)==0&&m->cpu_read(0xc000)==15,"MMC1 power-up fixed final bank");
    for(unsigned control=0;control<32;++control){
        serial(*m,0x8000,control);
        constexpr std::array<Mirroring,4> modes{Mirroring::one_screen_lower,Mirroring::one_screen_upper,Mirroring::vertical,Mirroring::horizontal};
        check(m->mirroring()==modes[control&3],"MMC1 control mirroring bits");mirroring(*m,modes[control&3]);
        for(unsigned bank=0;bank<16;++bank){
            serial(*m,0xe000,bank);
            const auto mode=(control>>2)&3;
            const unsigned lo=mode<2?(bank&14):mode==2?0:bank;
            const unsigned hi=mode<2?(bank|1):mode==2?bank:15;
            check(m->cpu_read(0x8000)==lo&&m->cpu_read(0xbfff)==lo&&m->cpu_read(0xc000)==hi&&m->cpu_read(0xffff)==hi,"MMC1 all PRG modes and bank endpoints");
        }
        for(unsigned bank=0;bank<32;++bank){
            serial(*m,0xa000,bank);serial(*m,0xc000,31-bank);
            const unsigned lo=(control&16)?bank:(bank&30),hi=(control&16)?31-bank:(bank|1);
            check(m->ppu_read(0)==lo&&m->ppu_read(0xfff)==lo&&m->ppu_read(0x1000)==hi&&m->ppu_read(0x1fff)==hi,"MMC1 all CHR modes and low-bit ignore");
        }
    }
    serial(*m,0x8000,2);serial(*m,0xe000,3);m->cpu_write(0x8000,1);m->cpu_write(0x8000,1);
    m->cpu_write(0xbfff,0x80);
    check(m->cpu_read(0x8000)==3&&m->cpu_read(0xc000)==15&&m->mirroring()==Mirroring::vertical,"reset discards partial serial and ORs control with 0C");
    serial(*m,0xe000,7);check(m->cpu_read(0x8000)==7,"five fresh writes after reset");
    // Only the fifth address selects the destination; serial data is LSB first.
    m->cpu_write(0x8000,0x80);
    for(unsigned i=0;i<4;++i)m->cpu_write(0xa000,std::uint8_t(5>>i)&1);
    m->cpu_write(0xffff,0);check(m->cpu_read(0x8000)==5,"fifth write chooses destination");
    auto c=load_cartridge(rom(1,16,0));std::uint64_t cycle=100;
    c->cpu_write_cycle(0xe000,1,cycle++);
    for(unsigned i=0;i<4;++i)c->cpu_write_cycle(0xe000,0,cycle++);
    cycle+=2;for(unsigned i=0;i<4;++i){c->cpu_write_cycle(0xe000,0,cycle);cycle+=2;}
    check(c->cpu_read(0x8000)==1,"consecutive-cycle run ignores every data write after first");
    timed_serial(*c,0x8000,0,cycle);
    c->cpu_write_cycle(0x8000,0x80,cycle-1);
    check(c->cpu_read(0xc000)==15,"bit-7 reset effective on consecutive cycle");
    cycle+=2;
    auto before=encoded(*c);c->cpu_write(0x6123,0xb7);c->ppu_write(0x1234,0x65);
    check(encoded(*c)!=before&&c->cpu_read(0x6123)==0xb7&&c->ppu_read(0x1234)==0x65,"MMC1 both RAMs are canonical");
    timed_serial(*c,0xe000,17,cycle);c->cpu_write(0x6123,0x33);
    check(c->cpu_read(0x6123)==0,"MMC1 disabled PRG RAM deterministic read");
    timed_serial(*c,0xe000,1,cycle);check(c->cpu_read(0x6123)==0xb7,"disabled writes preserve PRG RAM");
    serial(*c,0x8000,0x1c);serial(*c,0xa000,1);serial(*c,0xc000,0);
    check(c->ppu_read(0x234)==0x65,"MMC1 CHR RAM follows bank wiring");
    c->ppu_write(0x234,0x99);serial(*c,0xa000,0);serial(*c,0xc000,1);
    check(c->ppu_read(0x1234)==0x99,"MMC1 RAM persists across bank switches");
    // Actual CPU RMW bus writes, not an opcode-specific mapper decision.
    auto image=rom(1,16,0);image[16]=0;
    Machine cpu(image);cpu.cpu.pc=0x300;cpu.ram[0x300]=0xee;cpu.ram[0x301]=0;cpu.ram[0x302]=0x80;
    check(cpu.step()==6,"INC absolute issues six bus cycles");
    for(unsigned i=0;i<4;++i){cpu.read(0);cpu.write(0xe000,0);}
    check(cpu.mapper->cpu_read(0x8000)==0,"RMW second write ignored by observed cycle");
    rejects([&]{load_cartridge(rom(1,32,0));});rejects([&]{load_cartridge(rom(1,2,1,1,true));});
}
void discrete(){
    for(unsigned id:{2u,3u})for(unsigned policy=0;policy<3;++policy){
        auto r=rom(id,id==2?16:2,id==3?4:0,policy,policy!=0);
        // Distinct driven bytes at two addresses, and in the selected PRG bank.
        r[16+0x123]=2;r[16+0x124]=0;
        auto m=load_cartridge(r);const bool conflict=policy==2||(id==3&&policy==0);
        m->cpu_write(0x8123,3);
        const unsigned bank=conflict?2:3;
        check((id==2?m->cpu_read(0x8000):m->ppu_read(0))==(id==2?bank:bank*2),"discrete conflict resolves old ROM byte at actual address");
        if(id==2){
            check(m->cpu_read(0xffff)==15,"UxROM fixed final bank");
            // Selected bank drives value 2 or 3 here, not bank zero's 0.
            m->cpu_write(0x8124,1);check(m->cpu_read(0x8000)==(conflict?0:1),"UxROM conflict uses currently selected bank");
        }else{
            check(m->cpu_read(0x8000)==0&&m->cpu_read(0xffff)==1,"CNROM PRG fixed");
            m->cpu_write(0x8124,3);check(m->ppu_read(0)==(conflict?0:6),"CNROM address-specific conflict");
            auto old=m->ppu_read(0);m->ppu_write(0,0xff);check(m->ppu_read(0)==old,"CNROM CHR ROM immutable");
        }
        mirroring(*m,Mirroring::horizontal);
        r[6]|=1;auto vertical=load_cartridge(r);vertical->cpu_write(0x8123,0xff);mirroring(*vertical,Mirroring::vertical);
        r[6]|=8;auto four=load_cartridge(r);four->cpu_write(0x8123,0xff);mirroring(*four,Mirroring::four_screen);
    }
    auto ux=load_cartridge(rom(2,16,0,1,true));
    for(unsigned b=0;b<16;++b){ux->cpu_write(0xffff,std::uint8_t(b));check(ux->cpu_read(0x8000)==b&&ux->cpu_read(0xbfff)==b&&ux->cpu_read(0xc000)==15,"all UxROM banks");}
    auto cn=load_cartridge(rom(3,1,4,1,true));
    for(unsigned b=0;b<4;++b){cn->cpu_write(0x8000,std::uint8_t(b));check(cn->ppu_read(0)==b*2&&cn->ppu_read(0x1fff)==b*2+1&&cn->cpu_read(0xc000)==0,"all CNROM banks with 16K mirrored PRG");}
    auto ax=load_cartridge(rom(7,16,0));
    for(unsigned b=0;b<8;++b)for(unsigned screen=0;screen<2;++screen){
        ax->cpu_write(0x8000,std::uint8_t(b|(screen<<4)));
        check(ax->cpu_read(0x8000)==b*2&&ax->cpu_read(0xffff)==b*2+1,"all AxROM 32K banks");
        mirroring(*ax,screen?Mirroring::one_screen_upper:Mirroring::one_screen_lower);
    }
    for(unsigned id:{2u,7u}){auto m=load_cartridge(rom(id,2,0));m->ppu_write(0x1fff,0xa5);m->cpu_write(0x8000,0xff);check(m->ppu_read(0x1fff)==0xa5,"discrete fixed CHR RAM");}
    rejects([&]{load_cartridge(rom(2,2,0,3,true));});rejects([&]{load_cartridge(rom(3,2,8));});
    rejects([&]{load_cartridge(rom(7,2,1));});rejects([&]{load_cartridge(rom(2,3,0));});
}
void provenance(){
    for(unsigned id:{1u,2u,3u,7u}){
        const auto r=rom(id,id==3?2:4,id==3?4:0);auto a=load_cartridge(r),b=load_cartridge(r);
        check(encoded(*a)==encoded(*b),"mapper power state deterministic");
        auto source=a->chr_source_at(0),epoch=a->chr_epoch_at(0),other=a->chr_epoch_at(0x1000);
        a->ppu_write(0,0xa5);b->ppu_write(0,0xa5);
        check(a->chr_source_at(0)==source,"CHR mutation preserves physical source");
        check((a->chr_epoch_at(0)!=epoch)==(id!=3)&&a->chr_epoch_at(0x1000)==other,"only mutated physical CHR bank epoch changes");
        epoch=a->chr_epoch_at(0);a->ppu_write(0,0xa5);check(a->chr_epoch_at(0)==epoch,"same byte write leaves epoch stable");
        if(id==1){serial(*a,0x8000,0x1c);serial(*a,0xa000,1);serial(*b,0x8000,0x1c);serial(*b,0xa000,1);}
        if(id==3){a->cpu_write(0xc000,1);b->cpu_write(0xc000,1);}
        if(id==1||id==3)check(a->chr_source_at(0)!=source&&a->chr_epoch_at(0)!=epoch,"bank switch changes physical source and high-plane identity");
        check(encoded(*a)==encoded(*b)&&a->chr_source_at(0)==b->chr_source_at(0),"mapper replay includes bank/epoch state");
        auto changed=r;changed.back()^=1;check(load_cartridge(changed)->chr_source()!=a->chr_source(),"distinct cartridge provenance");
    }
}
void request(Machine& m){auto& d=m.apu.dmc;d.enabled=d.fetch_pending=d.sample_buffer_empty=true;d.fetch_delay=0;d.bytes_remaining=1;d.current_address=0xc000;d.timer=1000;}
void controller(){
    auto r=rom(0,2,1);r[16+16384]=0xa6;
    for(unsigned port=0;port<2;++port)for(unsigned parity=0;parity<2;++parity)for(unsigned buttons=0;buttons<256;++buttons){
        Machine m(r);const auto addr=std::uint16_t(0x4016+port);m.controllers[port].buttons=std::uint8_t(buttons);
        m.write(0x4016,1);m.write(0x4016,0);m.open_bus=0xe0;
        check(m.read(addr)==(0xe0|(buttons&1))&&m.controllers[port].position==1,"normal controller read preserves high open bus and clocks once");
        m.write(0x4016,1);m.write(0x4016,0);while((m.cycles&1)!=parity)m.read(0);
        request(m);m.trace_enabled=true;const auto start=m.cycles;
        const auto value=m.read(addr);
        check(value==(0xa0|((buttons>>1)&1))&&m.controllers[port].position==2&&m.controllers[port^1].position==0,"DMC collision deletes exactly one bit on selected port");
        check(m.cycles-start==4+parity&&m.apu.dmc.fetches==1,"controller glitch does not alter DMA clock schedule");
        unsigned count=0;for(const auto& e:m.trace)if(e.address==addr){
            check((e.value&1)==((buttons>>(e.dummy?0:1))&1),"held setup reads retain one serial bit until sample get");++count;
        }
        check(count==3+parity,"DMA trace retains halt/dummy/alignment CPU reads");
    }
    for(unsigned port=0;port<2;++port){
        Machine m(r);m.controllers[port].buttons=0x55;m.write(0x4016,1);m.write(0x4016,0);
        request(m);m.read(0);check(m.controllers[port].position==0,"DMA stealing another address cannot clock controller");
        check((m.read(std::uint16_t(0x4016+port))&1)==1&&m.controllers[port].position==1,"noncolliding read after DMA");
        m.write(0x4016,1);request(m);check((m.read(std::uint16_t(0x4016+port))&1)==1&&m.controllers[port].position==0,"high strobe prevents shift even during collision");
        m.write(0x4016,0);request(m);m.write(0x4016,0);check(m.apu.dmc.fetch_pending,"DMC does not steal controller write");
    }
}
void until(Ppu& p,Mapper& m,unsigned dot){while(p.dot<=dot)p.tick(m);}
void setup(Ppu& p,unsigned count,unsigned line=100,unsigned first=0){
    p.scanline=std::uint16_t(line);p.dot=1;p.mask=0x18;p.oam.fill(255);
    for(unsigned i=0;i<count;++i)p.oam[(i+first)*4]=std::uint8_t(line);
}
void sprites(){
    auto m=load_cartridge(synthetic_nrom());
    for(unsigned count=0;count<=9;++count)for(unsigned mask:{0u,8u,16u,24u}){
        Ppu p;setup(p,count);p.mask=std::uint8_t(mask);p.secondary_oam.fill(0);
        p.tick(*m);check(p.secondary_oam[0]==0,"odd clear dot does not write secondary OAM");p.tick(*m);
        check(p.secondary_oam[0]==(mask?255:0),"even clear dot writes FF only while rendering");
        until(p,*m,64);if(mask)check(std::all_of(p.secondary_oam.begin(),p.secondary_oam.end(),[](auto b){return b==255;}),"all 32 secondary bytes clear by dot 64");
        until(p,*m,129);check(!(p.status&32),"ninth contiguous sprite cannot overflow before dot 130");
        p.tick(*m);check(bool(p.status&32)==(mask&&count==9),"overflow compares ninth Y on dot 130 with either render bit");
        until(p,*m,320);check(p.next_sprite_count==(mask?std::min(count,8u):0),"first eight sprites selected");
        if(mask)for(unsigned i=0;i<p.next_sprite_count;++i)check(p.next_sprites[i].slot==i,"sprite identity remains primary OAM order");
    }
    // Independent diagonal coordinates: ninth Y misses, then tile/flags/X/Y.
    for(unsigned offset:{37u,42u,47u,48u,53u}){
        Ppu p;setup(p,8);p.oam[offset]=100;const unsigned matchdot=130+2*(offset/4-8);
        until(p,*m,matchdot-1);check(!(p.status&32),"diagonal false positive not set before comparison");p.tick(*m);
        check(p.status&32,"diagonal tile/attribute/X false positive occurs at comparison");
    }
    Ppu negative;setup(negative,8);negative.oam[36]=100;until(negative,*m,256);
    check(!(negative.status&32),"diagonal search misses real ninth sprite whose tile is out of range");
    Ppu shifted;setup(shifted,8,100,1);shifted.oam[41]=100;until(shifted,*m,134);check(shifted.status&32,"diagonal search works at nonzero primary slot");
    Ppu end;setup(end,8,100,56);end.oam[1]=end.oam[2]=end.oam[3]=100;until(end,*m,256);
    check(!(end.status&32)&&end.eval_done,"primary search stops at 64 and never wraps");
    for(unsigned y:{239u,240u,255u}){
        Ppu p;setup(p,9,239);for(unsigned i=0;i<9;++i)p.oam[i*4]=std::uint8_t(y);until(p,*m,256);
        check(bool(p.status&32)==(y==239),"visible last scanline comparison does not wrap Y");
    }
    Ppu large;setup(large,9);for(unsigned i=0;i<9;++i)large.oam[i*4]=90;
    until(large,*m,64);large.ctrl=32;until(large,*m,130);check(large.status&32,"16-pixel height sampled during evaluation");
    large.mask=0;large.tick(*m);large.read_register(*m,2);check(large.status&32,"disable and status read preserve overflow");
    large.scanline=241;large.dot=1;large.tick(*m);check(large.status&32,"VBL start preserves overflow");
    large.scanline=261;large.dot=0;large.tick(*m);check(large.status&32,"overflow not cleared before pre-render dot 1");large.tick(*m);check(!(large.status&32),"pre-render dot 1 clears overflow even with rendering off");
    Ppu read;setup(read,1);until(read,*m,65);read.oam[0]=255;read.tick(*m);
    check(read.secondary_oam[0]==100&&read.eval_m==1,"even evaluation uses prior odd read latch");
    read.oam[1]=7;read.oam[2]=0x80;read.oam[3]=11;until(read,*m,256);read.oam[1]=99;
    until(read,*m,257);check(read.next_sprites[0].slot==0&&read.next_sprites[0].tile==7&&read.next_sprites[0].x==11&&read.next_sprites[0].pattern==7*16+7,"fetch unit uses secondary OAM and vertical flip");
    until(read,*m,263);check(read.next_sprites[0].fetch&&read.next_sprites[0].high_fetch>read.next_sprites[0].fetch,"sprite low/high fetch identities dot ordered");
    Ppu changed;setup(changed,9);until(changed,*m,128);changed.oam[32]=255;until(changed,*m,256);check(!(changed.status&32),"live primary OAM change before read prevents overflow");
    Ppu disabled;setup(disabled,9);until(disabled,*m,128);disabled.mask=0;until(disabled,*m,256);check(!(disabled.status&32),"disable before compare prevents future overflow");
}
void serialization(){
    Machine m(rom(1,4,0));auto original=m.serialize();
    auto changed=[&](auto& field){auto saved=field;
        if constexpr(std::is_same_v<std::remove_reference_t<decltype(field)>,bool>)field=!saved;
        else field=static_cast<std::remove_reference_t<decltype(field)>>(saved+1);
        check(original!=m.serialize(),"M3 hidden timing state enters canonical bytes");field=saved;check(original==m.serialize(),"timing field restoration restores bytes");};
    changed(m.ppu.eval_n);changed(m.ppu.eval_m);changed(m.ppu.eval_address);changed(m.ppu.eval_latch);changed(m.ppu.eval_count);changed(m.ppu.eval_tail);changed(m.ppu.eval_done);
    for(auto& value:m.ppu.secondary_oam)changed(value);for(auto& value:m.ppu.secondary_slots)changed(value);
    changed(m.controller_read_held);changed(m.controller_read_address);changed(m.controller_read_bit);
    auto a=load_cartridge(rom(1,4,0)),b=load_cartridge(rom(1,4,0));
    a->cpu_write_cycle(0xe000,1,20);b->cpu_write_cycle(0xe000,1,21);
    check(encoded(*a)!=encoded(*b),"MMC1 last observed write cycle is canonical");
    b->cpu_write(0xe000,1);check(encoded(*a)!=encoded(*b),"partial serial register is canonical");
    for(unsigned stop:{2u,65u,66u,128u,129u,130u,132u,257u,262u,319u}){
        Ppu p;setup(p,9);until(p,*a,stop);Ppu q=p;check(encoded(p)==encoded(q),"mid-evaluation state copy stable");
        for(unsigned i=0;i<400;++i){p.tick(*a);q.tick(*a);}check(encoded(p)==encoded(q),"mid-clear/evaluation/fetch deterministic continuation");
    }
    for(unsigned id:{1u,2u,3u,7u}){
        auto r=rom(id,id==3?2:4,id==3?4:0);Machine first(r),second(r);
        for(auto* machine:{&first,&second}){
            machine->cpu.pc=0x300;machine->ram[0x300]=0x4c;machine->ram[0x301]=0;machine->ram[0x302]=3;
            machine->ppu.mask=0x1e;machine->ppu.oam.fill(255);machine->ppu.oam[0]=40;
            machine->controllers[0].buttons=0x51;machine->write(0x4016,1);machine->write(0x4016,0);
            request(*machine);machine->read(0x4016);machine->run_frames(2);
        }
        const auto before=first.serialize();auto observation=observe(*first.frame());
        for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD})render(*observation,mode);
        check(before==first.serialize()&&before==second.serialize(),"presentation cannot mutate new mapper/PPU/controller state");
        first.run_frames(1);second.run_frames(1);check(first.serialize()==second.serialize()&&first.audio_hash()==second.audio_hash(),"continued execution invariant after presentation");
    }
}
}
int main(){try{mmc1();discrete();provenance();controller();sprites();serialization();}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;}
