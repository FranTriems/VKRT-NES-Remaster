#include "vkrt/nes_native_s0/mmc3.hpp"
#include <algorithm>
#include "vkrt/nes_native_s0/state.hpp"
#include <stdexcept>
namespace vkrt::nes {
Mmc3::Mmc3(std::span<const std::uint8_t> rom,CartridgeInfo c):info(c) {
    if(c.mapper!=4||c.submapper!=0||c.prg_size<32768||c.prg_size>512*1024||c.chr_size>256*1024)
        throw std::invalid_argument("bounded MMC3B: 32..512 KiB PRG, 0..256 KiB CHR, submapper 0");
    auto begin=16+(c.trainer?512:0);
    prg.assign(rom.begin()+begin,rom.begin()+begin+c.prg_size);
    if(c.chr_ram)chr.resize(8192);else chr.assign(rom.begin()+begin+c.prg_size,rom.end());
    if(c.trainer)std::copy_n(rom.begin()+16,512,ram.begin()+0x1000);
    state.mirror=c.mirroring;source=identity_hash(rom);
    for(unsigned bank=0;bank<chr.size()/1024;++bank){Encoder e;e.u64(source);e.u32(bank);bank_sources.push_back(identity_hash(e.bytes));}
}
std::uint8_t Mmc3::cpu_read(std::uint16_t a) const {
    if(a>=0x8000){
        const auto count=prg.size()/8192;
        std::array<std::size_t,4> banks{std::size_t(state.banks[6]&63),std::size_t(state.banks[7]&63),count-2,count-1};
        if(state.select&64)std::swap(banks[0],banks[2]);
        return prg[(banks[(a-0x8000)/8192]%count)*8192+(a&8191)];
    }
    if(a>=0x6000&&(state.ram_control&128))return ram[a-0x6000];
    return 0; // Deterministic disabled-RAM bus value; no analog open-bus claim.
}
void Mmc3::cpu_write(std::uint16_t a,std::uint8_t b) {
    if(a<0x6000)return;
    if(a<0x8000){if((state.ram_control&0xc0)==0x80)ram[a-0x6000]=b;return;}
    switch(a&0xe001){
    case 0x8000:state.select=b;break;
    case 0x8001:{auto r=state.select&7;state.banks[r]=b;
        if(r<6)++state.events.chr_bank_writes;else ++state.events.prg_bank_writes;break;}
    case 0xa000:if(info.mirroring!=Mirroring::four_screen)state.mirror=(b&1)?Mirroring::horizontal:Mirroring::vertical;break;
    case 0xa001:state.ram_control=b;break;
    case 0xc000:state.latch=b;break;
    case 0xc001:state.counter=0;state.reload=true;break;
    case 0xe000:state.enabled=false;if(state.pending)++state.events.irq_acknowledgements;state.pending=false;break;
    case 0xe001:state.enabled=true;break;
    }
}
std::size_t Mmc3::chr_offset(std::uint16_t a) const {
    unsigned slot=(a&8191)/1024;
    if(state.select&128)slot^=4;
    unsigned bank=slot<4?unsigned(state.banks[slot/2]&0xfe)+(slot&1):state.banks[slot-2];
    return (bank%(chr.size()/1024))*1024+(a&1023);
}
std::uint8_t Mmc3::ppu_read(std::uint16_t a) const {return chr[chr_offset(a)];}
void Mmc3::ppu_write(std::uint16_t a,std::uint8_t b){
    auto offset=chr_offset(a);
    if(info.chr_ram&&chr[offset]!=b){chr[offset]=b;++state.chr_write_epoch;}
}
std::uint64_t Mmc3::chr_source_at(std::uint16_t a) const {return bank_sources[chr_offset(a)/1024];}
std::uint64_t Mmc3::chr_epoch_at(std::uint16_t a) const {
    // High-plane identity also binds its physical bank, including a switch between planes.
    return (std::uint64_t(chr_offset(a)/1024)<<48)|(state.chr_write_epoch&0x0000ffffffffffffULL);
}
void Mmc3::clock_irq(){
    ++state.events.qualified_a12;
    if(state.counter==0||state.reload)state.counter=state.latch;else --state.counter;
    state.reload=false;
    if(state.counter==0&&state.enabled){if(!state.pending)++state.events.irq_assertions;state.pending=true;}
}
void Mmc3::ppu_fetch(std::uint16_t a,std::uint64_t,FetchKind){
    const bool high=(a&0x1000)!=0; // A13 does not gate the physical A12 pin.
    if(high&&!state.a12){if(state.low_m2_edges==3)clock_irq();else ++state.events.rejected_a12;}
    if(high||state.a12)state.low_m2_edges=0;
    state.a12=high;
}
void Mmc3::m2_falling(){
    if(!state.a12&&state.low_m2_edges<3)++state.low_m2_edges;
}
std::unique_ptr<Mapper> Mmc3::clone() const { return std::make_unique<Mmc3>(*this); }
void Mmc3::snapshot(StateArchive& a) {
    a.field(ram);if(info.chr_ram)for(auto& b:chr)a.field(b);
    a.fields(state.banks,state.select,state.ram_control,state.latch,state.counter,state.low_m2_edges,
        state.reload,state.enabled,state.pending,state.a12,state.mirror,state.chr_write_epoch);
    auto& e=state.events;
    a.fields(e.irq_assertions,e.irq_acknowledgements,e.prg_bank_writes,e.chr_bank_writes,e.qualified_a12,e.rejected_a12);
    a.require(state.low_m2_edges<=3 && (state.mirror==Mirroring::horizontal||state.mirror==Mirroring::vertical||state.mirror==Mirroring::four_screen));
    a.require((info.mirroring==Mirroring::four_screen)==(state.mirror==Mirroring::four_screen));
}
void Mmc3::encode(Encoder& e) const {
    e.u16(info.mapper);e.u8(info.submapper);e.u8(info.nes2);e.u8(info.battery);e.u8(info.trainer);
    e.u8(static_cast<std::uint8_t>(info.mirroring));e.u8(info.chr_ram);e.u64(source);
    e.u32(static_cast<std::uint32_t>(prg.size()));e.data(prg);
    e.u32(static_cast<std::uint32_t>(chr.size()));e.data(chr);e.data(ram);
    e.data(state.banks);e.u8(state.select);e.u8(state.ram_control);e.u8(state.latch);e.u8(state.counter);
    e.u8(state.low_m2_edges);e.u8(state.reload);e.u8(state.enabled);e.u8(state.pending);e.u8(state.a12);
    e.u8(static_cast<std::uint8_t>(state.mirror));e.u64(state.chr_write_epoch);
    const auto& s=state.events;e.u64(s.irq_assertions);e.u64(s.irq_acknowledgements);
    e.u64(s.prg_bank_writes);e.u64(s.chr_bank_writes);e.u64(s.qualified_a12);e.u64(s.rejected_a12);
}
}
