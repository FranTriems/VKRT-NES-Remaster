#include "vkrt/nes_native_s0/core.hpp"
#include <stdexcept>
namespace vkrt::nes {
namespace {
constexpr std::array<std::uint8_t,32> lengths{10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
constexpr std::array<std::uint16_t,16> noise_period{4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
constexpr std::array<std::uint16_t,16> dmc_period{428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54};
constexpr std::array<std::uint8_t,4> duty{0x02,0x06,0x1e,0xf9};
int sweep_target(const Pulse&p,unsigned channel){int shift=p.reg[1]&7;int delta=p.period>>shift;return (p.reg[1]&8)?p.period-delta-(channel==0?1:0):p.period+delta;}
void envelope(std::uint8_t reg,bool& start,std::uint8_t& divider,std::uint8_t& volume){
    if(start){start=false;volume=15;divider=reg&15;}
    else if(divider)--divider;
    else {divider=reg&15;if(volume)--volume;else if(reg&32)volume=15;}
}
}
void Apu::write(std::uint16_t a,std::uint8_t b){
    if(a>=0x4000&&a<=0x4007){auto& p=pulse[(a>>2)&1];unsigned r=a&3;p.reg[r]=b;
        if(r==1)p.sweep_reload=true;
        if(r==2)p.period=(p.period&0x700)|b;
        if(r==3){p.period=(p.period&255)|((b&7)<<8);if(p.enabled)p.length=lengths[b>>3];p.sequence=0;p.envelope_start=true;}
    } else if(a>=0x4008&&a<=0x400b){triangle[a&3]=b;if((a&3)==2)triangle_period=(triangle_period&0x700)|b;
        if((a&3)==3){triangle_period=(triangle_period&255)|((b&7)<<8);if(triangle_enabled)triangle_length=lengths[b>>3];linear_reload=true;}
    } else if(a>=0x400c&&a<=0x400f){noise[a&3]=b;if((a&3)==3){if(noise_enabled)noise_length=lengths[b>>3];noise_start=true;}}
    else if(a>=0x4010&&a<=0x4013){
        auto r=a-0x4010;dmc.reg[r]=b;
        if(r==0&&!(b&0x80))dmc.irq=false;
        if(r==1)dmc.output=b&0x7f;
    }
    else if(a==0x4015){
        dmc.irq=false;
        if(b&16){
            dmc.enabled=true;
            if(!dmc.bytes_remaining){
                dmc.current_address=std::uint16_t(0xc000u+std::uint16_t(dmc.reg[2])*64u);
                dmc.bytes_remaining=std::uint16_t(std::uint16_t(dmc.reg[3])*16u+1u);
                if(dmc.sample_buffer_empty){dmc.fetch_pending=true;dmc.fetch_delay=(cycles&1)?3:2;}
            }
        }else{
            dmc.enabled=false;dmc.bytes_remaining=0;dmc.fetch_pending=false;dmc.fetch_delay=0;
        }
        for(unsigned i=0;i<2;++i){pulse[i].enabled=(b&(1<<i))!=0;if(!pulse[i].enabled)pulse[i].length=0;}
        triangle_enabled=b&4;noise_enabled=b&8;if(!triangle_enabled)triangle_length=0;if(!noise_enabled)noise_length=0;
    } else if(a==0x4017){
        five_step=b&128;irq_inhibit=b&64;if(irq_inhibit)frame_irq=false;
        // tick() consumes the write cycle itself, then three/four more clocks.
        frame_reset_delay=(cycles&1)?4:5;
    }
}
std::uint8_t Apu::status(){
    std::uint8_t b=(pulse[0].length?1:0)|(pulse[1].length?2:0)|(triangle_length?4:0)|(noise_length?8:0)
        |(dmc.bytes_remaining?16:0)|(frame_irq?64:0)|(dmc.irq?128:0);
    frame_irq=false;return b;
}
void Apu::dmc_supply(std::uint8_t b){
    if(!dmc.fetch_pending||!dmc.bytes_remaining)throw std::logic_error("DMC supply without pending fetch");
    dmc.sample_buffer=b;dmc.sample_buffer_empty=false;dmc.fetch_pending=false;++dmc.fetches;
    dmc.current_address=dmc.current_address==0xffff?0x8000:std::uint16_t(dmc.current_address+1);
    --dmc.bytes_remaining;
    if(!dmc.bytes_remaining){
        if(dmc.reg[0]&0x40){
            dmc.current_address=std::uint16_t(0xc000u+std::uint16_t(dmc.reg[2])*64u);
            dmc.bytes_remaining=std::uint16_t(std::uint16_t(dmc.reg[3])*16u+1u);
        }else if(dmc.reg[0]&0x80)dmc.irq=true;
    }
}
void Apu::quarter(){
    for(auto& p:pulse)envelope(p.reg[0],p.envelope_start,p.envelope_divider,p.envelope);
    envelope(noise[0],noise_start,noise_divider,noise_envelope);
    if(linear_reload)linear_counter=triangle[0]&127;else if(linear_counter)--linear_counter;
    if(!(triangle[0]&128))linear_reload=false;
}
void Apu::half(){
    for(unsigned i=0;i<2;++i){auto& p=pulse[i];if(p.length&&!(p.reg[0]&32))--p.length;
        int target=sweep_target(p,i);
        if(!p.sweep_divider&&(p.reg[1]&128)&&(p.reg[1]&7)&&p.period>=8&&target>=0&&target<=0x7ff)p.period=static_cast<std::uint16_t>(target);
        if(!p.sweep_divider||p.sweep_reload){p.sweep_divider=(p.reg[1]>>4)&7;p.sweep_reload=false;}else --p.sweep_divider;
    }
    if(triangle_length&&!(triangle[0]&128))--triangle_length;
    if(noise_length&&!(noise[0]&32))--noise_length;
}
void Apu::tick(){
    ++cycles;++frame_cycle;
    if(dmc.fetch_delay)--dmc.fetch_delay;
    if(frame_reset_delay&&!--frame_reset_delay){frame_cycle=0;if(five_step){quarter();half();}}
    if(frame_cycle==7457||frame_cycle==22371)quarter();
    if(frame_cycle==14913){quarter();half();}
    if(!five_step&&frame_cycle>=29828&&frame_cycle<=29830&&!irq_inhibit)frame_irq=true;
    if(!five_step&&frame_cycle==29829){quarter();half();}
    if(!five_step&&frame_cycle>=29830)frame_cycle=0;
    if(five_step&&frame_cycle==37281){quarter();half();}
    if(five_step&&frame_cycle>=37282)frame_cycle=0;
    if(!(cycles&1))for(auto& p:pulse){if(p.timer)--p.timer;else{p.timer=p.period;p.sequence=(p.sequence+1)&7;}}
    if(triangle_timer)--triangle_timer;else{triangle_timer=triangle_period;if(triangle_enabled&&triangle_length&&linear_counter&&triangle_period>1)triangle_sequence=(triangle_sequence+1)&31;}
    if(noise_timer)--noise_timer;else{noise_timer=noise_period[noise[2]&15]-1;auto feedback=(lfsr^(lfsr>>((noise[2]&128)?6:1)))&1;lfsr=(lfsr>>1)|(feedback<<14);}
    if(dmc.timer)--dmc.timer;else{
        dmc.timer=std::uint16_t(dmc_period[dmc.reg[0]&15]-1);
        if(!dmc.silence){
            if(dmc.shift_register&1){if(dmc.output<=125)dmc.output+=2;}
            else if(dmc.output>=2)dmc.output-=2;
        }
        dmc.shift_register>>=1;
        if(!--dmc.bits_remaining){
            dmc.bits_remaining=8;
            if(dmc.sample_buffer_empty)dmc.silence=true;
            else{dmc.silence=false;dmc.shift_register=dmc.sample_buffer;dmc.sample_buffer_empty=true;}
        }
    }
    // Emptying the sample buffer requests reload on this APU clock, so RDY
    // can halt the following CPU read. Waiting another tick shifts its phase.
    if(dmc.sample_buffer_empty&&dmc.bytes_remaining&&!dmc.fetch_pending)dmc.fetch_pending=true;
    sample_phase+=44100;
    if(sample_phase>=1789773){
        sample_phase-=1789773;unsigned sum=0;
        for(unsigned i=0;i<2;++i){const auto& p=pulse[i];int target=sweep_target(p,i);
            if(p.enabled&&p.length&&p.period>=8&&target<=0x7ff&&target>=0&&((duty[p.reg[0]>>6]>>p.sequence)&1))sum+=(p.reg[0]&16)?p.reg[0]&15:p.envelope;
        }
        unsigned tri=triangle_enabled&&triangle_length&&linear_counter?(triangle_sequence<16?15-triangle_sequence:triangle_sequence-16):0;
        unsigned ns=noise_enabled&&noise_length&&!(lfsr&1)?((noise[0]&16)?noise[0]&15:noise_envelope):0;
        // Deterministic bounded presentation mix. DMC timing/state is canonical; analog transfer-function exactness is not claimed.
        std::uint64_t mixed=static_cast<unsigned>((std::uint64_t(9588)*sum*32767)/(100*(8128+100*sum)))+tri*400+ns*250+std::uint64_t(dmc.output)*80;
        if(mixed>32767)mixed=32767;
        if(pcm.size()>=441000)throw std::length_error("M0 PCM capture limited to ten seconds");
        pcm.push_back(static_cast<std::int16_t>(mixed));
    }
}
void Apu::encode(Encoder&e)const {
    for(const auto&p:pulse){e.data(p.reg);e.u16(p.timer);e.u16(p.period);e.u8(p.sequence);e.u8(p.length);e.u8(p.envelope);e.u8(p.envelope_divider);e.u8(p.sweep_divider);e.u8(p.envelope_start);e.u8(p.sweep_reload);e.u8(p.enabled);}
    e.data(triangle);e.data(noise);e.u16(triangle_timer);e.u16(triangle_period);e.u16(noise_timer);e.u16(lfsr);
    for(auto b:{triangle_sequence,triangle_length,linear_counter,noise_length,noise_envelope,noise_divider})e.u8(b);
    for(auto b:{triangle_enabled,noise_enabled,linear_reload,noise_start,five_step,irq_inhibit,frame_irq})e.u8(b);
    e.data(dmc.reg);e.u16(dmc.timer);e.u16(dmc.current_address);e.u16(dmc.bytes_remaining);
    for(auto b:{dmc.sample_buffer,dmc.shift_register,dmc.bits_remaining,dmc.output})e.u8(b);
    for(auto b:{dmc.sample_buffer_empty,dmc.silence,dmc.enabled,dmc.irq,dmc.fetch_pending})e.u8(b);
    e.u8(dmc.fetch_delay);e.u64(dmc.fetches);e.u64(dmc.stall_cycles);
    e.u32(frame_cycle);e.u32(sample_phase);e.u64(cycles);e.u8(frame_reset_delay);e.u32(static_cast<std::uint32_t>(pcm.size()));for(auto s:pcm)e.u16(static_cast<std::uint16_t>(s));
}
}
