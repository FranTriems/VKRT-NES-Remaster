#pragma once
#include "vkrt/nes_native_s0/core.hpp"
namespace vkrt::nes {
// Bounded MMC1 and discrete UxROM/CNROM/AxROM boards. No outer-bank wiring.
std::unique_ptr<Mapper> load_baseline_board(std::span<const std::uint8_t>, CartridgeInfo);
}
