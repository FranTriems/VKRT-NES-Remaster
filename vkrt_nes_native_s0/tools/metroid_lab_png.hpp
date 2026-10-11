#pragma once
#include "vkrt/nes_native_s0/remaster.hpp"
#include "vkrt/nes_native_s0/runtime.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace vkrt::nes::lab {
// Deterministic RGB PNG, filter 0 and stored DEFLATE blocks. No codec dependency,
// timestamps, metadata, ROM payload, tile atlas or provenance serialization.
inline void be32(Bytes& b,std::uint32_t v){for(int n=24;n>=0;n-=8)b.push_back(std::uint8_t(v>>n));}
inline void chunk(Bytes& b,const char* name,const Bytes& data){
    be32(b,std::uint32_t(data.size()));const auto start=b.size();
    b.insert(b.end(),name,name+4);b.insert(b.end(),data.begin(),data.end());
    be32(b,runtime::crc32(std::span<const std::uint8_t>(b).subspan(start)));
}
inline Bytes png(const Image& im){
    if(im.width<=0||im.height<=0||im.rgb.size()!=std::size_t(im.width)*im.height*3)throw std::invalid_argument("PNG extent");
    Bytes raw;raw.reserve(im.rgb.size()+im.height);
    for(int y=0;y<im.height;++y){raw.push_back(0);auto p=im.rgb.begin()+std::size_t(y)*im.width*3;raw.insert(raw.end(),p,p+im.width*3);}
    Bytes z{0x78,0x01};
    for(std::size_t at=0;at<raw.size();){unsigned n=unsigned(std::min<std::size_t>(65535,raw.size()-at));
        z.push_back(at+n==raw.size()?1:0);z.push_back(n&255);z.push_back(n>>8);z.push_back((~n)&255);z.push_back((~n)>>8);
        z.insert(z.end(),raw.begin()+at,raw.begin()+at+n);at+=n;}
    std::uint32_t a=1,b=0;for(auto v:raw){a=(a+v)%65521;b=(b+a)%65521;}be32(z,(b<<16)|a);
    Bytes out{137,80,78,71,13,10,26,10},header;be32(header,im.width);be32(header,im.height);
    header.insert(header.end(),{8,2,0,0,0});chunk(out,"IHDR",header);chunk(out,"IDAT",z);chunk(out,"IEND",{});return out;
}
inline std::string write_png(const std::filesystem::path& path,const Image& im){
    auto b=png(im);std::ofstream f(path,std::ios::binary);f.exceptions(std::ios::badbit|std::ios::failbit);
    f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));return sha256(b);
}
}
