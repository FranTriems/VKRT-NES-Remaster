#include "vkrt/nes_native_s0/boards.hpp"
#include <algorithm>
#include "vkrt/nes_native_s0/state.hpp"
#include <bit>
#include <stdexcept>
namespace vkrt::nes {
namespace {
class Board : public Mapper {
protected:
    CartridgeInfo info;
    Bytes prg, chr;
    std::array<std::uint8_t,8192> ram{};
    std::vector<std::uint64_t> sources, epochs;
    std::uint64_t source{}, epoch{};
    virtual std::size_t chr_offset(std::uint16_t a) const = 0;
    void snapshot_memory(StateArchive& a) {
        a.fields(epoch,ram);for(auto& v:epochs)a.field(v);
        if(info.chr_ram)for(auto& b:chr)a.field(b);
    }
    void encode_memory(Encoder& e) const {
        e.u16(info.mapper);e.u8(info.submapper);e.u8(info.nes2);e.u8(info.battery);e.u8(info.trainer);
        e.u8(static_cast<std::uint8_t>(info.mirroring));e.u8(info.chr_ram);e.u64(source);e.u64(epoch);
        e.u32(static_cast<std::uint32_t>(prg.size()));e.data(prg);
        e.u32(static_cast<std::uint32_t>(chr.size()));e.data(chr);e.data(ram);
        for(auto value:epochs)e.u64(value);
    }
public:
    std::span<std::uint8_t> battery_ram() override { return info.battery?std::span<std::uint8_t>(ram):std::span<std::uint8_t>{}; }
    Board(std::span<const std::uint8_t> rom,CartridgeInfo c):info(c),source(identity_hash(rom)) {
        const auto start=16+(c.trainer?512:0);
        prg.assign(rom.begin()+start,rom.begin()+start+c.prg_size);
        if(c.chr_ram)chr.resize(8192);else chr.assign(rom.begin()+start+c.prg_size,rom.end());
        if(c.trainer)std::copy_n(rom.begin()+16,512,ram.begin()+0x1000);
        for(unsigned bank=0;bank<chr.size()/1024;++bank){
            Encoder e;e.u64(source);e.u32(bank);sources.push_back(identity_hash(e.bytes));
        }
        epochs.resize(sources.size());
    }
    std::uint8_t ppu_read(std::uint16_t a) const override {return chr[chr_offset(a)];}
    void ppu_write(std::uint16_t a,std::uint8_t b) override {
        auto offset=chr_offset(a);
        if(info.chr_ram&&chr[offset]!=b){chr[offset]=b;++epoch;++epochs[offset/1024];}
    }
    void ppu_fetch(std::uint16_t,std::uint64_t,FetchKind) override {}
    std::uint64_t chr_source() const override {return source;}
    std::uint64_t chr_epoch() const override {return epoch;}
    std::uint64_t chr_source_at(std::uint16_t a) const override {return sources[chr_offset(a)/1024];}
    std::uint64_t chr_epoch_at(std::uint16_t a) const override {
        auto bank=chr_offset(a)/1024;
        return (std::uint64_t(bank)<<48)|(epochs[bank]&0x0000ffffffffffffULL);
    }
};
class Mmc1 final : public Board {
    std::uint8_t shift{0x10}, control{0x0c}, chr0{}, chr1{}, bank{};
    std::uint64_t last_write{};
    bool write_seen{};
    std::size_t chr_offset(std::uint16_t a) const override {
        const unsigned selected=(control&16)?((a&0x1000)?chr1:chr0):((chr0&0x1e)+((a>>12)&1));
        return (selected%(chr.size()/4096))*4096+(a&4095);
    }
public:
    using Board::Board;
    std::unique_ptr<Mapper> clone() const override { return std::make_unique<Mmc1>(*this); }
    void snapshot(StateArchive& a) override {
        snapshot_memory(a);a.fields(shift,control,chr0,chr1,bank,last_write,write_seen);
        a.require(shift>0&&shift<=31&&control<=31&&chr0<=31&&chr1<=31&&bank<=31);
    }
    std::uint8_t cpu_read(std::uint16_t a) const override {
        if(a<0x6000)return 0;
        if(a<0x8000)return (bank&16)?0:ram[a-0x6000];
        const auto count=prg.size()/16384;
        unsigned selected=bank&15;
        switch((control>>2)&3){
        case 0:case 1:selected=(selected&14)+((a>>14)&1);break;
        case 2:if(a<0xc000)selected=0;break;
        case 3:if(a>=0xc000)selected=static_cast<unsigned>(count-1);break;
        }
        return prg[(selected%count)*16384+(a&16383)];
    }
    // Untimed Mapper writes are independent register strobes (e.g. test pins).
    // Machine always uses cpu_write_cycle, never this untimed entry directly.
    void cpu_write(std::uint16_t a,std::uint8_t b) override {
        if(a<0x6000)return;
        if(a<0x8000){if(!(bank&16))ram[a-0x6000]=b;return;}
        if(b&128){shift=0x10;control|=0x0c;return;}
        const bool complete=shift&1;
        shift=std::uint8_t((shift>>1)|((b&1)<<4));
        if(complete){
            switch((a>>13)&3){case 0:control=shift;break;case 1:chr0=shift;break;case 2:chr1=shift;break;case 3:bank=shift;break;}
            shift=0x10;
        }
    }
    void cpu_write_cycle(std::uint16_t a,std::uint8_t b,std::uint64_t cycle) override {
        if(a<0x8000){cpu_write(a,b);return;}
        const bool consecutive=write_seen&&cycle-last_write==1;
        last_write=cycle;write_seen=true;
        if((b&128)||!consecutive)cpu_write(a,b);
    }
    Mirroring mirroring() const override {
        constexpr std::array<Mirroring,4> modes{Mirroring::one_screen_lower,Mirroring::one_screen_upper,Mirroring::vertical,Mirroring::horizontal};
        return modes[control&3];
    }
    void encode(Encoder& e) const override {
        encode_memory(e);e.u8(shift);e.u8(control);e.u8(chr0);e.u8(chr1);e.u8(bank);e.u64(last_write);e.u8(write_seen);
    }
};
class Discrete final : public Board {
    std::uint8_t bank{};
    bool conflict;
    std::size_t chr_offset(std::uint16_t a) const override {
        const auto selected=info.mapper==3?(bank&3):0;
        return (selected%(chr.size()/8192))*8192+(a&8191);
    }
public:
    std::unique_ptr<Mapper> clone() const override { return std::make_unique<Discrete>(*this); }
    void snapshot(StateArchive& a) override { snapshot_memory(a);a.field(bank); }
    Discrete(std::span<const std::uint8_t> rom,CartridgeInfo c):Board(rom,c),
        conflict(c.submapper==2||(c.mapper==3&&c.submapper==0)) {}
    std::uint8_t cpu_read(std::uint16_t a) const override {
        if(a<0x6000)return 0;
        if(a<0x8000)return ram[a-0x6000];
        if(info.mapper==2){
            const auto count=prg.size()/16384;
            const auto selected=a<0xc000?(bank&15)%count:count-1;
            return prg[selected*16384+(a&16383)];
        }
        if(info.mapper==7)return prg[((bank&7)%(prg.size()/32768))*32768+(a&32767)];
        return prg[(a-0x8000)%prg.size()];
    }
    void cpu_write(std::uint16_t a,std::uint8_t b) override {
        if(a<0x6000)return;
        if(a<0x8000){ram[a-0x6000]=b;return;}
        // ROM drive is resolved using the OLD bank and the actual write address.
        bank=conflict?std::uint8_t(b&cpu_read(a)):b;
    }
    Mirroring mirroring() const override {
        if(info.mapper==7)return (bank&16)?Mirroring::one_screen_upper:Mirroring::one_screen_lower;
        return info.mirroring;
    }
    void encode(Encoder& e) const override {encode_memory(e);e.u8(bank);e.u8(conflict);}
};
}
std::unique_ptr<Mapper> load_baseline_board(std::span<const std::uint8_t> rom,CartridgeInfo c) {
    if(!std::has_single_bit(c.prg_size)||(c.chr_size&&!std::has_single_bit(c.chr_size)))
        throw std::invalid_argument("baseline board requires power-of-two ROM sizes");
    if(c.mapper==1){
        if(c.submapper||c.prg_size>256*1024||c.chr_size>128*1024||c.mirroring==Mirroring::four_screen)
            throw std::invalid_argument("baseline MMC1: submapper 0, up to 256 KiB PRG / 128 KiB CHR, no outer wiring");
        return std::make_unique<Mmc1>(rom,c);
    }
    if(c.mapper==2){
        if(c.submapper>2||c.prg_size<32768||c.prg_size>256*1024||!c.chr_ram)
            throw std::invalid_argument("baseline UxROM: submapper 0/1/2, 32..256 KiB PRG, CHR RAM");
    }else if(c.mapper==3){
        if(c.submapper>2||c.prg_size>32768||!c.chr_size||c.chr_size>32768)
            throw std::invalid_argument("baseline CNROM: submapper 0/1/2, 16/32 KiB PRG, 8..32 KiB CHR ROM");
    }else if(c.mapper==7){
        if(c.submapper||c.prg_size<32768||c.prg_size>256*1024||!c.chr_ram||c.mirroring==Mirroring::four_screen)
            throw std::invalid_argument("baseline AxROM: submapper 0, 32..256 KiB PRG, CHR RAM, one screen");
    }else throw std::invalid_argument("unsupported baseline mapper");
    return std::make_unique<Discrete>(rom,c);
}
}
