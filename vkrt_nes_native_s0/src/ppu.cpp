#include "vkrt/nes_native_s0/core.hpp"
#include <algorithm>
namespace vkrt::nes {
namespace {
unsigned nt_index(std::uint16_t a,Mirroring mirror) {
    unsigned index=(a-0x2000)&0xfff,table=index/1024;
    if(mirror==Mirroring::horizontal)table>>=1;
    if(mirror==Mirroring::vertical)table&=1;
    if(mirror==Mirroring::one_screen_lower)table=0;
    if(mirror==Mirroring::one_screen_upper)table=1;
    return table*1024+(index&1023);
}
unsigned palette_index(std::uint16_t a){unsigned p=a&31;if((p&0x13)==0x10)p-=16;return p;}
}
PatternObservation Ppu::pattern_observation(const Mapper& m,std::uint16_t a){
    if(!capture_provenance)return {};
    auto& cached=pattern_cache_[(a&0x1ff0)/16];
    const auto source=m.chr_source_at(a),epoch=m.chr_epoch_at(a);
    if(!cached.valid||cached.source!=source||cached.epoch!=epoch){
        cached.source=source;cached.epoch=epoch;cached.valid=true;
        // Mapper::ppu_read is a const memory query. NEVER read_memory/ppu_fetch:
        // those model bus events and would alter MMC3 IRQ timing.
        for(unsigned k=0;k<16;++k)cached.shape.bits[k]=m.ppu_read(std::uint16_t((a&0x1ff0)+k));
    }
    return cached;
}
void Ppu::capture_span(const Sample& s,const PatternObservation& appearance,bool unstable){
    if(!capture_provenance)return;
    ++captured_pixels_;
    const auto& source=s.layer==Layer::sprite?s.sprite:s.background;
    const unsigned index=s.y*width+s.x;
    auto& spans=provenance_->spans;
    if(!spans.empty()&&s.x){auto& last=spans.back();
        const auto& old=last.source;
        // Same actual fetched row and transform. Palette writes do not split
        // geometry; exact pixel-time color/effects stay in the canonical planes.
        if(last.begin+last.count==index&&last.layer==s.layer&&last.unstable==unstable
           &&old.fetch_epoch==source.fetch_epoch&&old.high_fetch_epoch==source.high_fetch_epoch
           &&old.oam_slot==source.oam_slot&&old.fine_x==source.fine_x
           &&(s.layer==Layer::backdrop||int(source.local_x)==int(old.local_x)+((source.flags&64)&&s.layer==Layer::sprite?-int(last.count):int(last.count)))){
            ++last.count;return;
        }
    }
    spans.push_back({index,1,appearance.shape,source,s.layer,unstable});
}
std::uint8_t Ppu::read_memory(Mapper& m,std::uint16_t a,FetchKind kind) {
    a&=0x3fff;if(a<0x3f00)m.ppu_fetch(a,ticks,kind); // Palette is internal to the PPU.
    if(a<0x2000)return m.ppu_read(a);
    if(a<0x3f00)return nametables[nt_index(a,m.mirroring())];
    return palette[palette_index(a)];
}
void Ppu::write_memory(Mapper& m,std::uint16_t a,std::uint8_t b) {
    a&=0x3fff;if(a<0x3f00)m.ppu_fetch(a,ticks,FetchKind::cpu_port);
    if(a<0x2000)m.ppu_write(a,b);
    else if(a<0x3f00)nametables[nt_index(a,m.mirroring())]=b;
    else palette[palette_index(a)]=b&63;
}
void Ppu::update_nmi(){bool line=(status&0x80)&&(ctrl&0x80);if(line&&!nmi_line)nmi_edge=true;nmi_line=line;}
std::uint8_t Ppu::read_register(Mapper& m,std::uint16_t r) {
    std::uint8_t b=io_latch;
    switch(r&7){
    case 2:
        // A read immediately before the vblank set prevents that frame's flag.
        if(scanline==241&&dot==1)suppress_vblank=true;
        b=(status&0xe0)|(io_latch&31);status&=0x7f;write_toggle=false;update_nmi();break;
    case 4:b=oam[oam_address];break;
    case 7:{auto a=v&0x3fff;auto value=read_memory(m,a);b=read_buffer;
        if(a>=0x3f00){b=(value&((mask&1)?0x30:0x3f))|(io_latch&0xc0);read_buffer=read_memory(m,a-0x1000);}
        else read_buffer=value;
        v=(v+((ctrl&4)?32:1))&0x7fff;break;}
    default:break;
    }
    io_latch=b;return b;
}
void Ppu::write_register(Mapper& m,std::uint16_t r,std::uint8_t b) {
    io_latch=b;
    switch(r&7){
    case 0:ctrl=b;t=(t&0x73ff)|((b&3)<<10);update_nmi();break;
    case 1:mask=b;break;case 3:oam_address=b;break;case 4:oam[oam_address++]=b;break;
    case 5:if(!write_toggle){fine_x=b&7;t=(t&0x7fe0)|(b>>3);}else t=(t&0x0c1f)|((b&7)<<12)|((b&0xf8)<<2);write_toggle=!write_toggle;break;
    case 6:if(!write_toggle)t=(t&255)|((b&63)<<8);else {t=(t&0x7f00)|b;v=t;}write_toggle=!write_toggle;break;
    case 7:write_memory(m,v,b);v=(v+((ctrl&4)?32:1))&0x7fff;break;
    default:break;
    }
}
void Ppu::increment_x(){if((v&31)==31){v&=~31;v^=0x400;}else ++v;}
void Ppu::increment_y(){
    if((v&0x7000)!=0x7000)v+=0x1000;
    else {v&=~0x7000;auto y=(v&0x3e0)>>5;if(y==29){y=0;v^=0x800;}else if(y==31)y=0;else ++y;v=(v&~0x3e0)|(y<<5);}
}
void Ppu::evaluate_sprites(){
    if(dot==1){
        eval_n=eval_m=eval_address=eval_count=eval_tail=0;eval_done=false;
        secondary_slots.fill(255);
    }
    if(!(mask&24))return;
    if(dot>=1&&dot<=64){
        if(dot&1)eval_latch=255;
        else secondary_oam[dot/2-1]=eval_latch;
        return;
    }
    // Visible row N evaluates Y=N for output on row N+1. No Y=$FF wrap
    // onto row zero, and no predictive evaluation on the pre-render row.
    if(scanline>=240||dot<65||dot>256)return;
    if(dot&1){eval_latch=oam[(unsigned(eval_n&63)*4)+eval_m];return;}
    if(eval_done)return;
    auto advance_n=[&](){if(++eval_n==64)eval_done=true;};
    const int row=int(scanline)-int(eval_latch);
    const bool in_range=row>=0&&row<((ctrl&32)?16:8);
    if(eval_count<8){
        secondary_oam[eval_address]=eval_latch;
        if(!eval_m){
            if(!in_range){advance_n();return;}
            secondary_slots[eval_count]=eval_n;
        }
        ++eval_address;
        if(++eval_m==4){eval_m=0;++eval_count;advance_n();}
    }else if(eval_tail){
        // Once a match is found, finish the suppressed four-byte copy.
        if(++eval_m==4){eval_m=0;advance_n();}
        if(!--eval_tail){eval_m=0;eval_done=true;}
    }else if(in_range){
        status|=0x20;eval_tail=3;
        if(++eval_m==4){eval_m=0;advance_n();}
    }else{
        // Secondary writes are disabled, but the faulty carry increments
        // BOTH n and m (independently). Tile/attribute/X bytes become Y.
        eval_m=(eval_m+1)&3;advance_n();
    }
}
void Ppu::load_sprite(unsigned slot){
    auto& s=next_sprites[slot];s={};
    if(slot>=next_sprite_count)return;
    s.slot=secondary_slots[slot];s.y=secondary_oam[slot*4];s.tile=secondary_oam[slot*4+1];
    s.flags=secondary_oam[slot*4+2];s.x=secondary_oam[slot*4+3];
    const int h=(ctrl&32)?16:8;
    int row=(int(scanline)-s.y)&(h-1);
    if(s.flags&128)row=h-1-row;
    if(h==16)s.pattern=((s.tile&1)<<12)+((s.tile&0xfe)+(row/8))*16+(row&7);
    else s.pattern=((ctrl&8)?0x1000:0)+s.tile*16+row;
}
void Ppu::compose(){
    unsigned x=dot-1,y=scanline;auto index=y*width+x;
    Sample sample;sample.x=static_cast<std::uint16_t>(x);sample.y=static_cast<std::uint16_t>(y);sample.dot=dot;
    sample.emphasis=mask>>5;sample.grayscale=mask&1;
    unsigned bg=0,sp=0,sp_palette=0;
    const PatternObservation* appearance=&bg_appearance_[fine_x];
    bool unstable=bg_unstable_[fine_x];
    if((mask&8)&&(x>=8||(mask&2))){bg=bg_pixel[fine_x];sample.background=bg_source[fine_x];sample.background.fine_x=fine_x;}
    if((mask&16)&&(x>=8||(mask&4))){
        for(unsigned i=0;i<sprite_count;++i){const auto& s=sprites[i];
            if(x<s.x||x>=unsigned(s.x)+8)continue;
            auto local=x-s.x;unsigned bit=(s.flags&64)?local:7-local;
            auto color=((s.low>>bit)&1)|(((s.high>>bit)&1)<<1);if(!color)continue;
            sp=color;sp_palette=s.flags&3;sample.sprite_behind=s.flags&32;sample.sprite_zero=s.slot==0;
            auto& source=sample.sprite;source.oam_slot=s.slot;source.pattern=s.pattern&0x1ff0;
            source.tile=s.tile;source.palette=s.flags&3;source.flags=s.flags;source.local_x=static_cast<std::uint8_t>(7-bit);
            source.local_y=s.pattern&7;source.origin_x=s.x;source.origin_y=static_cast<std::int16_t>(s.y+1);
            source.chr_source=s.source;source.chr_epoch=s.epoch;source.high_chr_epoch=s.high_epoch;
            source.fetch_epoch=s.fetch;source.high_fetch_epoch=s.high_fetch;source.scroll_v=v;
            if(!bg||!sample.sprite_behind){appearance=&s.appearance;unstable=s.appearance_unstable;}
            break;
        }
    }
    sample.bg_opaque=bg!=0;sample.sprite_opaque=sp!=0;
    if(bg&&sp&&sample.sprite_zero&&x!=255)status|=64;
    unsigned entry=0;
    if(sp&&(!bg||!sample.sprite_behind)){sample.layer=Layer::sprite;entry=16+sp_palette*4+sp;}
    else if(bg){sample.layer=Layer::background;entry=bg_palette[fine_x]*4+bg;}
    // Forced blank can expose the addressed palette entry.
    else if(!(mask&24)&&(v&0x3f00)==0x3f00)entry=palette_index(v);
    sample.color=palette[palette_index(static_cast<std::uint16_t>(entry))]&((mask&1)?0x30:0x3f);
    working.indices[index]=sample.color;working.effects[index]=(mask&0xe0)|(mask&1);
    working.samples[index]=sample;
    if(sample.layer==Layer::backdrop){static const PatternObservation empty{};capture_span(sample,empty,false);}
    else capture_span(sample,*appearance,unstable||!appearance->valid);
}
void Ppu::tick(Mapper& m){
    ++ticks;
    if(scanline==261&&dot==1){suppress_vblank=false;status&=0x1f;update_nmi();}
    if(scanline==241&&dot==1){if(!suppress_vblank)status|=0x80;update_nmi();working.number=++frame_number;
        provenance_->palette=palette;provenance_->oam=oam;
        working.native=capture_provenance&&captured_pixels_==pixels?provenance_:nullptr;
        completed=std::make_shared<const Frame>(working);
        provenance_=std::make_shared<PpuProvenance>();provenance_->spans.reserve(12000);captured_pixels_=0;
    }
    bool render=(mask&24)!=0,visible=scanline<240,prerender=scanline==261;
    if(visible||prerender)evaluate_sprites();
    if((visible||prerender)&&dot==257){
        next_sprites={};next_sprite_count=render&&visible?eval_count:0;
    }
    if(render&&(visible||prerender)){
        if((dot>=2&&dot<=257)||(dot>=322&&dot<=337)){
            for(unsigned i=0;i<15;++i){bg_pixel[i]=bg_pixel[i+1];bg_palette[i]=bg_palette[i+1];bg_source[i]=bg_source[i+1];bg_appearance_[i]=bg_appearance_[i+1];bg_unstable_[i]=bg_unstable_[i+1];}
            bg_pixel[15]=0;bg_palette[15]=0;bg_source[15]={};
        }
        auto load=[&](){
            for(unsigned i=0;i<8;++i){bg_pixel[8+i]=((next_low>>(7-i))&1)|(((next_high>>(7-i))&1)<<1);bg_palette[8+i]=next_attribute;
                bg_appearance_[8+i]=next_appearance_;bg_unstable_[8+i]=next_unstable_;
                Source s;s.nametable=next_nt;s.attribute=next_at;s.pattern=next_pattern&0x1ff0;s.tile=next_tile;s.palette=next_attribute;
                s.local_x=static_cast<std::uint8_t>(i);s.local_y=next_pattern&7;s.chr_source=next_chr_source;s.chr_epoch=next_chr_epoch;
                s.high_chr_epoch=next_high_epoch;s.fetch_epoch=next_fetch;s.high_fetch_epoch=next_high_fetch;
                s.scroll_v=std::uint16_t(((next_nt&0xfff))|((next_pattern&7)<<12));
                s.origin_x=static_cast<std::int16_t>((next_nt&31)*8+((next_nt&0x400)?256:0));
                s.origin_y=static_cast<std::int16_t>(((next_nt>>5)&31)*8+((next_nt&0x800)?240:0));bg_source[8+i]=s;}
        };
        if((dot>=1&&dot<=256)||(dot>=321&&dot<=336)){
            switch((dot-1)&7){
            case 0:load();next_nt=0x2000|(v&0xfff);next_tile=read_memory(m,next_nt,FetchKind::nametable);break;
            case 2:next_at=0x23c0|(v&0xc00)|((v>>4)&0x38)|((v>>2)&7);next_attribute=(read_memory(m,next_at,FetchKind::attribute)>>(((v>>4)&4)|(v&2)))&3;break;
            case 4:next_pattern=((ctrl&16)?0x1000:0)+next_tile*16+((v>>12)&7);next_low=read_memory(m,next_pattern,FetchKind::pattern_low);
                next_appearance_=pattern_observation(m,next_pattern);next_unstable_=false;
                next_chr_source=m.chr_source_at(next_pattern);next_chr_epoch=m.chr_epoch_at(next_pattern);next_fetch=ticks;break;
            case 6:next_high=read_memory(m,next_pattern+8,FetchKind::pattern_high);next_high_epoch=m.chr_epoch_at(next_pattern+8);next_high_fetch=ticks;
                next_unstable_=next_appearance_.shape!=pattern_observation(m,next_pattern+8).shape;break;
            case 7:increment_x();break;default:break;
            }
        }
        if(dot==256)increment_y();
        if(dot==257){load();v=(v&~0x041f)|(t&0x041f);}
        if(prerender&&dot>=280&&dot<=304)v=(v&~0x7be0)|(t&0x7be0);
        if(dot>=257&&dot<=320){
            auto slot=(dot-257)/8,phase=(dot-257)&7;auto& s=next_sprites[slot];
            if(phase==0)load_sprite(slot);
            if(phase==0||phase==2)read_memory(m,0x2000|(v&0xfff),FetchKind::dummy);
            if(phase==4||phase==6){auto a=slot<next_sprite_count?s.pattern:((ctrl&32)?0x1ff0:((ctrl&8)?0x1ff0:0x0ff0));auto b=read_memory(m,a+(phase==6?8:0),FetchKind::sprite);
                if(phase==4){s.low=b;s.source=m.chr_source_at(a);s.epoch=m.chr_epoch_at(a);s.fetch=ticks;s.appearance=pattern_observation(m,a);}
                else{s.high=b;s.high_epoch=m.chr_epoch_at(a+8);s.high_fetch=ticks;s.appearance_unstable=s.appearance.shape!=pattern_observation(m,a+8).shape;}}
        }
        if(dot==337){load();read_memory(m,0x2000|(v&0xfff),FetchKind::dummy);}
        if(dot==339)read_memory(m,0x2000|(v&0xfff),FetchKind::dummy);
    }
    if(visible&&dot>=1&&dot<=256)compose();
    if(prerender&&dot==338)skip_rendering=render;
    if(prerender&&dot==339&&odd&&skip_rendering){dot=0;scanline=0;odd=false;sprites=next_sprites;sprite_count=next_sprite_count;return;}
    if(++dot>340){dot=0;if(++scanline>261){scanline=0;odd=!odd;}sprites=next_sprites;sprite_count=next_sprite_count;}
}
void Ppu::encode(Encoder&e)const {
    e.data(nametables);e.data(palette);e.data(oam);
    for(auto b:{ctrl,mask,status,oam_address,read_buffer,io_latch,fine_x})e.u8(b);
    e.u16(v);e.u16(t);e.u16(scanline);e.u16(dot);e.u8(write_toggle);e.u8(odd);e.u8(nmi_line);e.u8(nmi_edge);e.u8(suppress_vblank);e.u8(skip_rendering);e.u64(ticks);e.u64(frame_number);
    for(auto b:{next_tile,next_attribute,next_low,next_high})e.u8(b);
    e.u16(next_nt);e.u16(next_at);e.u16(next_pattern);e.data(bg_pixel);e.data(bg_palette);
    // bg_source is observational; it never participates in composition decisions.
    e.u8(sprite_count);e.u8(next_sprite_count);
    e.data(secondary_oam);e.data(secondary_slots);
    for(auto b:{eval_n,eval_m,eval_address,eval_latch,eval_count,eval_tail})e.u8(b);
    e.u8(eval_done);
    for(const auto* list:{&sprites,&next_sprites})for(const auto&s:*list){e.u8(s.slot);e.u8(s.x);e.u8(s.y);e.u8(s.tile);e.u8(s.flags);e.u8(s.low);e.u8(s.high);e.u16(s.pattern);}
    e.data(working.indices);e.data(working.effects);
    e.u8(completed!=nullptr);if(completed){e.u64(completed->number);e.data(completed->indices);e.data(completed->effects);}
}
}
