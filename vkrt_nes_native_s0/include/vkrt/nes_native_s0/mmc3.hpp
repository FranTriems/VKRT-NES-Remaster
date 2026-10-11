#pragma once
#include "vkrt/nes_native_s0/core.hpp"
namespace vkrt::nes {
// Value-only diagnostic snapshot. No presentation component receives a Mapper.
struct Mmc3State {
    std::array<std::uint8_t,8> banks{};
    std::uint8_t select{}, ram_control{0x80}, latch{}, counter{}, low_m2_edges{};
    bool reload{}, enabled{}, pending{}, a12{};
    Mirroring mirror{};
    std::uint64_t chr_write_epoch{};
    MapperStats events{};
};
class Mmc3 final : public Mapper {
public:
    Mmc3(std::span<const std::uint8_t>, CartridgeInfo);
    std::uint8_t cpu_read(std::uint16_t) const override;
    void cpu_write(std::uint16_t, std::uint8_t) override;
    std::uint8_t ppu_read(std::uint16_t) const override;
    void ppu_write(std::uint16_t, std::uint8_t) override;
    void ppu_fetch(std::uint16_t, std::uint64_t, FetchKind) override;
    void m2_falling() override;
    Mirroring mirroring() const override { return state.mirror; }
    std::uint64_t chr_source() const override { return source; }
    std::uint64_t chr_epoch() const override { return state.chr_write_epoch; }
    std::uint64_t chr_source_at(std::uint16_t) const override;
    std::uint64_t chr_epoch_at(std::uint16_t) const override;
    bool irq_line() const override { return state.pending; }
    MapperStats stats() const override { return state.events; }
    Mmc3State snapshot() const { return state; }
    void encode(Encoder&) const override;
    std::unique_ptr<Mapper> clone() const override;
    void snapshot(StateArchive&) override;
    std::span<std::uint8_t> battery_ram() override { return info.battery?std::span<std::uint8_t>(ram):std::span<std::uint8_t>{}; }
private:
    CartridgeInfo info;
    Bytes prg, chr;
    std::array<std::uint8_t,8192> ram{};
    std::vector<std::uint64_t> bank_sources;
    std::uint64_t source{};
    Mmc3State state;
    std::size_t chr_offset(std::uint16_t) const;
    void clock_irq();
};
}
