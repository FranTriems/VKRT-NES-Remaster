#include "vkrt/nes_native_s0/core.hpp"
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
namespace vkrt::nes {
Bytes synthetic_nrom(){
    Bytes rom(16+16384+8192);rom[0]='N';rom[1]='E';rom[2]='S';rom[3]=0x1a;rom[4]=1;rom[5]=1;rom[6]=1;
    std::size_t pc=16;
    auto emit=[&](std::initializer_list<unsigned> values){for(auto v:values)rom.at(pc++)=static_cast<std::uint8_t>(v);};
    auto address=[&](){return static_cast<unsigned>(0x8000+pc-16);};
    auto store=[&](unsigned a,unsigned b){emit({0xa9,b,0x8d,a&255,a>>8});};
    auto branch=[&](unsigned opcode,unsigned target){int d=int(target)-int(address()+2);if(d<-128||d>127)throw std::logic_error("witness branch range");emit({opcode,static_cast<unsigned>(d)&255});};
    emit({0x78,0xd8,0xa2,0xff,0x9a,0xe8,0x8e,0x00,0x20,0x8e,0x01,0x20});
    store(0x4017,0x40);store(0x4015,15);
    store(0x4000,0xbf);store(0x4001,0x08);store(0x4002,0xfd);store(0x4003,0x08);
    store(0x4004,0x76);store(0x4005,0x08);store(0x4006,0x7e);store(0x4007,0x09);
    store(0x4008,0xbf);store(0x400a,0xfd);store(0x400b,0x08);
    store(0x400c,0x34);store(0x400e,0x07);store(0x400f,0x08);
    emit({0xa9,0xff,0xa2,0});auto fill=address();emit({0x9d,0x00,0x02,0xe8});branch(0xd0,fill);
    for(auto [a,b]:std::array<std::array<unsigned,2>,12>{{{0x200,95},{0x201,4},{0x202,0},{0x203,80},
        {0x204,143},{0x205,5},{0x206,0x21},{0x207,112},{0x208,111},{0x209,6},{0x20a,0x42},{0x20b,184}}})store(a,b);
    store(0x2006,0x3f);store(0x2006,0);emit({0xa2,0});auto pal=address();emit({0xbd,0x00,0x94,0x8d,0x07,0x20,0xe8,0xe0,32});branch(0xd0,pal);
    store(0x2006,0x20);store(0x2006,0);
    for(unsigned page=0;page<4;++page){emit({0xa2,0});auto loop=address();emit({0xbd,0,0x90+page,0x8d,7,0x20,0xe8});branch(0xd0,loop);}
    store(0x2003,0);store(0x4014,2);store(0x2005,0);store(0x2005,0);store(0x2000,0x80);store(0x2001,0x1e);
    auto forever=address();emit({0x4c,forever&255,forever>>8});
    pc=16+0x800;
    emit({0x48,0x8a,0x48,0x98,0x48,0xe6,0});
    store(0x4016,1);store(0x4016,0);emit({0xad,0x16,0x40,0x29,1,0x85,1});
    emit({0xa5,0,0x29,31,0x18,0x69,80,0x18,0x65,1,0x8d,3,2});
    store(0x2003,0);store(0x4014,2);emit({0xa5,0,0x29,7,0x8d,5,0x20});store(0x2005,0);
    emit({0xa5,0,0x29,31,0x18,0x69,0xd0,0x8d,2,0x40,0x68,0xa8,0x68,0xaa,0x68,0x40});
    for(int vector=0;vector<3;++vector){rom[16+0x3ffa+vector*2]=0;rom[16+0x3ffb+vector*2]=vector==1?0x80:0x88;}
    for(int y=0;y<30;++y)for(int x=0;x<32;++x){unsigned tile=1;
        if(y>=24)tile=2;
        if(y>=12&&y<24&&x>=2&&x<12&&y>=22-(x-2))tile=y==22-(x-2)?3:7;
        if(y==18&&x>=13&&x<=23)tile=2;
        if(y==21&&x>=22&&x<=29)tile=2;
        if(y>=6&&y<10&&x>=22&&x<28)tile=8; // Broad connected regions exercise HD contours.
        rom[16+0x1000+y*32+x]=static_cast<std::uint8_t>(tile);
    }
    constexpr std::string_view title="VKRT NES M0";
    constexpr std::string_view alphabet="VKRTNESM0";
    for(unsigned i=0;i<title.size();++i){auto pos=alphabet.find(title[i]);rom[16+0x1000+2*32+10+i]=pos==std::string_view::npos?1:static_cast<std::uint8_t>(16+pos);}
    for(int i=0;i<64;++i)rom[16+0x13c0+i]=i>=48?0x55:0;
    constexpr std::array<std::uint8_t,32> palette{0x0f,0x21,0x29,0x30,0x0f,0x07,0x17,0x28,0x0f,0x11,0x22,0x30,0x0f,0x03,0x14,0x25,
        0x0f,0x16,0x28,0x30,0x0f,0x12,0x27,0x30,0x0f,0x19,0x2a,0x30,0x0f,0x11,0x21,0x31};
    for(unsigned i=0;i<palette.size();++i)rom[16+0x1400+i]=palette[i];
    auto pixel=[&](unsigned tile,int x,int y,unsigned color){auto base=16+16384+tile*16;rom[base+y]|=(color&1)<<(7-x);rom[base+y+8]|=((color>>1)&1)<<(7-x);};
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){
        pixel(1,x,y,1);pixel(2,x,y,y==0||x==0?1:(y==7||x==7?3:2));pixel(3,x,y,x>=7-y?2:1);pixel(7,x,y,2);
        pixel(8,x,y,x<4?1:3);
        int dx=2*x-7,dy=2*y-7;if(dx*dx+dy*dy<50)pixel(4,x,y,y<3?3:2);
        if(std::abs(dx)+std::abs(dy)<=8)pixel(5,x,y,x<4?1:2);
        if(dx*dx+dy*dy<55&&dx*dx+dy*dy>12)pixel(6,x,y,2);
    }
    constexpr std::array<std::array<std::string_view,7>,9> glyphs{{
        {"10001","10001","10001","10001","10001","01010","00100"},
        {"10001","10010","10100","11000","10100","10010","10001"},
        {"11110","10001","10001","11110","10100","10010","10001"},
        {"11111","00100","00100","00100","00100","00100","00100"},
        {"10001","11001","11001","10101","10011","10011","10001"},
        {"11111","10000","10000","11110","10000","10000","11111"},
        {"01111","10000","10000","01110","00001","00001","11110"},
        {"10001","11011","10101","10101","10001","10001","10001"},
        {"01110","10001","10011","10101","11001","10001","01110"}
    }};
    for(unsigned g=0;g<glyphs.size();++g)for(int y=0;y<8;++y)for(int x=0;x<8;++x)pixel(16+g,x,y,y<7&&x>=1&&x<6&&glyphs[g][y][x-1]=='1'?3:1);
    return rom;
}
}
