#pragma once
#include "vkrt/nes_native_s0/hash.hpp"
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace vkrt::nes {
inline constexpr std::size_t max_state_bytes=48u*1024u*1024u;
// Explicit little-endian scalar archive. No native layouts, addresses or ROM data.
class StateArchive {
public:
    explicit StateArchive(Bytes& output):out_(&output) {}
    explicit StateArchive(std::span<const std::uint8_t> input):in_(input) {}
    bool reading() const { return !out_; }
    void require(bool ok) const { if(!ok)throw std::runtime_error("Invalid or unsupported save data"); }
    void finish() const { require(!reading()||position_==in_.size()); }
    template<class T> requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void field(T& value) {
        if constexpr(std::is_same_v<T,bool>){
            std::uint8_t b=value?1:0;field(b);require(b<=1);if(reading())value=b!=0;
        }else if constexpr(std::is_enum_v<T>){
            auto b=static_cast<std::underlying_type_t<T>>(value);field(b);if(reading())value=static_cast<T>(b);
        }else{
            using U=std::make_unsigned_t<T>;
            U bits=static_cast<U>(value);
            if(reading()){
                require(sizeof(T)<=in_.size()-position_);bits=0;
                for(unsigned i=0;i<sizeof(T);++i)bits|=U(U(in_[position_++])<<(8*i));
                value=std::bit_cast<T>(bits);
            }else{
                require(out_->size()+sizeof(T)<=max_state_bytes);
                for(unsigned i=0;i<sizeof(T);++i)out_->push_back(std::uint8_t(bits>>(8*i)));
            }
        }
    }
    template<class T,std::size_t N> void field(std::array<T,N>& a){for(auto& v:a)field(v);}
    template<class... T> void fields(T&... values){(field(values),...);}
    template<class T> void vector(std::vector<T>& v,std::size_t cap){
        std::uint32_t size=static_cast<std::uint32_t>(v.size());field(size);require(size<=cap);
        if(reading()){require(size<=in_.size()-position_);v.resize(size);}
        for(auto& value:v)field(value);
    }
private:
    Bytes* out_{};
    std::span<const std::uint8_t> in_;
    std::size_t position_{};
};
}
