#pragma once
// VKRT public-experimental: fixed-endian evidence serialization, no object dumps.
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace vkrt::nes {
using Bytes = std::vector<std::uint8_t>;
struct Encoder {
    Bytes bytes;
    void u8(std::uint8_t v) { bytes.push_back(v); }
    void u16(std::uint16_t v) { u8(v & 255); u8(v >> 8); }
    void u32(std::uint32_t v) { u16(v & 65535); u16(v >> 16); }
    void u64(std::uint64_t v) { u32(v & 0xffffffffu); u32(v >> 32); }
    void data(std::span<const std::uint8_t> v) { bytes.insert(bytes.end(), v.begin(), v.end()); }
};
std::string sha256(std::span<const std::uint8_t>);
std::uint64_t identity_hash(std::span<const std::uint8_t>);
}
