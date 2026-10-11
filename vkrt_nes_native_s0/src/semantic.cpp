#include "vkrt/nes_native_s0/semantic.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <tuple>
namespace vkrt::nes {
namespace {
thread_local std::uint64_t observation_calls{};
const Source& winning(const Sample&s){return s.layer==Layer::sprite?s.sprite:s.background;}
using MatchKey=std::tuple<std::uint64_t,std::uint16_t,std::uint16_t>;
// Exact-key bounded table: correspondence only needs multiplicity and one index.
// Hash collisions compare the complete key; they never authorize a match.
struct MatchTable {
    struct Entry { MatchKey key{}; unsigned count{}, index{}; };
    std::vector<Entry> entries=std::vector<Entry>(131072);
    Entry& at(const MatchKey& key){
        auto [surface,x,y]=key;
        auto h=surface^(std::uint64_t(x)<<32)^y;h^=h>>30;h*=0xbf58476d1ce4e5b9ULL;h^=h>>27;h*=0x94d049bb133111ebULL;h^=h>>31;
        unsigned slot=unsigned(h)&131071;
        while(entries[slot].count&&entries[slot].key!=key)slot=(slot+1)&131071;
        return entries[slot];
    }
    void add(const MatchKey& key,unsigned index){auto& e=at(key);if(!e.count){e.key=key;e.index=index;}++e.count;}
};
struct IdentityEncoder {
    std::uint64_t value{14695981039346656037ULL};
    void u8(std::uint8_t v){value=(value^v)*1099511628211ULL;}
    void u16(std::uint16_t v){u8(v&255);u8(v>>8);}
    void u32(std::uint32_t v){u16(v&65535);u16(v>>16);}
    void u64(std::uint64_t v){u32(std::uint32_t(v));u32(std::uint32_t(v>>32));}
};
MatchKey match_key(const Sample&s){
    const auto& source=winning(s);
    if(s.layer==Layer::backdrop)return {s.surface_key,s.x,s.y};
    return {s.surface_key,source.local_x,source.local_y};
}
bool same_asset(const Source&a,const Source&b,Layer layer){
    return a.pattern==b.pattern&&a.tile==b.tile&&a.palette==b.palette&&a.flags==b.flags&&a.chr_source==b.chr_source
        &&a.chr_epoch==b.chr_epoch&&a.high_chr_epoch==b.high_chr_epoch
        &&(layer!=Layer::background||a.attribute==b.attribute);
}
// Validate the redundant tile/local provenance emitted by the PPU. A name-table
// address identifies a tile, not a whole spatial surface. Opaque external records
// without this evidence retain the stricter exact-context fallback below.
bool background_coordinates(const Source&s){
    const int nt=s.nametable;
    return nt>=0x2000&&nt<0x3000&&(nt&0x3ff)<960
        &&s.attribute==(0x23c0|(nt&0xc00)|((nt>>4)&0x38)|((nt>>2)&7))
        &&s.origin_x==(nt&31)*8+((nt&0x400)?256:0)
        &&s.origin_y==((nt>>5)&31)*8+((nt&0x800)?240:0)
        &&s.scroll_v==((nt&0xfff)|(s.local_y<<12))
        &&s.pattern==((s.pattern&0x1000)+s.tile*16)&&s.fine_x<8;
}
bool background_continuation(const Sample&a,const Sample&b){
    const auto&x=a.background;const auto&y=b.background;
    if(!background_coordinates(x)||!background_coordinates(y))return false;
    // Keep the visible name-table plane, palette, orientation, pattern bank and
    // CHR content epochs. Tile/attribute addresses and asset keys may advance.
    if((x.nametable&0xc00)!=(y.nametable&0xc00)||(x.pattern&0x1000)!=(y.pattern&0x1000)
       ||x.palette!=y.palette||x.flags!=y.flags||x.chr_source!=y.chr_source
       ||x.chr_epoch!=y.chr_epoch||x.high_chr_epoch!=y.high_chr_epoch)return false;
    // Already sampled coordinates include scrolling. Comparing fine_x or origins
    // for equality would mistake a transform or an adjacent tile for a new owner.
    if(int(y.origin_x)+y.local_x-int(x.origin_x)-x.local_x!=int(b.x)-a.x
       ||int(y.origin_y)+y.local_y-int(x.origin_y)-x.local_y!=int(b.y)-a.y)return false;
    // Within the same tile, conflicting content/context keys still veto a join.
    // Across tiles, their inequality is expected and raw continuation is authority.
    return x.nametable!=y.nametable||(same_asset(x,y,Layer::background)
        &&a.asset_key==b.asset_key&&a.surface_key==b.surface_key);
}
}
void validate_frame(const Frame&f){
    if(f.indices.size()!=pixels||f.effects.size()!=pixels||f.samples.size()!=pixels)throw std::invalid_argument("semantic frame extent");
    for(int i=0;i<pixels;++i){const auto&s=f.samples[i];
        if(s.x!=i%width||s.y!=i/width||s.dot>340||s.color>63||s.emphasis>7||f.indices[i]!=s.color
           ||f.effects[i]!=std::uint8_t((s.emphasis<<5)|(s.grayscale?1:0))||static_cast<unsigned>(s.layer)>2)
            throw std::invalid_argument("inconsistent semantic sample");
        for(const auto* source:{&s.background,&s.sprite})if(source->local_x>7||source->local_y>7||source->palette>3)throw std::invalid_argument("semantic source bounds");
    }
}
std::uint64_t asset_key(std::uint16_t mapper,Layer layer,const Source&s){
    IdentityEncoder e;e.u32(0x31545341);e.u16(mapper);e.u8(static_cast<std::uint8_t>(layer));
    if(layer!=Layer::backdrop){e.u64(s.chr_source);e.u16(s.pattern);e.u8(s.tile);e.u8(s.palette);e.u8(s.flags);e.u64(s.chr_epoch);e.u64(s.high_chr_epoch);if(layer==Layer::background)e.u16(s.attribute);}
    return e.value;
}
std::uint64_t surface_key(std::uint64_t asset,Layer layer,const Source&s){
    IdentityEncoder e;e.u32(0x31465253);e.u64(asset);e.u8(static_cast<std::uint8_t>(layer));
    if(layer==Layer::background)e.u16(s.nametable);
    // Sprite identity is content/context based, never OAM-slot based. Duplicates reject history.
    return e.value;
}
ImmutableFrame observe(const Frame& raw,const Frame* previous){
    ++observation_calls;
    validate_frame(raw);if(previous)validate_frame(*previous);
    auto out=std::make_shared<Frame>(raw);
    MatchTable old,current;
    bool adjacent=previous&&previous->number+1==raw.number&&previous->mapper_domain==raw.mapper_domain;
    if(adjacent)for(unsigned i=0;i<pixels;++i)old.add(match_key(previous->samples[i]),i);
    for(unsigned i=0;i<pixels;++i){auto&s=out->samples[i];const auto&source=winning(s);
        const auto* prior=i?&out->samples[i-1]:nullptr;
        if(prior&&prior->layer==s.layer&&(s.layer==Layer::backdrop||same_asset(winning(*prior),source,s.layer))){
            s.asset_key=prior->asset_key;
            s.surface_key=s.layer!=Layer::background||prior->background.nametable==source.nametable?prior->surface_key:surface_key(s.asset_key,s.layer,source);
        }else{s.asset_key=asset_key(raw.mapper_domain,s.layer,source);s.surface_key=surface_key(s.asset_key,s.layer,source);}
        s.previous_surface=0;s.motion_x=0;s.motion_y=0;s.confidence=0;s.reactive=previous&&!adjacent?Reactive::history_gap:Reactive::disoccluded;
        current.add(match_key(s),i);
    }
    if(adjacent)for(auto&s:out->samples){auto key=match_key(s);const auto& entry=old.at(key);
        if(current.at(key).count!=1||(entry.count&&entry.count!=1)){s.reactive=Reactive::ambiguous;continue;}
        if(!entry.count)continue;
        const auto&p=previous->samples[entry.index];
        if(p.layer!=s.layer||!same_asset(winning(p),winning(s),s.layer)){s.reactive=Reactive::ambiguous;continue;}
        if(s.layer==Layer::background&&p.background.nametable!=s.background.nametable){s.reactive=Reactive::ambiguous;continue;}
        int dx=int(p.x)-s.x,dy=int(p.y)-s.y;
        if(std::abs(dx)>32||std::abs(dy)>32){s.reactive=Reactive::ambiguous;continue;}
        s.previous_surface=p.surface_key;s.motion_x=static_cast<std::int16_t>(dx);s.motion_y=static_cast<std::int16_t>(dy);
        if(p.color!=s.color||p.emphasis!=s.emphasis||p.grayscale!=s.grayscale){s.reactive=Reactive::appearance;s.confidence=96;}
        else{s.reactive=Reactive::stable;s.confidence=s.layer==Layer::sprite?192:255;}
    }
    return out;
}
std::uint64_t legacy_observation_calls(){return observation_calls;}
bool same_semantic_owner(const Sample&a,const Sample&b){
    if(a.layer!=b.layer||std::abs(int(a.x)-b.x)>1||std::abs(int(a.y)-b.y)>1)return false;
    // Winning visibility/priority is spatial authority; hidden candidate content
    // and temporal confidence are not. Hash equality alone never authorizes it.
    if(a.bg_opaque!=b.bg_opaque||a.sprite_opaque!=b.sprite_opaque||a.sprite_behind!=b.sprite_behind)return false;
    if(a.layer==Layer::background&&(background_coordinates(a.background)||background_coordinates(b.background)))
        return background_continuation(a,b);
    if(a.asset_key!=b.asset_key||a.surface_key!=b.surface_key)return false;
    const auto&x=winning(a);const auto&y=winning(b);
    if(!same_asset(x,y,a.layer))return false;
    if(a.layer==Layer::background)return x.nametable==y.nametable&&x.origin_x==y.origin_x&&x.origin_y==y.origin_y&&x.fine_x==y.fine_x;
    // OAM slot orders candidates; it is neither necessary nor sufficient for a
    // spatial join. Equal winning content at the same placement survives a slot
    // change; distinct placements still require exact fallback.
    if(a.layer==Layer::sprite)return x.origin_x==y.origin_x&&x.origin_y==y.origin_y;
    return true;
}
Regions connected_regions(const Frame&f){
    validate_frame(f);Regions out;out.labels.resize(pixels);std::vector<unsigned> queue;queue.reserve(pixels);
    auto compatible=[&](unsigned a,unsigned b){return f.indices[a]==f.indices[b]&&f.effects[a]==f.effects[b]
        &&same_semantic_owner(f.samples[a],f.samples[b]);};
    for(unsigned start=0;start<pixels;++start){if(out.labels[start])continue;
        Region r;r.id=static_cast<std::uint32_t>(out.regions.size()+1);r.asset_key=f.samples[start].asset_key;r.color=f.indices[start];r.layer=f.samples[start].layer;
        queue.clear();queue.push_back(start);out.labels[start]=r.id;bool thin=false;
        for(std::size_t head=0;head<queue.size();++head){unsigned i=queue[head],x=i%width,y=i/width;++r.area;
            r.min_x=std::min(r.min_x,static_cast<std::uint16_t>(x));r.max_x=std::max(r.max_x,static_cast<std::uint16_t>(x));
            r.min_y=std::min(r.min_y,static_cast<std::uint16_t>(y));r.max_y=std::max(r.max_y,static_cast<std::uint16_t>(y));
            unsigned degree=0,neighbors=0,bit=1;
            for(auto delta:{-1,1,-width,width}){int n=static_cast<int>(i)+delta;
                bool valid=n>=0&&n<pixels&&!(delta==-1&&x==0)&&!(delta==1&&x==255);
                bool connected=valid&&compatible(i,static_cast<unsigned>(n));
                if(connected)neighbors|=bit;
                bit<<=1;
                if(!connected){++r.perimeter;continue;}
                ++degree;if(!out.labels[n]){out.labels[n]=r.id;queue.push_back(static_cast<unsigned>(n));}
            }
            // Also protect closed one-pixel loops and necks with no terminal.
            if(degree<=1||!(neighbors&3)||!(neighbors&12))thin=true;
        }
        r.protected_fine=(thin&&r.area<=16)||r.area<=6||r.max_x-r.min_x<=1||r.max_y-r.min_y<=1||r.layer==Layer::backdrop
            ||r.perimeter>=r.area;
        out.regions.push_back(r);
    }
    return out;
}
SemanticTopology semantic_topology(const Frame&f){
    SemanticTopology out;out.components=connected_regions(f);out.adjacency.resize(pixels);
    constexpr std::array<int,4> dx{-1,1,0,0},dy{0,0,-1,1};
    for(int i=0;i<pixels;++i)for(int e=0;e<4;++e){
        auto&relation=out.adjacency[i][e];relation=TopologyRelation::hard_owner_boundary;
        int x=i%width+dx[e],y=i/width+dy[e];if(x<0||x>=width||y<0||y>=height)continue;
        int n=y*width+x;const auto&a=f.samples[i];const auto&b=f.samples[n];
        if(a.layer!=b.layer||a.bg_opaque!=b.bg_opaque||a.sprite_opaque!=b.sprite_opaque||a.sprite_behind!=b.sprite_behind)
            relation=TopologyRelation::occlusion_boundary;
        else if(f.effects[i]!=f.effects[n]||winning(a).palette!=winning(b).palette)
            relation=TopologyRelation::palette_effect_boundary;
        else if(same_semantic_owner(a,b))relation=out.components.labels[i]==out.components.labels[n]
            ?TopologyRelation::same_topology_region:TopologyRelation::same_surface_different_appearance;
    }
    return out;
}
std::string frame_hash(const Frame&f){Encoder e;e.u16(width);e.u16(height);e.data(f.indices);e.data(f.effects);return sha256(e.bytes);}
}
