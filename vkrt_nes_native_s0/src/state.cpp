#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/state.hpp"
#include <algorithm>

namespace vkrt::nes {
std::unique_ptr<Mapper> Mapper::clone() const { throw std::runtime_error("Mapper has no snapshot support"); }
void Mapper::snapshot(StateArchive&) { throw std::runtime_error("Mapper has no snapshot support"); }
namespace {
void source(StateArchive& a,Source& s){
    a.fields(s.nametable,s.attribute,s.pattern,s.scroll_v,s.chr_source,s.chr_epoch,s.high_chr_epoch,
        s.fetch_epoch,s.high_fetch_epoch,s.tile,s.palette,s.local_x,s.local_y,s.flags,s.oam_slot,s.fine_x,s.origin_x,s.origin_y);
}
void pattern(StateArchive& a,PatternObservation& p){a.fields(p.shape.bits,p.source,p.epoch,p.valid);}
void sprite(StateArchive& a,SpriteUnit& s){
    a.fields(s.slot,s.x,s.y,s.tile,s.flags,s.low,s.high,s.pattern,s.source,s.epoch,s.high_epoch,s.fetch,s.high_fetch);
    pattern(a,s.appearance);a.field(s.appearance_unstable);
    a.require(s.pattern<8192&&(s.slot<64||s.slot==255));
}
void provenance(StateArchive& a,PpuProvenance& p){
    std::uint32_t size=static_cast<std::uint32_t>(p.spans.size());a.field(size);a.require(size<=pixels);
    if(a.reading())p.spans.resize(size);
    std::uint32_t end=0;
    for(auto& s:p.spans){
        a.fields(s.begin,s.count,s.shape.bits);source(a,s.source);a.fields(s.layer,s.unstable);
        a.require(s.begin<pixels&&s.begin>=end&&s.count>0&&s.count<=unsigned(width)-s.begin%width&&unsigned(s.layer)<=2);
        end=s.begin+s.count;
    }
    a.fields(p.palette,p.oam);
}
void native(StateArchive& a,std::shared_ptr<const PpuProvenance>& p){
    bool present=bool(p);a.field(present);
    if(a.reading()){
        p.reset();if(present){auto next=std::make_shared<PpuProvenance>();provenance(a,*next);p=std::move(next);}
    }else if(present)provenance(a,const_cast<PpuProvenance&>(*p));
    if(p){
        std::uint32_t next=0;
        for(const auto& s:p->spans){a.require(s.begin==next);next+=s.count;}
        a.require(next==pixels);
    }
}
void frame(StateArchive& a,Frame& f){
    a.fields(f.number,f.mapper_domain);
    a.require(f.indices.size()==pixels&&f.effects.size()==pixels&&f.samples.size()==pixels);
    for(auto& v:f.indices){a.field(v);a.require(v<=63);}
    for(auto& v:f.effects){a.field(v);a.require((v&30)==0);}
    for(auto& s:f.samples){
        a.fields(s.x,s.y,s.dot,s.color,s.emphasis,s.grayscale,s.bg_opaque,s.sprite_opaque,s.sprite_zero,s.sprite_behind,s.layer);
        source(a,s.background);source(a,s.sprite);
        a.fields(s.asset_key,s.surface_key,s.previous_surface,s.motion_x,s.motion_y,s.confidence,s.reactive);
        a.require(s.x<width&&s.y<height&&s.dot<=340&&s.color<=63&&s.emphasis<=7&&unsigned(s.layer)<=2&&unsigned(s.reactive)<=4);
    }
    native(a,f.native);
}
void apu_state(StateArchive& a,Apu& p){
    for(auto& v:p.pulse){
        a.fields(v.reg,v.timer,v.period,v.sequence,v.length,v.envelope,v.envelope_divider,v.sweep_divider,v.envelope_start,v.sweep_reload,v.enabled);
        a.require(v.period<=2047&&v.timer<=2047&&v.sequence<=7&&v.envelope<=15&&v.envelope_divider<=15&&v.sweep_divider<=7);
    }
    a.fields(p.triangle,p.noise,p.triangle_timer,p.triangle_period,p.noise_timer,p.lfsr,p.triangle_sequence,
        p.triangle_length,p.linear_counter,p.noise_length,p.noise_envelope,p.noise_divider,p.triangle_enabled,
        p.noise_enabled,p.linear_reload,p.noise_start,p.five_step,p.irq_inhibit,p.frame_irq);
    auto& d=p.dmc;
    a.fields(d.reg,d.timer,d.current_address,d.bytes_remaining,d.sample_buffer,d.shift_register,d.bits_remaining,d.output,
        d.sample_buffer_empty,d.silence,d.enabled,d.irq,d.fetch_pending,d.fetch_delay,d.fetches,d.stall_cycles);
    a.fields(p.frame_cycle,p.sample_phase,p.cycles,p.frame_reset_delay);a.vector(p.pcm,441000);
    a.require(p.triangle_sequence<=31&&p.triangle_timer<=2047&&p.triangle_period<=2047&&p.noise_timer<=4067&&p.lfsr<=32767);
    a.require(p.linear_counter<=127&&p.noise_envelope<=15&&p.noise_divider<=15&&p.sample_phase<1789773&&p.frame_cycle<37282&&p.frame_reset_delay<=5);
    a.require(d.bits_remaining>=1&&d.bits_remaining<=8&&d.output<=127&&d.fetch_delay<=3&&d.bytes_remaining<=4081&&d.timer<=427);
    a.require(d.current_address>=0x8000&&(!d.fetch_pending||d.bytes_remaining>0));
}
constexpr std::uint64_t state_magic=0x31545353454e4b56ULL, battery_magic=0x314d5253454e4b56ULL;
constexpr std::size_t header_size=44;
std::uint8_t board_flags(const CartridgeInfo& c){return std::uint8_t(unsigned(c.mirroring)|(c.nes2?8:0)|(c.battery?16:0)|(c.trainer?32:0)|(c.chr_ram?64:0));}
}
void Ppu::snapshot(StateArchive& a){
    a.fields(nametables,palette,oam,ctrl,mask,status,oam_address,read_buffer,io_latch,fine_x,v,t,scanline,dot,
        write_toggle,odd,nmi_line,nmi_edge,suppress_vblank,skip_rendering,ticks,frame_number,
        next_tile,next_attribute,next_low,next_high,next_nt,next_at,next_pattern,next_chr_source,next_chr_epoch,
        next_high_epoch,next_fetch,next_high_fetch,bg_pixel,bg_palette);
    for(auto& s:bg_source)source(a,s);
    for(auto& s:sprites)sprite(a,s);
    for(auto& s:next_sprites)sprite(a,s);
    a.fields(sprite_count,next_sprite_count,secondary_oam,secondary_slots,eval_n,eval_m,eval_address,eval_latch,eval_count,eval_tail,eval_done);
    frame(a,working);
    bool present=bool(completed);a.field(present);
    if(a.reading()){
        completed.reset();if(present){auto f=std::make_shared<Frame>();frame(a,*f);completed=std::move(f);}
    }else if(present)frame(a,const_cast<Frame&>(*completed));
    a.field(capture_provenance);
    for(auto& p:pattern_cache_)pattern(a,p);
    for(auto& p:bg_appearance_)pattern(a,p);
    a.field(bg_unstable_);pattern(a,next_appearance_);a.fields(next_unstable_,captured_pixels_);
    if(a.reading())provenance_=std::make_shared<PpuProvenance>();
    provenance(a,*provenance_);
    a.require(fine_x<8&&v<32768&&t<32768&&scanline<262&&dot<341&&next_attribute<4);
    a.require(sprite_count<=8&&next_sprite_count<=8&&eval_n<=64&&eval_m<4&&eval_address<=32&&eval_count<=8&&eval_tail<=3&&captured_pixels_<=pixels);
    a.require((eval_address<32||eval_count==8)&&next_pattern<8192);
    for(auto b:palette)a.require(b<=63);
    for(auto b:bg_pixel)a.require(b<4);
    for(auto b:bg_palette)a.require(b<4);
}
void Machine::snapshot(StateArchive& a){
    a.fields(cpu.a,cpu.x,cpu.y,cpu.sp,cpu.p,cpu.pc,cpu.nmi_pending,cpu.irq_line,cpu.halted,cpu.irq_pending,
        cpu.poll_valid,cpu.irq_sample,cpu.irq_previous,cpu.nmi_latched,cpu.nmi_previous,cpu.nmi_sampled_line,cpu.fault_opcode,cpu.fault_pc);
    a.fields(ram,cycles,open_bus,dma_pending,dma_page,last_dma_stall,dmc_phase,dma_active,oam_latch_valid,
        dma_index,halted_address,oam_latch,controller_read_held,controller_read_address,controller_read_bit);
    for(auto& c:controllers){a.fields(c.buttons,c.latched,c.position,c.strobe);a.require(c.position<=8);}
    mapper->snapshot(a);ppu.snapshot(a);apu_state(a,apu);
    a.require(dmc_phase<=4&&dma_index<=256&&controller_read_bit<=1);
    a.require(!dmc_phase||(apu.dmc.fetch_pending&&apu.dmc.bytes_remaining));
    a.require(!controller_read_held||controller_read_address==0x4016||controller_read_address==0x4017);
    a.require(ppu.working.mapper_domain==cartridge_.mapper&&(!ppu.completed||ppu.completed->mapper_domain==cartridge_.mapper));
}
Bytes Machine::envelope(std::span<const std::uint8_t> data,bool battery) const {
    if(data.size()>max_state_bytes-header_size)throw std::runtime_error("Save exceeds size limit");
    Encoder e;e.u64(battery?battery_magic:state_magic);e.u32(1);e.u64(rom_identity_);
    e.u16(cartridge_.mapper);e.u8(cartridge_.submapper);e.u8(board_flags(cartridge_));
    e.u32(cartridge_.prg_size);e.u32(cartridge_.chr_size);e.u32(static_cast<std::uint32_t>(data.size()));
    e.u64(identity_hash(data));e.data(data);return e.bytes;
}
std::span<const std::uint8_t> Machine::payload(std::span<const std::uint8_t> data,bool battery) const {
    if(data.size()<header_size||data.size()>max_state_bytes)throw std::runtime_error("Save is truncated or oversized");
    StateArchive a(data.first(header_size));
    std::uint64_t magic{},identity{},checksum{};std::uint32_t version{},prg{},chr{},size{};
    std::uint16_t mapper_id{};std::uint8_t sub{},flags{};
    a.fields(magic,version,identity,mapper_id,sub,flags,prg,chr,size,checksum);a.finish();
    a.require(magic==(battery?battery_magic:state_magic)&&version==1);
    if(identity!=rom_identity_)throw std::runtime_error("Save belongs to a different ROM");
    if(mapper_id!=cartridge_.mapper||sub!=cartridge_.submapper||flags!=board_flags(cartridge_)||prg!=cartridge_.prg_size||chr!=cartridge_.chr_size)
        throw std::runtime_error("Save board does not match this cartridge");
    a.require(size==data.size()-header_size);
    auto result=data.subspan(header_size);a.require(identity_hash(result)==checksum);return result;
}
Bytes Machine::save_state() const {
    Bytes data;data.reserve(24u*1024u*1024u);StateArchive a(data);
    // The writing archive never modifies fields; shared published frames stay immutable.
    const_cast<Machine*>(this)->snapshot(a);return envelope(data,false);
}
bool Machine::load_state(std::span<const std::uint8_t> data,std::string& error){
    try{
        const auto bytes=payload(data,false);
        auto next=std::unique_ptr<Machine>(new Machine(mapper->clone()));
        next->rom_identity_=rom_identity_;next->cartridge_=cartridge_;
        StateArchive a(bytes);next->snapshot(a);a.finish();
        next->trace_enabled=trace_enabled;
        // All validation/allocation precedes this noexcept move commit.
        static_assert(std::is_nothrow_move_assignable_v<Machine>);
        *this=std::move(*next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Machine::has_battery() const {return !mapper->battery_ram().empty();}
Bytes Machine::save_battery() const {
    const auto ram=mapper->battery_ram();if(ram.empty())return {};
    if(ram.size()!=8192)throw std::runtime_error("Unsupported battery RAM size");
    return envelope(ram,true);
}
bool Machine::load_battery(std::span<const std::uint8_t> data,std::string& error){
    try{
        const auto bytes=payload(data,true);auto ram=mapper->battery_ram();
        if(ram.size()!=8192||bytes.size()!=ram.size())throw std::runtime_error("Battery RAM size mismatch");
        std::copy(bytes.begin(),bytes.end(),ram.begin());error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
