#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/modern_scene.hpp"
#include "vkrt/nes_native_s0/modern_prepared.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <tuple>

using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
std::map<std::string,unsigned> names;
void check(bool b,const char* n){++checks;if(!b){++failures;++names[n];}}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"invalid input rejected");}
const Source& winner(const Sample& s){return s.layer==Layer::sprite?s.sprite:s.background;}
std::uint64_t reference_asset(std::uint16_t mapper,Layer layer,const Source& s){
    Encoder e;e.u32(0x31545341);e.u16(mapper);e.u8(std::uint8_t(layer));
    if(layer!=Layer::backdrop){e.u64(s.chr_source);e.u16(s.pattern);e.u8(s.tile);e.u8(s.palette);e.u8(s.flags);e.u64(s.chr_epoch);e.u64(s.high_chr_epoch);if(layer==Layer::background)e.u16(s.attribute);}
    return identity_hash(e.bytes);
}
std::uint64_t reference_surface(std::uint64_t asset,Layer layer,const Source& s){
    Encoder e;e.u32(0x31465253);e.u64(asset);e.u8(std::uint8_t(layer));if(layer==Layer::background)e.u16(s.nametable);return identity_hash(e.bytes);
}
// Frozen tree/vector observer algorithm, retained here solely as an independent
// differential oracle for the allocation-free production implementation.
ImmutableFrame reference_observe(const Frame& raw,const Frame* previous){
    auto out=std::make_shared<Frame>(raw);
    using Key=std::tuple<std::uint64_t,unsigned,unsigned>;
    auto key=[](const Sample& s)->Key{return s.layer==Layer::backdrop?Key{s.surface_key,s.x,s.y}:Key{s.surface_key,winner(s).local_x,winner(s).local_y};};
    std::map<Key,std::vector<unsigned>> old,current;
    bool adjacent=previous&&previous->number+1==raw.number&&previous->mapper_domain==raw.mapper_domain;
    if(adjacent)for(unsigned i=0;i<pixels;++i)old[key(previous->samples[i])].push_back(i);
    for(unsigned i=0;i<pixels;++i){auto& s=out->samples[i];s.asset_key=reference_asset(raw.mapper_domain,s.layer,winner(s));s.surface_key=reference_surface(s.asset_key,s.layer,winner(s));
        s.previous_surface=0;s.motion_x=s.motion_y=0;s.confidence=0;s.reactive=previous&&!adjacent?Reactive::history_gap:Reactive::disoccluded;current[key(s)].push_back(i);}
    auto same=[](const Source& a,const Source& b,Layer layer){return a.pattern==b.pattern&&a.tile==b.tile&&a.palette==b.palette&&a.flags==b.flags&&a.chr_source==b.chr_source
        &&a.chr_epoch==b.chr_epoch&&a.high_chr_epoch==b.high_chr_epoch&&(layer!=Layer::background||a.attribute==b.attribute);};
    if(adjacent)for(auto& s:out->samples){auto k=key(s);auto it=old.find(k);
        if(current[k].size()!=1||(it!=old.end()&&it->second.size()!=1)){s.reactive=Reactive::ambiguous;continue;}
        if(it==old.end())continue;const auto& p=previous->samples[it->second[0]];
        if(p.layer!=s.layer||!same(winner(p),winner(s),s.layer)){s.reactive=Reactive::ambiguous;continue;}
        if(s.layer==Layer::background&&p.background.nametable!=s.background.nametable){s.reactive=Reactive::ambiguous;continue;}
        int dx=int(p.x)-s.x,dy=int(p.y)-s.y;if(std::abs(dx)>32||std::abs(dy)>32){s.reactive=Reactive::ambiguous;continue;}
        s.previous_surface=p.surface_key;s.motion_x=std::int16_t(dx);s.motion_y=std::int16_t(dy);
        if(p.color!=s.color||p.emphasis!=s.emphasis||p.grayscale!=s.grayscale){s.reactive=Reactive::appearance;s.confidence=96;}
        else{s.reactive=Reactive::stable;s.confidence=s.layer==Layer::sprite?192:255;}
    }
    return out;
}
void observation_equal(const Frame& a,const Frame& b){
    for(int i=0;i<pixels;++i){const auto& x=a.samples[i];const auto& y=b.samples[i];
        check(x.asset_key==y.asset_key,"legacy asset bytes");check(x.surface_key==y.surface_key,"legacy surface bytes");
        check(x.previous_surface==y.previous_surface,"legacy temporal identity");
        check(x.motion_x==y.motion_x&&x.motion_y==y.motion_y,"legacy motion bytes");
        check(x.reactive==y.reactive&&x.confidence==y.confidence,"legacy ambiguity confidence bytes");
        check(x.background==y.background&&x.sprite==y.sprite,"legacy source bytes");
    }
}
Bytes generated_rom(unsigned mapper=0){
    Bytes rom(16+32768+(mapper==3?8192:0));rom[0]='N';rom[1]='E';rom[2]='S';rom[3]=26;rom[4]=2;rom[6]=std::uint8_t((mapper<<4)|1);
    if(mapper==3){rom[5]=1;auto start=16+32768;
        for(int y=0;y<8;++y){rom[start+16+y]=rom[start+64+y]=255;rom[start+32+y]=std::uint8_t(y==0||y==7?0x7e:0x42);
            rom[start+40+y]=y==3||y==4?0x18:0;rom[start+48+y]=std::uint8_t(255>>(y/2));rom[start+56+y]=std::uint8_t(0x80>>(y%4));}}
    return rom;
}
struct Rig {
    Ppu p;
    std::unique_ptr<Mapper> m;
    explicit Rig(unsigned mapper=0):m(load_cartridge(generated_rom(mapper))){
        p.mask=0x1e;p.palette={0x21,0x19,0x2a,0x30,0x21,0x12,0x22,0x30,0x21,0x17,0x27,0x30,0x21,0x14,0x24,0x30,
                             0x21,0x16,0x26,0x30,0x21,0x12,0x22,0x30,0x21,0x17,0x27,0x30,0x21,0x14,0x24,0x30};
        // Distinct generic source forms: solid, ring, taper, plus equal content
        // at a different pattern address. No commercial data is present.
        for(int y=0;y<8;++y){m->ppu_write(16+y,255);m->ppu_write(24+y,0);
            auto ring=std::uint8_t(y==0||y==7?0x7e:0x42);m->ppu_write(32+y,ring);m->ppu_write(40+y,y==3||y==4?0x18:0);
            m->ppu_write(48+y,std::uint8_t(255>>(y/2)));m->ppu_write(56+y,std::uint8_t(0x80>>(y%4)));
            m->ppu_write(64+y,255);m->ppu_write(72+y,0);}
        for(unsigned table=0;table<4;++table)for(int y=0;y<30;++y)for(int x=0;x<32;++x)p.write_memory(*m,std::uint16_t(0x2000+table*1024+y*32+x),std::uint8_t(1+(x/4+y/4)%3));
        p.oam.fill(255);p.oam[0]=71;p.oam[1]=2;p.oam[2]=0;p.oam[3]=63;
        p.oam[4]=71;p.oam[5]=2;p.oam[6]=64;p.oam[7]=111;
        p.oam[8]=112;p.oam[9]=3;p.oam[10]=32;p.oam[11]=141;
    }
    ImmutableFrame next(std::function<void(Ppu&,Mapper&)> hook={}){
        auto target=p.frame_number+1;
        for(unsigned budget=0;p.frame_number!=target&&budget<90000;++budget){if(hook)hook(p,*m);p.tick(*m);}
        if(!p.completed)throw std::runtime_error("fixture frame timeout");return p.completed;
    }
};
void structure_oracle(const Frame& f){
    check(bool(f.native),"PPU attaches provenance");unsigned next=0;
    for(const auto& cmd:f.native->spans){check(cmd.begin==next&&cmd.count!=0,"span partition");next+=cmd.count;
        for(unsigned n=0;n<cmd.count;++n){unsigned i=cmd.begin+n;const auto& s=f.samples[i];const auto& src=winner(s);
            check(s.layer==cmd.layer,"span agrees with winning layer");
            auto expected=s.sprite_opaque&&(!s.bg_opaque||!s.sprite_behind)?Layer::sprite:s.bg_opaque?Layer::background:Layer::backdrop;
            check(expected==s.layer,"independent composition oracle");
            check(f.indices[i]==s.color&&f.effects[i]==((s.emphasis<<5)|s.grayscale),"canonical pixel planes exact");
            if(cmd.layer!=Layer::backdrop){
                int local=int(cmd.source.local_x)+((cmd.layer==Layer::sprite&&(cmd.source.flags&64))?-int(n):int(n));
                check(local==src.local_x&&cmd.source.local_y==src.local_y,"retained local reconstruction");
                unsigned bit=7-src.local_x,row=src.local_y;
                unsigned role=((cmd.shape.bits[row]>>bit)&1)|(((cmd.shape.bits[row+8]>>bit)&1)<<1);
                check(cmd.unstable||role!=0,"shape opacity exact or explicit unstable fallback");
                check(src.fetch_epoch==cmd.source.fetch_epoch&&src.high_fetch_epoch==cmd.source.high_fetch_epoch,"actual fetch provenance exact");
                if(!cmd.unstable){unsigned entry=(s.layer==Layer::sprite?16:0)+src.palette*4+role;
                    // Fixtures do not mutate palette mid-frame in this oracle.
                    check(s.color==(f.native->palette[entry]&((f.effects[i]&1)?0x30:0x3f)),"retained color reconstructs canonical");}
            }
        }
    }
    check(next==pixels,"span coverage entire canonical frame");
}
void legacy_and_mapper_sequences(){
    for(unsigned mapper:{0u,1u,2u,3u,4u,7u}){
        auto rig=std::make_unique<Rig>(mapper);auto scene=std::make_unique<ModernScene>();ImmutableFrame old,reference;
        for(int n=0;n<3;++n){auto raw=rig->next();structure_oracle(*raw);
            auto fast=observe(*raw,old.get()),slow=reference_observe(*raw,reference.get());observation_equal(*fast,*slow);
            observe_modern(*scene,raw);check(scene->stats().legacy_observer_calls==0,"native path bypasses legacy mechanism");
            Encoder before;rig->p.encode(before);Encoder mapper_before;rig->m->encode(mapper_before);
            auto a=render_modern_scene(*scene,{},256,240);auto b=render_modern_scene(*scene,{},256,240);
            for(unsigned i=0;i<a.rgb.size();++i)check(a.rgb[i]==b.rgb[i],"deterministic native output bytes");
            Encoder after;rig->p.encode(after);Encoder mapper_after;rig->m->encode(mapper_after);
            check(before.bytes==after.bytes&&mapper_before.bytes==mapper_after.bytes,"renderer has no canonical write channel");
            if(n==1){rig->p.palette[1]=0x29;rig->p.oam[3]+=2;}
            old=fast;reference=slow;
        }
    }
    Machine machine(synthetic_nrom());machine.run_frames(8);auto raw=machine.frame();auto observed=observe(*raw);
    const auto state=machine.serialize();const auto audio=machine.apu.pcm;const auto canonical=frame_hash(*raw);
    for(int mode=0;mode<3;++mode){auto stripped=std::make_shared<Frame>(*observed);stripped->native.reset();
        check(render(*observed,Mode(mode))==render(*stripped,Mode(mode)),"legacy modes ignore native cache");}
    auto native=std::make_unique<ModernScene>();observe_modern(*native,raw);render_modern_scene(*native);
    check(state==machine.serialize()&&audio==machine.apu.pcm&&canonical==frame_hash(*raw),"machine audio legacy immutable");
    auto gap=std::make_shared<Frame>(*raw);gap->number+=8;observation_equal(*observe(*gap,observed.get()),*reference_observe(*gap,observed.get()));
    // Nonadjacent, changed mapper and ambiguous repeated surfaces retain exactly
    // the old observer's behavior, including attacker-controlled equal keys.
    gap->mapper_domain+=1;observation_equal(*observe(*gap,observed.get()),*reference_observe(*gap,observed.get()));
}
void invalidation_and_identity(){
    auto rig=std::make_unique<Rig>();auto scene=std::make_unique<ModernScene>();auto f=rig->next();observe_modern(*scene,f);
    f=rig->next();observe_modern(*scene,f);const auto first=render_modern_scene(*scene,{},256,240);
    auto shape_count=scene->shape_count();auto bg=scene->backgrounds();
    observe_modern(*scene,f);check(scene->stats().shape_builds==0&&scene->stats().form_builds==0&&scene->stats().background_updates==0&&scene->stats().sprite_updates==0,"repeat delta is empty");
    rig->p.palette[1]=0x28;auto pal=rig->next();observe_modern(*scene,pal);
    check(scene->stats().palette_updates==1,"palette generation delta");
    check(scene->stats().shape_builds==0&&scene->stats().form_builds==0&&scene->stats().background_updates==0,"palette never rebuilds geometry");
    for(unsigned i=0;i<4096;++i)check(scene->backgrounds()[i].generation==bg[i].generation,"unrelated geometry generation retained");
    check(render_modern_scene(*scene,{},256,240)!=first,"palette repaint visible");
    rig->p.oam[3]+=3;observe_modern(*scene,rig->next());check(scene->stats().sprite_updates==1,"OAM changes only affected instance");
    check(scene->stats().shape_builds==0,"OAM transform retains asset");
    bg=scene->backgrounds();rig->p.write_memory(*rig->m,0x20a9,2);observe_modern(*scene,rig->next());
    check(scene->stats().background_updates==1,"nametable delta changes only fetched cell");
    for(unsigned i=0;i<4096;++i)if(i!=0xa9)check(scene->backgrounds()[i].generation==bg[i].generation,"nametable leaves unrelated cells intact");
    // Changing CHR content invalidates users; equivalent source shapes have one
    // collision-free key even at distinct addresses, epochs or palette bindings.
    rig->m->ppu_write(16,0x7e);observe_modern(*scene,rig->next());
    check(scene->stats().shape_builds==1&&scene->stats().background_updates>0,"CHR content invalidates users");
    check(scene->shape_count()==shape_count+1,"content addressed shape insertion");
    auto current=rig->p.completed;NativeSpan a,b;
    for(const auto& cmd:current->native->spans)if(cmd.layer==Layer::background){a=cmd;break;}
    b=a;b.source.nametable+=1;b.source.chr_epoch+=11;b.source.pattern+=16;
    auto ka=native_appearance_key(a),kb=native_appearance_key(b);
    check(ka.shape==kb.shape,"equivalent shape ignores address and epochs");check(ka.context!=kb.context,"generic context disambiguates reused shape");
    b=a;b.source.palette=(b.source.palette+1)&3;kb=native_appearance_key(b);check(ka.shape==kb.shape&&ka.palette_role!=kb.palette_role,"palette role separated from shape");
    // Exact repeat and changed frame numbers cannot animate appearance.
    auto still=render_modern_scene(*scene);auto repeat=std::make_shared<Frame>(*current);repeat->number+=1000;
    observe_modern(*scene,repeat);check(render_modern_scene(*scene)==still,"static-light shimmer exactly zero");
}
void irregular_and_scroll(){
    auto rig=std::make_unique<Rig>();auto scene=std::make_unique<ModernScene>();rig->next();auto f=rig->next();observe_modern(*scene,f);
    auto before=render_modern_scene(*scene,{0,1},256,240);
    rig->p.fine_x=3;auto scrolled=rig->next();observe_modern(*scene,scrolled);auto after=render_modern_scene(*scene,{0,1},256,240);
    check(scene->stats().shape_builds==0,"scroll builds no new shape");
    // Compare a generic interior world patch, away from sprite visibility and
    // newly exposed context. Source-local pigment must move by exact PPU scroll.
    for(int y=20;y<50;++y)for(int x=32;x<200;++x)for(int k=0;k<3;++k){
        // Universal backdrop is an explicitly screen-fixed decorative light
        // field. It is not a source texture and is tested for static shimmer.
        if(f->samples[y*width+x+3].layer==Layer::backdrop)continue;
        bool equal=after.rgb[(y*width+x)*3+k]==before.rgb[(y*width+x+3)*3+k];
        check(equal,"source local appearance does not crawl");}
    bool changed=false;
    auto mixed=rig->next([&](Ppu& p,Mapper& m){if(!changed&&p.scanline==80&&p.dot==7){m.ppu_write(p.next_pattern+8,std::uint8_t(m.ppu_read(p.next_pattern+8)^255));changed=true;}});
    observe_modern(*scene,mixed);check(changed&&scene->stats().unstable_spans>0,"split low high fetch selects fallback");
    structure_oracle(*mixed);auto im=render_modern_scene(*scene,{},256,240);
    for(const auto& cmd:mixed->native->spans)if(cmd.unstable)for(unsigned n=0;n<cmd.count;++n){unsigned i=cmd.begin+n;auto c=digital_color(mixed->indices[i],mixed->effects[i]);
        for(int k=0;k<3;++k)check(im.rgb[i*3+k]==c[k],"irregular region exact canonical fallback");}
    // A caller can copy and mutate Frame while retaining its sidecar. Stale
    // metadata may never impose old visibility on the modified canonical frame.
    auto tampered=std::make_shared<Frame>(*mixed);int i=100*width+100;tampered->samples[i].layer=Layer::backdrop;tampered->samples[i].bg_opaque=false;tampered->samples[i].sprite_opaque=false;
    observe_modern(*scene,tampered);auto structure=modern_structure(*scene);check(structure[i].layer==Layer::backdrop,"stale sidecar never overrides authority");
    auto bad=std::make_shared<Frame>(*mixed);auto tap=std::make_shared<PpuProvenance>(*bad->native);tap->spans.front().count=0;bad->native=tap;
    rejects([&]{observe_modern(*scene,bad);});
}
void extents_topology_and_style(){
    auto rig=std::make_unique<Rig>();rig->next();auto f=rig->next();auto scene=std::make_unique<ModernScene>();observe_modern(*scene,f);
    const auto hash=frame_hash(*f);
    for(auto [w,h]:{std::pair{256,240},std::pair{512,480},std::pair{768,720},std::pair{427,389}}){
        auto a=render_modern_scene(*scene,{},w,h),b=render_modern_scene(*scene,{},w,h);auto structure=modern_structure(*scene,w,h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int sx=int(std::int64_t(2*x+1)*width/(2*w)),sy=int(std::int64_t(2*y+1)*height/(2*h));
            int i=sy*width+sx,j=y*w+x;const auto& s=f->samples[i];
            check(structure[j].layer==s.layer,"style leaves exact silhouette and layer ordering");
            check(structure[j].color==f->indices[i]&&structure[j].effects==f->effects[i],"original categorical topology authoritative");
            check(structure[j].oam_slot==(s.layer==Layer::sprite?s.sprite.oam_slot:255),"OAM is ordinal visibility only");
            for(int k=0;k<3;++k)check(a.rgb[j*3+k]==b.rgb[j*3+k],"repeat at multiple extents byte exact");
        }
        check(render_modern_scene(*scene,{0,0},w,h)==render(*f,Mode::SEMANTIC_HD,w,h),"zero controls exact legacy identity");
    }
    auto exact=render(*f,Mode::EXACT_DIGITAL,256,240),tone=render_modern_scene(*scene,{0,1},256,240),structure=render_modern_scene(*scene,{1,0},256,240),full=render_modern_scene(*scene,{},256,240);
    std::uint64_t delta=0,actor_delta=0,world_delta=0;std::map<std::array<std::uint8_t,3>,std::array<std::uint8_t,3>> output_for_input;bool spatial=false;
    for(int i=0;i<pixels;++i){std::array<std::uint8_t,3> a{},b{};
        for(int k=0;k<3;++k){a[k]=exact.rgb[i*3+k];b[k]=full.rgb[i*3+k];unsigned d=std::abs(int(a[k])-b[k]);delta+=d;if(f->samples[i].layer==Layer::sprite)actor_delta+=d;if(f->samples[i].layer==Layer::background)world_delta+=d;}
        auto [it,inserted]=output_for_input.emplace(a,b);if(!inserted&&it->second!=b)spatial=true;
    }
    check(delta>pixels*8&&actor_delta>500&&world_delta>pixels*4,"material lighting nontrivial on world and actor");
    check(spatial,"appearance is spatial and cannot be a global scalar or palette grade");
    check(tone!=structure&&tone!=exact&&structure!=exact,"tone and structure independently active");
    check(frame_hash(*f)==hash,"no generated image changes canonical frame");
    rejects([&]{render_modern_scene(*scene,{},255,240);});rejects([&]{render_modern_scene(*scene,{-1,1});});
}
}
int main(){
    try{legacy_and_mapper_sequences();invalidation_and_identity();irregular_and_scroll();extents_topology_and_style();}
    catch(const std::exception& e){check(false,e.what());}
    for(const auto& [name,count]:names)std::cerr<<"FAIL "<<name<<" COUNT="<<count<<'\n';
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
