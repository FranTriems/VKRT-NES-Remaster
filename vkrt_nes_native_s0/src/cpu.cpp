#include "vkrt/nes_native_s0/core.hpp"
#include <array>
#include <stdexcept>
namespace vkrt::nes {
namespace {
enum class Op { ADC,AND,ASL,BCC,BCS,BEQ,BIT,BMI,BNE,BPL,BRK,BVC,BVS,CLC,CLD,CLI,CLV,
 CMP,CPX,CPY,DEC,DEX,DEY,EOR,INC,INX,INY,JMP,JSR,LDA,LDX,LDY,LSR,NOP,ORA,PHA,PHP,PLA,PLP,
 ROL,ROR,RTI,RTS,SBC,SEC,SED,SEI,STA,STX,STY,TAX,TAY,TSX,TXA,TXS,TYA, SLO,RLA,SRE,RRA,SAX,LAX,DCP,ISC,ANC,ALR,ARR,AXS,LAS,AHX,SHX,SHY,TAS,XAA,LXA,KIL };
enum class Am { impl,acc,imm,zp,zpx,zpy,abs,abx,aby,izx,izy,rel,ind };
struct Instruction {Op op;Am mode;};
#define O(op,mode) Instruction{Op::op,Am::mode}
constexpr std::array<Instruction,256> decode{{
 O(BRK,impl),O(ORA,izx),O(KIL,impl),O(SLO,izx),O(NOP,zp),O(ORA,zp),O(ASL,zp),O(SLO,zp),O(PHP,impl),O(ORA,imm),O(ASL,acc),O(ANC,imm),O(NOP,abs),O(ORA,abs),O(ASL,abs),O(SLO,abs),
 O(BPL,rel),O(ORA,izy),O(KIL,impl),O(SLO,izy),O(NOP,zpx),O(ORA,zpx),O(ASL,zpx),O(SLO,zpx),O(CLC,impl),O(ORA,aby),O(NOP,impl),O(SLO,aby),O(NOP,abx),O(ORA,abx),O(ASL,abx),O(SLO,abx),
 O(JSR,abs),O(AND,izx),O(KIL,impl),O(RLA,izx),O(BIT,zp),O(AND,zp),O(ROL,zp),O(RLA,zp),O(PLP,impl),O(AND,imm),O(ROL,acc),O(ANC,imm),O(BIT,abs),O(AND,abs),O(ROL,abs),O(RLA,abs),
 O(BMI,rel),O(AND,izy),O(KIL,impl),O(RLA,izy),O(NOP,zpx),O(AND,zpx),O(ROL,zpx),O(RLA,zpx),O(SEC,impl),O(AND,aby),O(NOP,impl),O(RLA,aby),O(NOP,abx),O(AND,abx),O(ROL,abx),O(RLA,abx),
 O(RTI,impl),O(EOR,izx),O(KIL,impl),O(SRE,izx),O(NOP,zp),O(EOR,zp),O(LSR,zp),O(SRE,zp),O(PHA,impl),O(EOR,imm),O(LSR,acc),O(ALR,imm),O(JMP,abs),O(EOR,abs),O(LSR,abs),O(SRE,abs),
 O(BVC,rel),O(EOR,izy),O(KIL,impl),O(SRE,izy),O(NOP,zpx),O(EOR,zpx),O(LSR,zpx),O(SRE,zpx),O(CLI,impl),O(EOR,aby),O(NOP,impl),O(SRE,aby),O(NOP,abx),O(EOR,abx),O(LSR,abx),O(SRE,abx),
 O(RTS,impl),O(ADC,izx),O(KIL,impl),O(RRA,izx),O(NOP,zp),O(ADC,zp),O(ROR,zp),O(RRA,zp),O(PLA,impl),O(ADC,imm),O(ROR,acc),O(ARR,imm),O(JMP,ind),O(ADC,abs),O(ROR,abs),O(RRA,abs),
 O(BVS,rel),O(ADC,izy),O(KIL,impl),O(RRA,izy),O(NOP,zpx),O(ADC,zpx),O(ROR,zpx),O(RRA,zpx),O(SEI,impl),O(ADC,aby),O(NOP,impl),O(RRA,aby),O(NOP,abx),O(ADC,abx),O(ROR,abx),O(RRA,abx),
 O(NOP,imm),O(STA,izx),O(NOP,imm),O(SAX,izx),O(STY,zp),O(STA,zp),O(STX,zp),O(SAX,zp),O(DEY,impl),O(NOP,imm),O(TXA,impl),O(XAA,imm),O(STY,abs),O(STA,abs),O(STX,abs),O(SAX,abs),
 O(BCC,rel),O(STA,izy),O(KIL,impl),O(AHX,izy),O(STY,zpx),O(STA,zpx),O(STX,zpy),O(SAX,zpy),O(TYA,impl),O(STA,aby),O(TXS,impl),O(TAS,aby),O(SHY,abx),O(STA,abx),O(SHX,aby),O(AHX,aby),
 O(LDY,imm),O(LDA,izx),O(LDX,imm),O(LAX,izx),O(LDY,zp),O(LDA,zp),O(LDX,zp),O(LAX,zp),O(TAY,impl),O(LDA,imm),O(TAX,impl),O(LXA,imm),O(LDY,abs),O(LDA,abs),O(LDX,abs),O(LAX,abs),
 O(BCS,rel),O(LDA,izy),O(KIL,impl),O(LAX,izy),O(LDY,zpx),O(LDA,zpx),O(LDX,zpy),O(LAX,zpy),O(CLV,impl),O(LDA,aby),O(TSX,impl),O(LAS,aby),O(LDY,abx),O(LDA,abx),O(LDX,aby),O(LAX,aby),
 O(CPY,imm),O(CMP,izx),O(NOP,imm),O(DCP,izx),O(CPY,zp),O(CMP,zp),O(DEC,zp),O(DCP,zp),O(INY,impl),O(CMP,imm),O(DEX,impl),O(AXS,imm),O(CPY,abs),O(CMP,abs),O(DEC,abs),O(DCP,abs),
 O(BNE,rel),O(CMP,izy),O(KIL,impl),O(DCP,izy),O(NOP,zpx),O(CMP,zpx),O(DEC,zpx),O(DCP,zpx),O(CLD,impl),O(CMP,aby),O(NOP,impl),O(DCP,aby),O(NOP,abx),O(CMP,abx),O(DEC,abx),O(DCP,abx),
 O(CPX,imm),O(SBC,izx),O(NOP,imm),O(ISC,izx),O(CPX,zp),O(SBC,zp),O(INC,zp),O(ISC,zp),O(INX,impl),O(SBC,imm),O(NOP,impl),O(SBC,imm),O(CPX,abs),O(SBC,abs),O(INC,abs),O(ISC,abs),
 O(BEQ,rel),O(SBC,izy),O(KIL,impl),O(ISC,izy),O(NOP,zpx),O(SBC,zpx),O(INC,zpx),O(ISC,zpx),O(SED,impl),O(SBC,aby),O(NOP,impl),O(ISC,aby),O(NOP,abx),O(SBC,abx),O(INC,abx),O(ISC,abx)
}};
#undef O
}
std::uint32_t Machine::step() {
    if(cpu.halted){auto before=cycles;read(cpu.pc,true);return static_cast<std::uint32_t>(cycles-before);} // KIL holds the bus until reset.
    const auto start=cycles;
    if(dma_pending){dma();return static_cast<std::uint32_t>(cycles-start);}
    auto fetch=[&](){return read(cpu.pc++);};
    auto push=[&](std::uint8_t b){write(0x100|cpu.sp,b);--cpu.sp;};
    auto pull=[&](){++cpu.sp;return read(0x100|cpu.sp);};
    auto flag=[&](std::uint8_t f,bool value){cpu.p=std::uint8_t((cpu.p&~f)|(value?f:0));};
    auto nz=[&](std::uint8_t b){flag(2,b==0);flag(0x80,b&0x80);};
    auto interrupt=[&](std::uint16_t vector,bool brk){
        push(cpu.pc>>8);push(cpu.pc&255);
        // NMI may redirect BRK/IRQ before vector selection; the pushed B bit
        // still describes the original entry cause.
        if(cpu.nmi_latched){vector=0xfffa;cpu.nmi_latched=false;cpu.nmi_pending=false;}
        push((cpu.p&0xef)|0x20|(brk?0x10:0));cpu.p|=4;
        auto lo=read(vector);cpu.pc=lo|(std::uint16_t(read(vector+1))<<8);
    };
    if(cpu.nmi_pending||cpu.irq_pending||(!cpu.poll_valid&&cpu.irq_line&&!(cpu.p&4))) {
        auto vector=cpu.nmi_pending?0xfffa:0xfffe;
        if(cpu.nmi_pending)cpu.nmi_latched=false;
        cpu.nmi_pending=false;cpu.irq_pending=false;cpu.poll_valid=true;
        read(cpu.pc,true);read(cpu.pc,true);interrupt(vector,false);
        return static_cast<std::uint32_t>(cycles-start);
    }
    auto opcode_pc=cpu.pc;auto opcode=fetch();auto [op,mode]=decode[opcode];
    if(op==Op::KIL){read(cpu.pc,true);cpu.halted=true;cpu.fault_opcode=opcode;cpu.fault_pc=opcode_pc;return static_cast<std::uint32_t>(cycles-start);}
    const auto old_p=cpu.p;
    auto poll=[&](bool masked){cpu.irq_pending=cpu.irq_previous&&!masked;cpu.nmi_pending=cpu.nmi_previous;cpu.poll_valid=true;};
    auto finish=[&](){
        poll((op==Op::CLI||op==Op::SEI||op==Op::PLP?old_p:cpu.p)&4);
        cpu.p=(cpu.p|0x20)&0xef;return static_cast<std::uint32_t>(cycles-start);
    };
    if(op==Op::BRK){fetch();interrupt(0xfffe,true);cpu.poll_valid=true;return static_cast<std::uint32_t>(cycles-start);}
    if(op==Op::JSR){auto lo=fetch();read(0x100|cpu.sp,true);push(cpu.pc>>8);push(cpu.pc&255);auto hi=fetch();cpu.pc=lo|(hi<<8);return finish();}
    if(op==Op::RTS||op==Op::RTI){
        read(cpu.pc,true);read(0x100|cpu.sp,true);
        if(op==Op::RTI)cpu.p=(pull()|0x20)&0xef;
        auto lo=pull();cpu.pc=lo|(std::uint16_t(pull())<<8);
        if(op==Op::RTS){read(cpu.pc,true);++cpu.pc;}return finish();
    }
    if(op==Op::PHA||op==Op::PHP){read(cpu.pc,true);push(op==Op::PHA?cpu.a:(cpu.p|0x30));return finish();}
    if(op==Op::PLA||op==Op::PLP){read(cpu.pc,true);read(0x100|cpu.sp,true);auto b=pull();if(op==Op::PLA){cpu.a=b;nz(b);}else cpu.p=(b|0x20)&0xef;return finish();}
    if(mode==Am::rel){
        auto offset=static_cast<std::int8_t>(fetch());poll(cpu.p&4);bool taken=false;
        switch(op){
        case Op::BCC:taken=!(cpu.p&1);break;case Op::BCS:taken=cpu.p&1;break;
        case Op::BEQ:taken=cpu.p&2;break;case Op::BNE:taken=!(cpu.p&2);break;
        case Op::BMI:taken=cpu.p&0x80;break;case Op::BPL:taken=!(cpu.p&0x80);break;
        case Op::BVS:taken=cpu.p&0x40;break;case Op::BVC:taken=!(cpu.p&0x40);break;default:break;
        }
        if(taken){auto old=cpu.pc;read(old,true);cpu.pc=std::uint16_t(cpu.pc+offset);if((old^cpu.pc)&0xff00){read((old&0xff00)|(cpu.pc&255),true);poll(cpu.p&4);}}
        return static_cast<std::uint32_t>(cycles-start);
    }
    bool store=op==Op::STA||op==Op::STX||op==Op::STY||op==Op::SAX||op==Op::AHX||op==Op::SHX||op==Op::SHY||op==Op::TAS;
    bool modify=op==Op::ASL||op==Op::LSR||op==Op::ROL||op==Op::ROR||op==Op::INC||op==Op::DEC||op==Op::SLO||op==Op::RLA||op==Op::SRE||op==Op::RRA||op==Op::DCP||op==Op::ISC;
    std::uint16_t address=0,base_address=0;std::uint8_t value=0;
    auto word=[&](){auto lo=fetch();return std::uint16_t(lo|(std::uint16_t(fetch())<<8));};
    switch(mode){
    case Am::impl:case Am::acc:read(cpu.pc,true);value=cpu.a;break;
    case Am::imm:address=cpu.pc++;break;
    case Am::zp:address=fetch();break;
    case Am::zpx:case Am::zpy:{auto b=fetch();read(b,true);address=std::uint8_t(b+(mode==Am::zpx?cpu.x:cpu.y));break;}
    case Am::abs:address=word();break;
    case Am::abx:case Am::aby:{auto base=word();base_address=base;address=std::uint16_t(base+(mode==Am::abx?cpu.x:cpu.y));if(store||modify||((base^address)&0xff00))read((base&0xff00)|(address&255),true);break;}
    case Am::izx:{auto p=fetch();read(p,true);p+=cpu.x;auto lo=read(p);address=lo|(std::uint16_t(read(std::uint8_t(p+1)))<<8);break;}
    case Am::izy:{auto p=fetch();auto lo=read(p);auto base=std::uint16_t(lo|(std::uint16_t(read(std::uint8_t(p+1)))<<8));base_address=base;address=std::uint16_t(base+cpu.y);if(store||modify||((base^address)&0xff00))read((base&0xff00)|(address&255),true);break;}
    case Am::ind:{auto p=word();auto lo=read(p);address=lo|(std::uint16_t(read((p&0xff00)|std::uint8_t(p+1)))<<8);break;}
    case Am::rel:break;
    }
    if(op==Op::JMP){cpu.pc=address;return finish();}
    if(store){
        auto b=op==Op::STA?cpu.a:(op==Op::STX?cpu.x:cpu.y);
        if(op==Op::SAX)b=cpu.a&cpu.x;
        if(op==Op::TAS)cpu.sp=cpu.a&cpu.x;
        if(op==Op::AHX||op==Op::SHX||op==Op::SHY||op==Op::TAS){
            b=(op==Op::SHX?cpu.x:op==Op::SHY?cpu.y:std::uint8_t(cpu.a&cpu.x)) & std::uint8_t((base_address>>8)+1);
            if((base_address^address)&0xff00)address=std::uint16_t((b<<8)|(address&255));
        }
        write(address,b);return finish();
    }
    if(mode!=Am::impl&&mode!=Am::acc)value=read(address);
    if(modify&&mode!=Am::acc)write(address,value); // NMOS read/old-write/new-write.
    auto compare=[&](std::uint8_t a){flag(1,a>=value);nz(std::uint8_t(a-value));};
    auto add=[&](std::uint8_t b){
        unsigned sum=cpu.a+b+(cpu.p&1);
        flag(0x40,(~(cpu.a^b)&(cpu.a^sum)&0x80)!=0);flag(1,sum>255);cpu.a=std::uint8_t(sum);nz(cpu.a);
    };
    switch(op){
    case Op::ADC:case Op::SBC:{
        // RP2A03 retains D in P but always uses binary arithmetic.
        auto b=op==Op::SBC?std::uint8_t(~value):value;unsigned sum=cpu.a+b+(cpu.p&1);
        flag(0x40,(~(cpu.a^b)&(cpu.a^sum)&0x80)!=0);flag(1,sum>255);cpu.a=std::uint8_t(sum);nz(cpu.a);break;
    }
    case Op::LAX:cpu.a=cpu.x=value;nz(value);break;
    case Op::LAS:cpu.a=cpu.x=cpu.sp=value&cpu.sp;nz(cpu.a);break;
    case Op::ANC:cpu.a&=value;nz(cpu.a);flag(1,cpu.a&128);break;
    case Op::ALR:cpu.a&=value;flag(1,cpu.a&1);cpu.a>>=1;nz(cpu.a);break;
    case Op::ARR:cpu.a=std::uint8_t(((cpu.a&value)>>1)|((cpu.p&1)<<7));nz(cpu.a);flag(1,cpu.a&64);flag(0x40,((cpu.a>>6)^(cpu.a>>5))&1);break;
    case Op::AXS:{auto ax=cpu.a&cpu.x;cpu.x=std::uint8_t(ax-value);flag(1,ax>=value);nz(cpu.x);break;}
    // Deterministic digital convention for silicon-dependent immediate gates.
    // The 0xEE precharge mask is not an analog qualification.
    case Op::XAA:cpu.a=(cpu.a|0xee)&cpu.x&value;nz(cpu.a);break;
    case Op::LXA:cpu.a=cpu.x=value;nz(cpu.a);break;
    case Op::AND:cpu.a&=value;nz(cpu.a);break;case Op::ORA:cpu.a|=value;nz(cpu.a);break;case Op::EOR:cpu.a^=value;nz(cpu.a);break;
    case Op::SLO:case Op::ASL:flag(1,value&128);value<<=1;nz(value);break;
    case Op::SRE:case Op::LSR:flag(1,value&1);value>>=1;nz(value);break;
    case Op::RLA:case Op::ROL:{auto carry=cpu.p&1;flag(1,value&128);value=std::uint8_t((value<<1)|carry);nz(value);break;}
    case Op::RRA:case Op::ROR:{auto carry=cpu.p&1;flag(1,value&1);value=std::uint8_t((value>>1)|(carry<<7));nz(value);break;}
    case Op::ISC:case Op::INC:++value;nz(value);break;case Op::DCP:case Op::DEC:--value;nz(value);break;
    case Op::BIT:flag(2,(cpu.a&value)==0);flag(0x40,value&0x40);flag(0x80,value&0x80);break;
    case Op::CMP:compare(cpu.a);break;case Op::CPX:compare(cpu.x);break;case Op::CPY:compare(cpu.y);break;
    case Op::LDA:cpu.a=value;nz(value);break;case Op::LDX:cpu.x=value;nz(value);break;case Op::LDY:cpu.y=value;nz(value);break;
    case Op::CLC:flag(1,false);break;case Op::SEC:flag(1,true);break;
    case Op::CLD:flag(8,false);break;case Op::SED:flag(8,true);break;
    case Op::CLI:flag(4,false);break;case Op::SEI:flag(4,true);break;case Op::CLV:flag(0x40,false);break;
    case Op::DEX:--cpu.x;nz(cpu.x);break;case Op::DEY:--cpu.y;nz(cpu.y);break;
    case Op::INX:++cpu.x;nz(cpu.x);break;case Op::INY:++cpu.y;nz(cpu.y);break;
    case Op::TAX:cpu.x=cpu.a;nz(cpu.x);break;case Op::TAY:cpu.y=cpu.a;nz(cpu.y);break;
    case Op::TSX:cpu.x=cpu.sp;nz(cpu.x);break;case Op::TXA:cpu.a=cpu.x;nz(cpu.a);break;
    case Op::TXS:cpu.sp=cpu.x;break;case Op::TYA:cpu.a=cpu.y;nz(cpu.a);break;
    default:break;
    }
    switch(op){
    case Op::SLO:cpu.a|=value;nz(cpu.a);break;
    case Op::RLA:cpu.a&=value;nz(cpu.a);break;
    case Op::SRE:cpu.a^=value;nz(cpu.a);break;
    case Op::RRA:add(value);break;
    case Op::DCP:compare(cpu.a);break;
    case Op::ISC:add(std::uint8_t(~value));break;
    default:break;
    }
    if(modify){if(mode==Am::acc)cpu.a=value;else write(address,value);}
    return finish();
}
}
