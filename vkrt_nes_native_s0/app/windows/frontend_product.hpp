#pragma once
#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/state.hpp"
#include <filesystem>

namespace vkrt::nes::frontend {
// The caller supplies the root. Probes never resolve the user's known folders.
Bytes read_product_file(const std::filesystem::path&,std::size_t cap);
void atomic_product_write(const std::filesystem::path&,std::span<const std::uint8_t>);
class ProductStore {
public:
    explicit ProductStore(std::filesystem::path root):root_(std::move(root)) {}
    std::filesystem::path state_path(const Machine&,unsigned slot) const;
    std::filesystem::path battery_path(const Machine&) const;
    void save_state(const Machine&,unsigned slot) const;
    bool load_state(Machine&,unsigned slot,std::string& error) const;
    void save_battery(const Machine&) const;
    bool load_battery(Machine&,std::string& error) const;
private:
    std::filesystem::path root_;
    void validate_root() const;
};
int product_probe(const std::filesystem::path& rom,const std::filesystem::path& output);
}
