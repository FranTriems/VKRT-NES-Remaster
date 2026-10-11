#pragma once
// VKRT public-experimental: bounded deterministic NTSC machine.
#include "vkrt/nes_native_s0/semantic.hpp"
#include "vkrt/nes_native_s0/modern_provenance.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace vkrt::nes {
class StateArchive;
enum class Mirroring : std::uint8_t { horizontal, vertical, four_screen, one_screen_lower, one_screen_upper };
struct CartridgeInfo {
    bool nes2{}, trainer{}, battery{}, chr_ram{};
    std::uint16_t mapper{};
    std::uint8_t submapper{};
    Mirroring mirroring{};
    std::uint32_t prg_size{}, chr_size{};
};
CartridgeInfo inspect_ines(std::span<const std::uint8_t>);
enum class FetchKind : std::uint8_t { cpu_port, nametable, attribute, pattern_low, pattern_high, sprite, dummy };
struct MapperStats {
    std::uint64_t irq_assertions{}, irq_acknowledgements{}, prg_bank_writes{}, chr_bank_writes{};
    std::uint64_t qualified_a12{}, rejected_a12{};
};
class Mapper {
public:
    virtual ~Mapper() = default;
    virtual std::uint8_t cpu_read(std::uint16_t) const = 0;
    virtual void cpu_write(std::uint16_t, std::uint8_t) = 0;
    // Actual bus-cycle start, including both writes of a CPU RMW instruction.
    virtual void cpu_write_cycle(std::uint16_t a, std::uint8_t b, std::uint64_t) { cpu_write(a,b); }
    virtual std::uint8_t ppu_read(std::uint16_t) const = 0;
    virtual void ppu_write(std::uint16_t, std::uint8_t) = 0;
    // External PPU address bus activity; the address holds between events.
    virtual void ppu_fetch(std::uint16_t, std::uint64_t, FetchKind) = 0;
    virtual void m2_falling() {} // Ordered independently of PPU address edges.
    virtual Mirroring mirroring() const = 0;
    virtual std::uint64_t chr_source() const = 0;
    virtual std::uint64_t chr_epoch() const = 0;
    virtual std::uint64_t chr_source_at(std::uint16_t) const { return chr_source(); }
    virtual std::uint64_t chr_epoch_at(std::uint16_t) const { return chr_epoch(); }
    virtual MapperStats stats() const { return {}; }
    virtual bool irq_line() const { return false; } // Mapper IRQ wired independently of presentation.
    virtual void encode(Encoder&) const = 0;
    virtual std::unique_ptr<Mapper> clone() const;
    virtual void snapshot(StateArchive&);
    virtual std::span<std::uint8_t> battery_ram() { return {}; }
};
std::unique_ptr<Mapper> load_cartridge(std::span<const std::uint8_t>);
struct Controller {
    std::uint8_t buttons{}, latched{}, position{};
    bool strobe{};
    void write(std::uint8_t);
    std::uint8_t read();
    void encode(Encoder&) const;
};
struct Cpu {
    std::uint8_t a{}, x{}, y{}, sp{0xfd}, p{0x24};
    std::uint16_t pc{};
    bool nmi_pending{}, irq_line{}, halted{};
    bool irq_pending{}, poll_valid{}, irq_sample{}, irq_previous{};
    bool nmi_latched{}, nmi_previous{}, nmi_sampled_line{};
    std::uint8_t fault_opcode{};
    std::uint16_t fault_pc{};
};
struct BusEvent {
    std::uint64_t cycle{};
    std::uint16_t address{};
    std::uint8_t value{};
    bool write{}, dummy{};
    bool operator==(const BusEvent&) const = default;
};
struct Pulse {
    std::array<std::uint8_t,4> reg{};
    std::uint16_t timer{}, period{};
    std::uint8_t sequence{}, length{}, envelope{}, envelope_divider{}, sweep_divider{};
    bool envelope_start{}, sweep_reload{}, enabled{};
};
struct Dmc {
    std::array<std::uint8_t,4> reg{};
    std::uint16_t timer{}, current_address{0xc000}, bytes_remaining{};
    std::uint8_t sample_buffer{}, shift_register{}, bits_remaining{8}, output{};
    bool sample_buffer_empty{true}, silence{true}, enabled{}, irq{}, fetch_pending{};
    std::uint8_t fetch_delay{};
    std::uint64_t fetches{}, stall_cycles{};
};
class Apu {
public:
    std::array<Pulse,2> pulse{};
    std::array<std::uint8_t,4> triangle{}, noise{};
    std::uint16_t triangle_timer{}, triangle_period{}, noise_timer{}, lfsr{1};
    std::uint8_t triangle_sequence{}, triangle_length{}, linear_counter{}, noise_length{}, noise_envelope{}, noise_divider{};
    bool triangle_enabled{}, noise_enabled{}, linear_reload{}, noise_start{}, five_step{}, irq_inhibit{}, frame_irq{};
    Dmc dmc{};
    std::uint32_t frame_cycle{}, sample_phase{};
    std::uint64_t cycles{};
    std::uint8_t frame_reset_delay{};
    std::vector<std::int16_t> pcm;
    void write(std::uint16_t, std::uint8_t);
    std::uint8_t status();
    void tick();
    bool dmc_needs_fetch() const { return dmc.fetch_pending; }
    std::uint16_t dmc_fetch_address() const { return dmc.current_address; }
    void dmc_supply(std::uint8_t);
    void dmc_note_stall(unsigned cycles) { dmc.stall_cycles+=cycles; }
    void encode(Encoder&) const;
private:
    void quarter();
    void half();
};
struct SpriteUnit {
    std::uint8_t slot{255}, x{}, y{}, tile{}, flags{}, low{}, high{};
    std::uint16_t pattern{};
    std::uint64_t source{}, epoch{}, high_epoch{}, fetch{}, high_fetch{};
    PatternObservation appearance;
    bool appearance_unstable{};
};
class Ppu {
public:
    std::array<std::uint8_t,4096> nametables{};
    std::array<std::uint8_t,32> palette{};
    std::array<std::uint8_t,256> oam{};
    std::uint8_t ctrl{}, mask{}, status{}, oam_address{}, read_buffer{}, io_latch{}, fine_x{};
    std::uint16_t v{}, t{}, scanline{261}, dot{};
    bool write_toggle{}, odd{}, nmi_line{}, nmi_edge{};
    bool suppress_vblank{}, skip_rendering{};
    std::uint64_t ticks{}, frame_number{};
    std::uint8_t next_tile{}, next_attribute{}, next_low{}, next_high{};
    std::uint16_t next_nt{}, next_at{}, next_pattern{};
    std::uint64_t next_chr_source{}, next_chr_epoch{}, next_high_epoch{}, next_fetch{}, next_high_fetch{};
    std::array<std::uint8_t,16> bg_pixel{}, bg_palette{};
    std::array<Source,16> bg_source{};
    std::array<SpriteUnit,8> sprites{}, next_sprites{};
    std::uint8_t sprite_count{}, next_sprite_count{};
    std::array<std::uint8_t,32> secondary_oam{};
    std::array<std::uint8_t,8> secondary_slots{};
    std::uint8_t eval_n{}, eval_m{}, eval_address{}, eval_latch{}, eval_count{}, eval_tail{};
    bool eval_done{};
    Frame working;
    ImmutableFrame completed;
    bool capture_provenance{true}; // Observation policy only; excluded from state.
    std::uint8_t read_memory(Mapper&, std::uint16_t, FetchKind = FetchKind::cpu_port);
    void write_memory(Mapper&, std::uint16_t, std::uint8_t);
    std::uint8_t read_register(Mapper&, std::uint16_t);
    void write_register(Mapper&, std::uint16_t, std::uint8_t);
    void tick(Mapper&);
    void encode(Encoder&) const;
    void snapshot(StateArchive&);
private:
    // Read-only derived metadata. Never encoded and never read by gameplay.
    std::array<PatternObservation,512> pattern_cache_{};
    std::array<PatternObservation,16> bg_appearance_{};
    std::array<bool,16> bg_unstable_{};
    PatternObservation next_appearance_;
    bool next_unstable_{};
    std::uint32_t captured_pixels_{};
    std::shared_ptr<PpuProvenance> provenance_=std::make_shared<PpuProvenance>();
    PatternObservation pattern_observation(const Mapper&,std::uint16_t);
    void capture_span(const Sample&,const PatternObservation&,bool);
    void update_nmi();
    void increment_x();
    void increment_y();
    void evaluate_sprites(); // One PPU dot: clear, read, copy or overflow search.
    void load_sprite(unsigned);
    void compose();
};
class Machine {
public:
    explicit Machine(std::span<const std::uint8_t> rom);
    Cpu cpu;
    Ppu ppu;
    Apu apu;
    std::array<Controller,2> controllers{};
    std::array<std::uint8_t,2048> ram{};
    std::unique_ptr<Mapper> mapper;
    std::uint64_t cycles{};
    std::uint8_t open_bus{};
    bool dma_pending{};
    std::uint8_t dma_page{};
    std::uint32_t last_dma_stall{};
    // Canonical DMA phases: 0 idle, 1 halt, 2 dummy, 3 align-or-get, 4 get.
    std::uint8_t dmc_phase{};
    bool dma_active{}, oam_latch_valid{};
    std::uint16_t dma_index{}, halted_address{};
    std::uint8_t oam_latch{};
    // Controller /OE remains asserted across DMA halt/dummy/alignment reads.
    bool controller_read_held{};
    std::uint16_t controller_read_address{};
    std::uint8_t controller_read_bit{};
    bool trace_enabled{};
    std::vector<BusEvent> trace; // Bounded observation; excluded from state.
    void reset(); // Warm reset; power = fresh Machine.
    std::uint8_t read(std::uint16_t, bool dummy = false);
    void write(std::uint16_t, std::uint8_t);
    std::uint32_t step();
    void run_frames(unsigned count, std::uint64_t budget = 2000000);
    void tick();
    Bytes serialize() const;
    // Persistence is separate from the frozen diagnostic serialization above.
    Bytes save_state() const;
    bool load_state(std::span<const std::uint8_t>,std::string& error);
    Bytes save_battery() const;
    bool load_battery(std::span<const std::uint8_t>,std::string& error);
    bool has_battery() const;
    std::uint64_t rom_identity() const { return rom_identity_; }
    std::string state_hash() const;
    std::string audio_hash() const;
    ImmutableFrame frame() const { return ppu.completed; }
private:
    explicit Machine(std::unique_ptr<Mapper> copy):mapper(std::move(copy)) {}
    std::uint64_t rom_identity_{};
    CartridgeInfo cartridge_{};
    void snapshot(StateArchive&);
    Bytes envelope(std::span<const std::uint8_t>,bool battery) const;
    std::span<const std::uint8_t> payload(std::span<const std::uint8_t>,bool battery) const;
    void dma();
    void service_dma(std::uint16_t);
    std::uint8_t bus_read(std::uint16_t, bool);
};
Bytes synthetic_nrom();
}
