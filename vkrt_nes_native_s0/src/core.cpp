#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/mmc3.hpp"
#include "vkrt/nes_native_s0/boards.hpp"
#include <algorithm>
#include "vkrt/nes_native_s0/state.hpp"
#include <stdexcept>
namespace vkrt::nes {
CartridgeInfo inspect_ines(std::span<const std::uint8_t> rom) {
    if (rom.size()<16 || rom.size()>16+512+32*1024*1024u) throw std::invalid_argument("iNES size outside bounded envelope");
    if (rom[0]!='N'||rom[1]!='E'||rom[2]!='S'||rom[3]!=0x1a) throw std::invalid_argument("iNES magic");
    CartridgeInfo c;
    auto format=rom[7]&12;
    if(format!=0 && format!=8) throw std::invalid_argument("reserved iNES format");
    c.nes2=format==8;c.trainer=rom[6]&4;c.battery=rom[6]&2;
    c.mapper=(rom[6]>>4)|(rom[7]&0xf0);
    c.mirroring=(rom[6]&8)?Mirroring::four_screen:((rom[6]&1)?Mirroring::vertical:Mirroring::horizontal);
    if(rom[7]&3)throw std::invalid_argument("unsupported console type");
    std::uint32_t prg=rom[4],chr=rom[5];
    if(c.nes2) {
        c.mapper|=(rom[8]&15)<<8;c.submapper=rom[8]>>4;
        // Bounded architecture: exponent/multiplier sizes and extended RAM are explicit seams.
        if((rom[9]&15)==15 || (rom[9]>>4)==15)throw std::invalid_argument("NES2 exponent size unsupported");
        prg|=(rom[9]&15)<<8;chr|=(rom[9]>>4)<<8;
        if(rom[10]||rom[11]||rom[12]||rom[13]||rom[14]||rom[15])throw std::invalid_argument("NES2 extended RAM/timing/misc unsupported");
    } else {
        for(int i=9;i<16;++i)if(rom[i])throw std::invalid_argument("nonzero reserved iNES bytes / non-NTSC timing");
        if(rom[8]>1)throw std::invalid_argument("extended iNES RAM unsupported");
    }
    c.prg_size=prg*16384u;c.chr_size=chr*8192u;c.chr_ram=chr==0;
    if(!prg || c.prg_size>16*1024*1024u || c.chr_size>16*1024*1024u)throw std::invalid_argument("cartridge size limit");
    std::size_t expected=16+(c.trainer?512:0)+std::size_t(c.prg_size)+c.chr_size;
    if(rom.size()!=expected)throw std::invalid_argument("truncated or trailing iNES payload");
    return c;
}
namespace {
class Nrom final : public Mapper {
    CartridgeInfo info;
    Bytes prg,chr;
    std::array<std::uint8_t,8192> ram{};
    std::uint64_t epoch{}, source{};
public:
    Nrom(std::span<const std::uint8_t> rom,CartridgeInfo c):info(c) {
        source=identity_hash(rom); // Cartridge/CHR origin, independent of presentation and run.
        auto begin=16+(c.trainer?512:0);
        prg.assign(rom.begin()+begin,rom.begin()+begin+c.prg_size);
        if(c.chr_ram)chr.resize(8192);else chr.assign(rom.begin()+begin+c.prg_size,rom.end());
        if(c.trainer)std::copy_n(rom.begin()+16,512,ram.begin()+0x1000);
    }
    std::uint8_t cpu_read(std::uint16_t a)const override {
        if(a>=0x8000)return prg[(a-0x8000)%prg.size()];
        if(a>=0x6000)return ram[a-0x6000];
        return 0;
    }
    void cpu_write(std::uint16_t a,std::uint8_t b)override {if(a>=0x6000&&a<0x8000)ram[a-0x6000]=b;}
    std::uint8_t ppu_read(std::uint16_t a)const override {return chr[a&0x1fff];}
    void ppu_write(std::uint16_t a,std::uint8_t b)override {if(info.chr_ram&&chr[a&0x1fff]!=b){chr[a&0x1fff]=b;++epoch;}}
    void ppu_fetch(std::uint16_t,std::uint64_t,FetchKind)override {}
    Mirroring mirroring()const override{return info.mirroring;}
    std::uint64_t chr_source()const override{return source;}
    std::uint64_t chr_epoch()const override{return epoch;}
    std::unique_ptr<Mapper> clone() const override { return std::make_unique<Nrom>(*this); }
    std::span<std::uint8_t> battery_ram() override { return info.battery?std::span<std::uint8_t>(ram):std::span<std::uint8_t>{}; }
    void snapshot(StateArchive& a) override {
        a.fields(epoch,ram);if(info.chr_ram)for(auto& b:chr)a.field(b);
    }
    void encode(Encoder& e)const override {
        e.u16(info.mapper);e.u8(info.submapper);e.u8(info.nes2);e.u8(info.battery);e.u8(info.trainer);
        e.u8(static_cast<std::uint8_t>(info.mirroring));e.u8(info.chr_ram);e.u64(epoch);
        e.u32(static_cast<std::uint32_t>(prg.size()));e.data(prg);
        e.u32(static_cast<std::uint32_t>(chr.size()));e.data(chr);e.data(ram);
    }
};
}
std::unique_ptr<Mapper> load_cartridge(std::span<const std::uint8_t> rom) {
    auto c=inspect_ines(rom);
    if(c.mapper==4)return std::make_unique<Mmc3>(rom,c);
    if(c.mapper==1||c.mapper==2||c.mapper==3||c.mapper==7)return load_baseline_board(rom,c);
    if(c.mapper!=0||c.submapper!=0)throw std::invalid_argument("supported mappers: 0, 1, 2, 3, 4, 7 (bounded boards)");
    if((c.prg_size!=16384&&c.prg_size!=32768)||(c.chr_size!=0&&c.chr_size!=8192))throw std::invalid_argument("invalid NROM bank sizes");
    return std::make_unique<Nrom>(rom,c);
}
void Controller::write(std::uint8_t b) {
    if((b&1)||strobe){latched=buttons;position=0;}
    strobe=b&1;
}
std::uint8_t Controller::read() {
    if(strobe)return buttons&1;
    if(position>=8)return 1;
    return (latched>>position++)&1;
}
void Controller::encode(Encoder&e)const {e.u8(buttons);e.u8(latched);e.u8(position);e.u8(strobe);}
Machine::Machine(std::span<const std::uint8_t> rom):mapper(load_cartridge(rom)) {cartridge_=inspect_ines(rom);rom_identity_=identity_hash(rom);ppu.working.mapper_domain=cartridge_.mapper;cpu.sp=0;reset();}
void Machine::tick() {
    cpu.irq_previous=cpu.irq_sample;
    cpu.nmi_previous=cpu.nmi_latched;
    ++cycles;apu.tick();
    // NTSC fixed phase: PPU events at 4/8/12, falling M2 at 10.
    for(int i=0;i<3;++i){
        if(i==2)mapper->m2_falling();
        if(i==1){
            // Fixed NTSC input sampling slot, between the first two dots.
            // Only the sampled line transition enters the CPU edge latch.
            if(ppu.nmi_line&&!cpu.nmi_sampled_line)cpu.nmi_latched=true;
            cpu.nmi_sampled_line=ppu.nmi_line;ppu.nmi_edge=false;
        }
        ppu.tick(*mapper);
    }
    cpu.irq_line=apu.frame_irq||apu.dmc.irq||mapper->irq_line();
    cpu.irq_sample=cpu.irq_line;
}
std::uint8_t Machine::read(std::uint16_t a,bool dummy) {
    if(!dma_active&&(dma_pending||(apu.dmc_needs_fetch()&&!apu.dmc.fetch_delay)))service_dma(a);
    return bus_read(a,dummy);
}
std::uint8_t Machine::bus_read(std::uint16_t a,bool dummy) {
    const auto bus_cycle=cycles;
    std::uint8_t b=open_bus;
    if(a<0x2000)b=ram[a&0x7ff];
    else if(a<0x4000)b=ppu.read_register(*mapper,a&7);
    // The frame sequencer clocks before the CPU's status read strobe. A
    // coincident frame IRQ is visible to this read and is then acknowledged.
    else if(a==0x4015){
        tick();b=apu.status();
        // Acknowledge the live pin without retracting the earlier CPU sample.
        cpu.irq_line=apu.dmc.irq||mapper->irq_line();
    }
    else if(a==0x4016||a==0x4017){
        if(!controller_read_held||controller_read_address!=a)controller_read_bit=controllers[a&1].read();
        b=(open_bus&0xe0)|controller_read_bit;
    }
    else if(a>=0x6000)b=mapper->cpu_read(a);
    open_bus=b;
    // RDY holds the CPU address and controller /OE through setup clocks.
    // The DMC sample get deasserts /OE; the resumed CPU read clocks it again.
    controller_read_held=dma_active&&(a==0x4016||a==0x4017);
    controller_read_address=controller_read_held?a:0;
    if(!controller_read_held)controller_read_bit=0;
    if(trace_enabled){if(trace.size()>=1000000)throw std::length_error("trace limit");trace.push_back({bus_cycle,a,b,false,dummy});}
    if(a!=0x4015)tick();
    return b;
}
void Machine::write(std::uint16_t a,std::uint8_t b) {
    open_bus=b;
    if(a<0x2000)ram[a&0x7ff]=b;
    else if(a<0x4000)ppu.write_register(*mapper,a&7,b);
    else if(a==0x4014){dma_pending=true;dma_page=b;}
    else if(a==0x4016){controllers[0].write(b);controllers[1].write(b);}
    else if(a<=0x4017)apu.write(a,b);
    else if(a>=0x6000)mapper->cpu_write_cycle(a,b,cycles);
    controller_read_held=false;controller_read_address=0;controller_read_bit=0;
    if(trace_enabled){if(trace.size()>=1000000)throw std::length_error("trace limit");trace.push_back({cycles,a,b,true,false});}
    tick();
}
void Machine::reset() {
    cpu.halted=false;cpu.fault_opcode=0;cpu.fault_pc=0;cpu.nmi_pending=false;cpu.p=(cpu.p|0x24)&0xef;
    cpu.irq_pending=cpu.poll_valid=cpu.irq_sample=cpu.irq_previous=false;
    cpu.nmi_latched=cpu.nmi_previous=cpu.nmi_sampled_line=false;
    dma_pending=dma_active=oam_latch_valid=false;dmc_phase=0;dma_index=0;halted_address=0;oam_latch=0;
    controller_read_held=false;controller_read_address=0;controller_read_bit=0;
    read(cpu.pc,true);read(cpu.pc,true);
    for(int i=0;i<3;++i){read(0x100|cpu.sp,true);--cpu.sp;}
    auto lo=read(0xfffc);cpu.pc=lo|(std::uint16_t(read(0xfffd))<<8);
}
void Machine::dma() { service_dma(cpu.pc); }
void Machine::service_dma(std::uint16_t address) {
    const auto start=cycles;
    const bool oam=dma_pending;
    dma_active=true;halted_address=address;dma_index=0;oam_latch_valid=false;
    if(oam){
        // Halt on a CPU read; get phases precede the APU's put/clock phase.
        if(apu.dmc_needs_fetch()&&!apu.dmc.fetch_delay){dmc_phase=2;apu.dmc_note_stall(1);}
        bus_read(address,true);
    }
    while((oam&&dma_index<256)||dmc_phase||(apu.dmc_needs_fetch()&&!apu.dmc.fetch_delay)){
        const bool get=(cycles&1)==0;
        bool dmc_get=false;
        if(!dmc_phase&&apu.dmc_needs_fetch()&&!apu.dmc.fetch_delay)dmc_phase=1;
        if(dmc_phase){
            apu.dmc_note_stall(1);
            switch(dmc_phase){
            case 1:dmc_phase=2;break; // halt (CPU writes cannot enter this routine)
            case 2:dmc_phase=3;break; // mandatory dummy
            case 3:if(get){dmc_get=true;dmc_phase=0;}else dmc_phase=4;break;
            case 4:dmc_get=true;dmc_phase=0;break;
            default:throw std::logic_error("invalid DMA phase");
            }
        }
        if(dmc_get){
            auto sample=bus_read(apu.dmc_fetch_address(),false);
            apu.dmc_supply(sample);
        }else if(oam&&dma_index<256&&get){
            oam_latch=bus_read(std::uint16_t((dma_page<<8)|dma_index),false);oam_latch_valid=true;
        }else if(oam&&dma_index<256&&!get&&oam_latch_valid){
            write(0x2004,oam_latch);oam_latch_valid=false;++dma_index;
        }else bus_read(address,true);
    }
    if(oam){last_dma_stall=static_cast<std::uint32_t>(cycles-start);dma_pending=false;}
    dma_active=false;
}
void Machine::run_frames(unsigned count,std::uint64_t budget) {
    if(count>600||budget>20000000)throw std::invalid_argument("run budget limit");
    auto target=ppu.frame_number+count,start=cycles;
    while(ppu.frame_number<target){if(cycles-start>=budget)throw std::runtime_error("frame budget exhausted");step();}
}
Bytes Machine::serialize()const {
    Encoder e;e.u32(0x304e4b56);e.u32(3);
    e.u8(cpu.a);e.u8(cpu.x);e.u8(cpu.y);e.u8(cpu.sp);e.u8(cpu.p);e.u16(cpu.pc);
    e.u8(cpu.nmi_pending);e.u8(cpu.irq_line);e.u8(cpu.halted);e.u8(cpu.fault_opcode);e.u16(cpu.fault_pc);
    for(auto b:{cpu.irq_pending,cpu.poll_valid,cpu.irq_sample,cpu.irq_previous,cpu.nmi_latched,cpu.nmi_previous,cpu.nmi_sampled_line})e.u8(b);
    e.u8(dmc_phase);e.u8(dma_active);e.u8(oam_latch_valid);e.u16(dma_index);e.u16(halted_address);e.u8(oam_latch);
    e.u8(controller_read_held);e.u16(controller_read_address);e.u8(controller_read_bit);
    e.u64(cycles);e.u8(open_bus);e.u8(dma_pending);e.u8(dma_page);e.u32(last_dma_stall);e.data(ram);
    for(const auto& c:controllers)c.encode(e);
    mapper->encode(e);ppu.encode(e);apu.encode(e);return e.bytes;
}
std::string Machine::state_hash()const{return sha256(serialize());}
std::string Machine::audio_hash()const {Encoder e;for(auto s:apu.pcm)e.u16(static_cast<std::uint16_t>(s));return sha256(e.bytes);}
}
