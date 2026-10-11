#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/remaster.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <set>
#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
using namespace vkrt::nes;
namespace {
std::uint64_t checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;if(failures<25)std::cerr<<"FAIL "<<name<<'\n';}}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid/disabled input rejected");}
using Color=std::array<std::uint8_t,3>;
Color at(const Image&image,int x,int y){auto i=std::size_t(y*image.width+x)*3;return {image.rgb[i],image.rgb[i+1],image.rgb[i+2]};}
void paint(Frame&f,int x,int y,std::uint8_t c){int i=y*width+x;f.indices[i]=f.samples[i].color=c;}
Frame blank(std::uint8_t color=0x0f){
    Frame f;f.number=1;
    for(int i=0;i<pixels;++i){auto&s=f.samples[i];s.x=i%width;s.y=i/width;s.dot=s.x+1;
        s.layer=Layer::background;s.bg_opaque=true;s.background.pattern=16;s.background.tile=1;
        s.background.local_x=s.x%8;s.background.local_y=s.y%8;
        paint(f,s.x,s.y,color);
    }return f;
}
void rect(Frame&f,int x0,int y0,int x1,int y1,std::uint8_t c){for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)paint(f,x,y,c);}
void exact_compatibility(){
    // Captured from a separately built, untouched 9106f25 parent executable.
    // All uint8 input pairs also freeze the public digital_color alias behavior.
    Bytes table;
    for(unsigned e=0;e<256;++e)for(unsigned p=0;p<256;++p){auto c=digital_color(std::uint8_t(p),std::uint8_t(e));table.insert(table.end(),c.begin(),c.end());}
    check(sha256(table)=="ea5a9f2fb0eac7f9c0822ba6a10034afd67d57fe83e8323a5740a23828880aa1","all 65536 M3 palette/effect calls frozen");
    constexpr std::array<const char*,8> goldens{
        "72a43198cb2e2f61d42b2b9b5886b02c7ba39180ddd3c90800405a547a05ee56",
        "fb9a09ccce010b6de0df732b2075329a567dc17ecff97849658dfb41d18f1be3",
        "850da08f122510e05b595d35be38910404aa5beca8e422f48180053ea77822bd",
        "63ed2e0ee29e5a76ff97f308916b99251eec92694f2cf3df224ffc512b6938e1",
        "683d3d7cf0dfad6990c91a583abaa08ab85fcc530ffe87ac3c11af4a7ea32f74",
        "ce5c80f6dce9c80470fc240a34c1cd83af9d7c883f2794f3dea63c35129f9d29",
        "d1c5ad9b5df3852af73d5871c4bd644415e03df818a1da3b1d7303a6fbe024ee",
        "770a33fa580e4c932938e4a19367a6c3473b52c0561a610efdab77fe486369c9"};
    auto f=blank();for(int i=0;i<pixels;++i){auto&s=f.samples[i];s.color=i%64;s.emphasis=(i/128)%8;s.grayscale=(i/64)%2;
        f.indices[i]=s.color;f.effects[i]=std::uint8_t((s.emphasis<<5)|s.grayscale);}
    for(int scale=1;scale<=8;++scale){auto image=render(f,Mode::EXACT_DIGITAL,width*scale,height*scale);
        check(sha256(image.rgb)==goldens[scale-1],"whole M3 Exact image byte identity at every supported scale");
        for(int i=0;i<pixels;i+=13){int x=i%width*scale,y=i/width*scale;auto c=digital_color(f.indices[i],f.effects[i]);
            check(at(image,x,y)==c&&at(image,x+scale-1,y+scale-1)==c,"Exact first/last subpixel palette and extent");}
        if(scale==3)check(image==render(f,Mode::EXACT_DIGITAL),"M3 default call frozen");
    }
}
struct Topology {int bright{},dark{},holes{};bool operator==(const Topology&)const=default;};
Topology topology(const std::vector<bool>&bits,int w,int h){
    Topology out;std::vector<bool> seen(bits.size());std::vector<int> queue;
    for(int i=0;i<w*h;++i){if(seen[i])continue;bool border=false;queue.clear();queue.push_back(i);seen[i]=true;
        for(std::size_t head=0;head<queue.size();++head){int n=queue[head],x=n%w,y=n/w;border|=x==0||y==0||x==w-1||y==h-1;
            for(auto step:std::array<std::array<int,2>,4>{{{-1,0},{1,0},{0,-1},{0,1}}}){
                int xx=x+step[0],yy=y+step[1];if(xx<0||yy<0||xx>=w||yy>=h)continue;int next=yy*w+xx;
                if(!seen[next]&&bits[next]==bits[i]){seen[next]=true;queue.push_back(next);}
            }
        }
        if(bits[i])++out.bright;else{++out.dark;if(!border)++out.holes;}
    }return out;
}
void support(const Frame&f,const Image&image){
    std::vector<bool> source(pixels),output(std::size_t(image.width)*image.height);
    for(int i=0;i<pixels;++i)source[i]=f.indices[i]==0x30;
    for(int y=0;y<image.height;++y){bool row=true;
        int sy=int(std::int64_t(2*y+1)*height/(2*image.height));
        for(int x=0;x<image.width;++x){int sx=int(std::int64_t(2*x+1)*width/(2*image.width));
            auto c=at(image,x,y);bool light=int(c[0])+c[1]+c[2]>330;
            output[std::size_t(y)*image.width+x]=light;row&=light==source[sy*width+sx];
            // Individual coverage ownership checks, not comparisons of two implementations.
            if((x&7)==0)check(light==source[sy*width+sx],"categorical owner at subpixel center preserved");
        }check(row,"all output subpixels retain source support");
    }
    check(topology(source,width,height)==topology(output,image.width,image.height),"independent flood-fill component and hole counts survive reconstruction");
}
void boundaries(){
    for(int shape=0;shape<8;++shape){auto raw=blank();
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){bool light=false;
            switch(shape){
            case 0:light=x>=128;break;
            case 1:light=y>=120;break;
            case 2:light=x>=64+y/2;break;
            case 3:light=x>=184-y/2;break;
            case 4:light=y>=48+x/2;break;
            case 5:light=y>=176-x/2;break;
            case 6:light=x>=40&&x<216&&y>=30&&y<210&&!(x>=70&&x<186&&y>=60&&y<180);break;
            case 7:light=(x<128)==(y<120);break;
            }if(light)paint(raw,x,y,0x30);
        }
        auto f=observe(raw);auto exact=render(*f,Mode::EXACT_DIGITAL,1024,960),hd=render(*f,Mode::SEMANTIC_HD,1024,960);
        check(hd==render(*f,Mode::SEMANTIC_HD,1024,960),"4x semantic repeat determinism");
        check(hd!=exact,"large-region semantic effects active");support(*f,hd);
        if(shape>=2&&shape<=5){int edge_changes=0,intermediate=0;
            for(int y=4;y<height-4;++y)for(int x=4;x<width-4;++x){int i=y*width+x;
                if(f->indices[i]!=f->indices[i+1]||f->indices[i]!=f->indices[i+width]){
                    for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx){auto c=at(hd,x*4+xx,y*4+yy);
                        edge_changes+=c!=at(exact,x*4+xx,y*4+yy);intermediate+=c[0]>8&&c[0]<210;}
                }
            }check(edge_changes>100&&intermediate>100,"diagonal edge coverage active, not only material or nearest scaling");
        }
        if(shape==7){auto debug=render_diagnostic(*f,Diagnostic::protected_structure);
            for(int y=119;y<=120;++y)for(int x=127;x<=128;++x)for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx){
                check(at(hd,x*4+xx,y*4+yy)==at(exact,x*4+xx,y*4+yy),"ambiguous saddle locked before smoothing");
                check(at(debug,x*4+xx,y*4+yy)==Color{255,220,60},"saddle protection diagnostic");}
        }
        if(shape==2||shape==6)support(*f,render(*f,Mode::SEMANTIC_HD,901,803));
    }
}
void fine_structures(){
    auto raw=blank();
    rect(raw,20,20,60,21,0x30);rect(raw,20,23,60,24,0x30); // Disconnected single-pixel rows.
    rect(raw,70,20,71,60,0x30);rect(raw,73,20,74,60,0x30);
    for(int n=0;n<24;++n)paint(raw,90+n,20+n,0x30); // Disconnected diagonal pixels.
    rect(raw,130,20,146,36,0x30);rect(raw,131,21,145,35,0x0f); // Closed 1px loop, no terminals.
    // Fine glyph with 2px strokes; classify by geometry, never screen location.
    rect(raw,20,100,22,108,0x30);rect(raw,22,100,26,102,0x30);rect(raw,22,103,26,105,0x30);
    // A one-pixel neck stays exact while broad connected lobes may reconstruct.
    rect(raw,60,90,76,106,0x30);rect(raw,84,90,100,106,0x30);rect(raw,76,97,84,98,0x30);
    // Many alternating diagonal ambiguities, plus one-pixel holes in a mass.
    for(int y=150;y<158;++y)for(int x=20;x<28;++x)if((x+y)&1)paint(raw,x,y,0x30);
    rect(raw,130,100,180,140,0x30);paint(raw,150,120,0x0f);paint(raw,152,120,0x0f);
    rect(raw,190,20,230,60,0x30);rect(raw,191,21,229,59,0x0f); // Large closed one-pixel loop.
    rect(raw,190,65,230,85,0x30);rect(raw,192,67,228,83,0x0f); // Large two-pixel outline.
    auto f=observe(raw);auto image=render(*f,Mode::SEMANTIC_HD,1024,960);support(*f,image);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){bool protected_white=raw.indices[y*width+x]==0x30&&(y<90||x<40||y>=150||(y==97&&x>=76&&x<84));
        bool hole=y==120&&(x==150||x==152);if(!protected_white&&!hole)continue;
        for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx)
            check(at(image,x*4+xx,y*4+yy)==digital_color(raw.indices[y*width+x],0),"fine strokes, disconnected pixels, necks and holes remain byte exact");
    }
    auto regions=connected_regions(*f);
    check(regions.regions[regions.labels[20*width+130]-1].protected_fine,"closed thin ring is protected without terminals");
    check(regions.regions[regions.labels[100*width+20]-1].protected_fine,"small dense-perimeter glyph protected");
}
void ownership(){
    for(int variant=0;variant<11;++variant){auto raw=blank();rect(raw,128,0,256,240,0x30);
        for(auto&s:raw.samples)if(s.x>=128){auto&b=s.background;
            switch(variant){
            case 0:b.nametable=1;break; // Same AssetKey, different SurfaceKey.
            case 1:b.attribute=1;break;
            case 2:b.palette=1;break;
            case 3:b.chr_source=1;break;
            case 4:b.chr_epoch=1;break;
            case 5:b.high_chr_epoch=1;break;
            case 6:b.pattern=32;break;
            case 7:b.flags=64;break;
            case 8:s.layer=Layer::sprite;s.sprite_opaque=true;s.sprite.origin_x=128;s.sprite.oam_slot=0;break;
            case 9:s.emphasis=1;raw.effects[s.y*width+s.x]=32;break;
            case 10:s.grayscale=true;raw.effects[s.y*width+s.x]=1;break;
            }
        }
        auto f=observe(raw);auto hd=render(*f,Mode::SEMANTIC_HD,1024,960);auto regions=connected_regions(*f);
        check(regions.labels[100*width+127]!=regions.labels[100*width+128],"provenance/effects separate regions");
        if(variant==0)check(f->samples[127].asset_key==f->samples[128].asset_key&&f->samples[127].surface_key!=f->samples[128].surface_key,"contextual reuse retains asset and separates surface");
        for(int y=5;y<235;++y)for(int x=127;x<=128;++x)for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx)
            check(at(hd,x*4+xx,y*4+yy)==digital_color(f->indices[y*width+x],f->effects[y*width+x]),"no color donation across semantic owner/effect/occlusion boundary");
    }
    // Same-color adjacent reused assets must remain different components too.
    auto raw=blank(0x30);for(auto&s:raw.samples)if(s.x>=128)s.background.nametable=1;
    auto f=observe(raw);auto regions=connected_regions(*f);check(regions.regions.size()==2,"equal-color background SurfaceKeys do not merge");
    for(auto&s:raw.samples){s.layer=Layer::sprite;s.sprite.origin_x=s.x<128?0:128;s.sprite.oam_slot=s.x<128?0:1;}
    f=observe(raw);regions=connected_regions(*f);check(regions.regions.size()==2,"reused sprite AssetKey/SurfaceKey instances do not merge");
    check(f->samples[127].asset_key==f->samples[128].asset_key&&f->samples[127].surface_key==f->samples[128].surface_key,"sprite instance split uses winning provenance beyond keys");
    // Hidden candidates are retained, but never become reconstruction donors.
    auto hidden=raw;for(auto&s:hidden.samples){s.background.pattern=999;s.background.chr_epoch=7;s.background.palette=3;}
    check(render(*observe(hidden),Mode::SEMANTIC_HD)==render(*f,Mode::SEMANTIC_HD),"occluded background cannot bleed through winning sprites");
    for(int direction=0;direction<4;++direction){auto diagonal=blank();
        for(auto&s:diagonal.samples){bool side=direction==0?s.x>=64+s.y/2:direction==1?s.x>=184-s.y/2:direction==2?s.y>=48+s.x/2:s.y>=176-s.x/2;
            if(side){s.background.nametable=1;paint(diagonal,s.x,s.y,0x30);}}
        auto d=observe(diagonal);auto hd=render(*d,Mode::SEMANTIC_HD,1024,960);
        for(int y=1;y<height-1;++y)for(int x=1;x<width-1;++x){int i=y*width+x;bool border=false;
            for(int n:{i-1,i+1,i-width,i+width})border|=d->indices[i]!=d->indices[n];
            if(border)for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx)
                check(at(hd,x*4+xx,y*4+yy)==digital_color(d->indices[i],0),"diagonal different-owner contour remains byte exact");
        }
    }
}
void signal_and_material(){
    auto raw=blank();for(int y=0;y<height;++y)for(int x=0;x<width;++x)paint(raw,x,y,std::uint8_t(((x/8+y/16)%4)*16+((x/16)%12)+1));
    for(unsigned effect=0;effect<16;++effect){auto f=raw;
        for(auto&s:f.samples){s.emphasis=effect/2;s.grayscale=effect&1;f.effects[s.y*width+s.x]=std::uint8_t((s.emphasis<<5)|s.grayscale);}
        auto a=render(f,Mode::AUTHENTIC_SIGNAL,1024,960);
        check(a==render(f,Mode::AUTHENTIC_SIGNAL,1024,960),"signal deterministic across palette effects");
        check(a!=render(f,Mode::EXACT_DIGITAL,1024,960),"authentic effects active for every emphasis/grayscale combination");
    }
    auto first=render(raw,Mode::AUTHENTIC_SIGNAL,1024,960);raw.number+=1;
    check(first!=render(raw,Mode::AUTHENTIC_SIGNAL,1024,960),"carrier phase responds to observed frame phase");raw.number+=2;
    check(first==render(raw,Mode::AUTHENTIC_SIGNAL,1024,960),"carrier phase has bounded three-frame period, no history");
    auto flat=blank(0x30);auto beam=render(flat,Mode::AUTHENTIC_SIGNAL,1024,960);
    check(at(beam,300,400)[0]<at(beam,300,401)[0],"bounded continuous beam row response active at 4x");
    // Finite horizontal filter support: one changed sample cannot touch other rows or distant columns.
    auto impulse=flat;paint(impulse,128,120,0x16);auto changed=render(impulse,Mode::AUTHENTIC_SIGNAL,1024,960);
    int nearby_changes=0;
    for(int y=0;y<960;y+=3)for(int x=0;x<1024;x+=3){
        bool near=y/4==120&&std::abs(x/4-128)<=4;
        if(!near)check(at(beam,x,y)==at(changed,x,y),"signal kernel has bounded horizontal support and no row wrapping");
        else nearby_changes+=at(beam,x,y)!=at(changed,x,y);
    }check(nearby_changes>0,"signal impulse is active");
    for(auto palette:{0x00,0x16,0x21,0x30}){auto f=observe(blank(std::uint8_t(palette)));auto hd=render(*f,Mode::SEMANTIC_HD,1024,960);auto c=digital_color(std::uint8_t(palette),0);int active=0;
        for(int y=0;y<960;y+=7)for(int x=0;x<1024;x+=7){auto d=at(hd,x,y);bool bounded=true;
            for(int k=0;k<3;++k)bounded&=std::abs(int(d[k])-c[k])<=2;
            check(bounded,"safe material <=2 RGB codes");active+=d!=c;
            if(x<8||x>=1016||y<8||y>=952)check(d==c,"material stays outside two-cell boundary guard");
        }if(palette==0x30)check(active>0,"safe large-region material modulation active");
    }
}
void diagnostics_and_bounds(){
    auto f=blank();rect(f,80,80,90,81,0x30);
    auto observation=observe(f);f=*observation;
    for(auto&s:f.samples){s.motion_x=-5;s.motion_y=3;s.confidence=96;s.reactive=Reactive::appearance;}
    for(auto mode:{Diagnostic::layer,Diagnostic::asset,Diagnostic::protected_structure,Diagnostic::motion,Diagnostic::reactive}){
        auto a=render_diagnostic(f,mode);check(a==render_diagnostic(f,mode),"diagnostics repeat deterministically");
        check(a.width==1024&&a.height==960,"diagnostic 4x first-class default");
        if(mode==Diagnostic::motion)check(at(a,0,0)==Color{108,140,96},"motion signed direction and confidence encoding");
        if(mode==Diagnostic::reactive)check(at(a,0,0)==Color{240,200,40},"reactive appearance encoding");
        if(mode==Diagnostic::layer)check(at(a,0,0)==Color{50,180,240},"background layer encoding");
    }
    auto stale=f;for(auto&s:stale.samples){s.motion_x=30;s.previous_surface=999;s.reactive=Reactive::ambiguous;}
    check(render(stale,Mode::SEMANTIC_HD)==render(f,Mode::SEMANTIC_HD),"motion diagnostics grant no temporal runtime authority");
    constexpr std::array<Color,5> reactive_colors{{{40,180,80},{240,80,40},{240,200,40},{200,60,220},{60,100,240}}};
    for(unsigned state=0;state<5;++state){f.samples[0].reactive=static_cast<Reactive>(state);
        check(at(render_diagnostic(f,Diagnostic::reactive,256,240),0,0)==reactive_colors[state],"all reactive states distinct");}
    f.samples[0].motion_x=INT16_MIN;f.samples[0].motion_y=INT16_MAX;
    check(at(render_diagnostic(f,Diagnostic::motion,256,240),0,0)==Color{0,255,96},"debug motion saturation cannot overflow");
    for(auto size:std::array<std::array<int,2>,5>{{{256,240},{257,241},{901,803},{1024,960},{2048,1920}}})
        for(auto mode:{Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD}){auto a=render(f,mode,size[0],size[1]);
            check(a.rgb.size()==std::size_t(size[0])*size[1]*3,"bounded arbitrary output extent");check(a==render(f,mode,size[0],size[1]),"noninteger/max extent determinism");}
    for(auto size:std::array<std::array<int,2>,8>{{{0,0},{255,240},{256,239},{2049,960},{1024,1921},{-1,960},{INT_MAX,INT_MAX},{INT_MIN,240}}}){
        for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD})rejects([&]{render(f,mode,size[0],size[1]);});
        rejects([&]{render_diagnostic(f,Diagnostic::asset,size[0],size[1]);});
    }
    rejects([&]{render(f,Mode::EXACT_DIGITAL,1024,720);});rejects([&]{render(f,static_cast<Mode>(99));});
    rejects([&]{render_diagnostic(f,static_cast<Diagnostic>(99));});
    for(auto mode:{Mode::TEMPORAL_RECONSTRUCTION,Mode::NEURAL_RESIDUAL_EXPERIMENTAL})rejects([&]{render(f,mode,1024,960);});
    NeuralResidualPolicy policy;check(!policy.enabled&&!policy.geometry_authority&&!policy.silhouette_authority&&!policy.hud_authority&&!policy.low_frequency_authority,"disabled neural grants no authority");
    auto invalid=f;invalid.samples.pop_back();rejects([&]{render(invalid,Mode::SEMANTIC_HD);});rejects([&]{render_diagnostic(invalid,Diagnostic::asset);});
}
void invariance(){
    static_assert(std::is_const_v<ImmutableFrame::element_type>);
    Machine off(synthetic_nrom()),on(synthetic_nrom());ImmutableFrame previous;
    for(unsigned frame=0;frame<8;++frame){for(auto*m:{&off,&on}){m->controllers[0].buttons=frame%3==0?1:0;m->run_frames(1);}
        auto before=on.serialize();auto pcm=on.apu.pcm;auto raster=on.frame()->indices,effects=on.frame()->effects;
        auto observed=observe(*on.frame(),previous.get());auto copy=*observed;
        for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD}){
            auto image=render(*observed,mode,1024,960);check(image==render(*observed,mode,1024,960),"synthetic 4x repeat");}
        for(auto mode:{Diagnostic::layer,Diagnostic::asset,Diagnostic::protected_structure,Diagnostic::motion,Diagnostic::reactive})(void)render_diagnostic(*observed,mode);
        check(on.serialize()==before&&before==off.serialize(),"full canonical state bytes off/on and before/after");
        check(on.apu.pcm==pcm&&pcm==off.apu.pcm&&on.audio_hash()==off.audio_hash(),"full canonical PCM and hash unchanged");
        check(on.frame()->indices==raster&&raster==off.frame()->indices&&on.frame()->effects==effects&&effects==off.frame()->effects,"canonical palette/effect bytes unchanged");
        for(int i=0;i<pixels;i+=17){const auto&a=observed->samples[i];const auto&b=copy.samples[i];
            check(a.background==b.background&&a.sprite==b.sprite&&a.asset_key==b.asset_key&&a.surface_key==b.surface_key&&a.motion_x==b.motion_x&&a.motion_y==b.motion_y&&a.confidence==b.confidence&&a.reactive==b.reactive,"immutable semantic provenance and history unchanged");}
        previous=observed;
    }
    check(on.state_hash()=="e2c39f34bed83cf4c421837b55154af61695aa2134c082fc64c45977d82eb442","M3 synthetic canonical state golden");
    check(frame_hash(*on.frame())=="76d4e9d9e6a71c80ad6220e6837b11826354bab2e53ee652ac0dff87159a0d3a","M3 synthetic canonical frame golden");
    check(on.audio_hash()=="aad8905be5c045231333d03432147539c80e5f21af8cb945244946b535326d97","M3 synthetic canonical audio golden");
    on.run_frames(2);off.run_frames(2);check(on.serialize()==off.serialize()&&on.apu.pcm==off.apu.pcm,"continued canonical execution invariant after presentation");
}
// M4-R1 independent gates. The historical M4-R functions above are unmodified.
void topology_graph_contract(){
    auto raw=blank();rect(raw,128,0,256,240,0x30);auto f=*observe(raw);
    auto graph=semantic_topology(f);
    using R=TopologyRelation;
    check(graph.adjacency[100*width+100][1]==R::same_topology_region,"graph: same topology region");
    check(graph.adjacency[100*width+127][1]==R::same_surface_different_appearance,"graph: same surface, different appearance");
    for(int variant=0;variant<15;++variant){auto g=f;
        for(auto&s:g.samples)if(s.x>=128){switch(variant){
        case 0:s.surface_key^=1;break;
        case 1:s.background.nametable=1;break; // Deliberately equal hashes: raw identity must win.
        case 2:s.background.origin_x=8;break;
        case 3:s.background.origin_y=8;break;
        case 4:s.background.fine_x=1;break;
        case 5:s.background.chr_source=9;break;
        case 6:s.layer=Layer::sprite;s.sprite_opaque=true;break;
        case 7:s.sprite_opaque=true;break;
        case 8:s.bg_opaque=false;break;
        case 9:s.sprite_behind=true;break;
        case 10:s.background.palette=1;break;
        case 11:s.emphasis=1;g.effects[s.y*width+s.x]=32;break;
        case 12:s.grayscale=true;g.effects[s.y*width+s.x]=1;break;
        case 13:s.background.high_chr_epoch=3;break;
        case 14:s.background.flags=32;break;
        }}
        auto ir=semantic_contours(g);auto rel=ir.topology.adjacency[100*width+127][1];
        auto wanted=variant>=6&&variant<=9?R::occlusion_boundary:
            variant>=10&&variant<=12?R::palette_effect_boundary:R::hard_owner_boundary;
        check(rel==wanted,"graph: explicit raw owner / occlusion / palette-effect boundary");
        check(ir.topology.components.labels[127]!=ir.topology.components.labels[128],"graph split despite reused/forged hashes");
        auto image=render(g,Mode::SEMANTIC_HD,777,733);
        for(int y=0;y<image.height;++y)for(int x=383;x<395;++x){int sx=int(std::int64_t(2*x+1)*width/(2*image.width));
            int sy=int(std::int64_t(2*y+1)*height/(2*image.height)),i=sy*width+sx;
            check(at(image,x,y)==digital_color(g.indices[i],g.effects[i]),"hard boundary exact at noninteger footprints");}
    }
    for(int i=0;i<pixels;++i)for(int e=0;e<4;++e){int x=i%width+(e==0?-1:e==1?1:0),y=i/width+(e==2?-1:e==3?1:0);
        if(x>=0&&x<width&&y>=0&&y<height)check(graph.adjacency[i][e]==graph.adjacency[y*width+x][e^1],"graph adjacency is reciprocal");}
}
Frame saddle_fixture(int transform,bool split_owner=false){
    auto f=blank(0x30);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        int xx=transform&1?width-1-x:x,yy=transform&2?height-1-y:y;
        bool dark=(xx>=101&&xx<109&&yy>=93&&yy<101)||(xx>=93&&xx<101&&yy>=101&&yy<109);
        if(dark)paint(f,x,y,0x0f);
        if(split_owner&&xx>=101)f.samples[y*width+x].background.nametable=1;
    }return *observe(f);
}
void saddle_resolution(){
    for(int t=0;t<4;++t){auto f=saddle_fixture(t);auto ir=semantic_contours(f);
        int x=t&1?width-102:100,y=t&2?height-102:100,i=y*width+x;
        auto wanted=(t==0||t==3)?DiagonalResolution::connected_ad:DiagonalResolution::connected_bc;
        check(ir.diagonals[i]==wanted,"unique saddle continues an existing four-connected region");
        int cuts=0;for(int n:{i,i+1,i+width,i+width+1})for(auto cut:ir.cells[n].cuts)cuts+=cut.donor>=0;
        check(cuts>=2,"resolved saddle emits actual contour geometry");
        auto split=semantic_contours(saddle_fixture(t,true));
        check(split.diagonals[i]==DiagonalResolution::ambiguous_diagonal,"different owner cannot resolve same-color saddle");
        for(int n:{i,i+1,i+width,i+width+1})check(split.cells[n].locked,"unsupported saddle exact fallback");
    }
    auto f=blank();for(int y=90;y<110;++y)for(int x=90;x<110;++x)if((x+y)&1)paint(f,x,y,0x30);
    auto ir=semantic_contours(*observe(f));
    for(int y=90;y<109;++y)for(int x=90;x<109;++x)check(ir.diagonals[y*width+x]==DiagonalResolution::ambiguous_diagonal,"checkerboard never invents diagonal identity");
}
Frame topology_atlas(){
    auto f=blank();
    // Each tile is a separate component and has an empty border. Slopes 1/1,
    // 1/2, 2/1 in every mirrored orientation are followed by curves and features.
    for(int tile=0;tile<30;++tile){int ox=8+(tile%6)*40,oy=8+(tile/6)*44;
        for(int y=0;y<34;++y)for(int x=0;x<32;++x){bool ink=false;
            if(tile<12){int kind=tile/4,t=tile%4,xx=t&1?31-x:x,yy=t&2?33-y:y;
                ink=xx>=3&&xx<29&&yy>=3&&yy<31&&(kind==0?xx>=yy:kind==1?xx>=4+yy/2:yy>=4+xx/2);
            }else switch(tile){
            case 12:ink=(x-16)*(x-16)+(y-17)*(y-17)<=169;break;
            case 13:ink=(x-16)*(x-16)+(y-17)*(y-17)<=196&&(x-16)*(x-16)+(y-17)*(y-17)>=64;break;
            case 14:ink=x>=4&&x<28&&y>=4&&y<30&&!(x>=5&&x<27&&y>=5&&y<29);break;
            case 15:ink=x>=4&&x<28&&y>=4&&y<30&&!(x>=6&&x<26&&y>=6&&y<28);break;
            case 16:ink=(x==8||x==12)&&y>=4&&y<30;break;
            case 17:ink=(y==8||y==12)&&x>=4&&x<28;break;
            case 18:ink=x==y&&x>=4&&x<28;break;
            case 19:ink=(x>=3&&x<12&&y>=10&&y<23)||(x>=20&&x<29&&y>=10&&y<23)||(x>=12&&x<20&&y==16);break;
            case 20:ink=x>=4&&x<28&&y>=4&&y<30&&!(x==16&&y==17)&&!(x==18&&y==17);break;
            case 21:ink=(x>=4&&x<9&&y>=4&&y<9)||(x==6&&y>=9&&y<32);break;
            case 22:ink=(x>=4&&x<6&&y>=4&&y<24)||(x>=6&&x<18&&(y==4||y==12));break;
            case 23:ink=x>=8&&x<24&&y>=8&&y<24&&((x+y)&1);break;
            case 24:ink=(x>=8&&x<10&&y>=4&&y<28);break;
            case 25:ink=(y>=8&&y<10&&x>=4&&x<28);break;
            case 26:ink=x>=4&&x<28&&y>=4&&y<30&&!(x>12&&y>12);break;
            case 27:ink=(x>=4&&x<14&&y>=4&&y<14)||(x>=18&&x<28&&y>=18&&y<28);break;
            case 28:ink=(x==10&&y==10)||(x==12&&y==12);break;
            case 29:ink=x>=4&&x<28&&y>=4&&y<30&&!(x>=12&&x<20&&y>=12&&y<22);break;
            }if(ink)paint(f,ox+x,oy+y,0x30);
        }
    }return *observe(f);
}
// Independent output flood fill; classification does not consume the IR or its
// component IDs. Test both colors, so splitting background/closing a hole fails.
void output_topology(const Frame&f,const Image&image){
    std::vector<bool> src(pixels),dst(std::size_t(image.width)*image.height);
    for(int i=0;i<pixels;++i)src[i]=f.indices[i]==0x30;
    bool bounded=true;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){auto c=at(image,x,y);
        dst[std::size_t(y)*image.width+x]=c[0]>110;
        bounded&=c[0]==c[1]&&c[1]==c[2]&&c[0]<=220;
    }
    check(bounded,"coverage remains in observed color envelope, no ringing");
    auto a=topology(src,width,height),b=topology(dst,image.width,image.height);
    if(!(a==b))std::cerr<<"TOPOLOGY "<<image.width<<'x'<<image.height<<" source="<<a.bright<<','<<a.dark<<','<<a.holes<<" output="<<b.bright<<','<<b.dark<<','<<b.holes<<'\n';
    check(a==b,"independent component AND hole counts at arbitrary extents");
}
void contour_topology_and_support(){
    const std::array<std::array<int,2>,16> sizes{{{256,240},{257,241},{263,251},{383,359},
        {511,479},{512,480},{769,721},{901,803},{1023,959},{1024,960},{1279,1199},
        {1536,1440},{2047,1919},{2048,1920},{256,1920},{2048,240}}};
    auto f=topology_atlas();auto ir=semantic_contours(f);
    bool protected_aa=false;int cut_count=0;
    for(int i=0;i<pixels;++i){const auto&cell=ir.cells[i];
        auto label=ir.topology.components.labels[i];
        for(int c=0;c<4;++c){auto cut=cell.cuts[c];if(cut.donor<0)continue;++cut_count;
            protected_aa|=ir.topology.components.regions[label-1].protected_fine;
            check(!cell.locked&&cut.reach_q8>0&&cut.reach_q8<64,"IR bounded disjoint corner geometry");
            check(f.samples[i].background==f.samples[cut.donor].background||
                (f.samples[i].background.nametable==f.samples[cut.donor].background.nametable&&
                 f.samples[i].background.pattern==f.samples[cut.donor].background.pattern),"IR donor independently retains source instance");
            // A cut touches two cardinal donor cells. Their labels must already
            // coincide; otherwise the cut would join disconnected components.
            int dx=c&1?1:-1,dy=c&2?1:-1;
            check(ir.topology.components.labels[i+dx]==ir.topology.components.labels[i+dy*width]&&
                ir.topology.components.labels[cut.donor]==ir.topology.components.labels[i+dx],"cut cannot join distinct donor components");
        }
    }
    check(protected_aa,"protected-fine region permits safe AA on its broad lobe");
    check(cut_count>100,"atlas has eligible geometry, not an all-exact solution");
    for(auto size:sizes){auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);int moved=0;bool envelope=true;
        check(image==render(f,Mode::SEMANTIC_HD,size[0],size[1]),"arbitrary footprint deterministic repeat");
        output_topology(f,image);
        for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x){
            int sx=int(std::int64_t(2*x+1)*width/(2*size[0])),sy=int(std::int64_t(2*y+1)*height/(2*size[1])),i=sy*width+sx;
            auto color=at(image,x,y);const auto&cell=ir.cells[i];
            if(cell.locked)check(color==digital_color(f.indices[i],0),"structural fallback byte exact at all tested phases");
            if((color[0]>110)!=(f.indices[i]==0x30)){
                ++moved;bool supported=false;
                auto u=std::int64_t(2*x+1)*width-std::int64_t(2*size[0])*sx;
                auto v=std::int64_t(2*y+1)*height-std::int64_t(2*size[1])*sy;
                for(int c=0;c<4;++c){auto cut=cell.cuts[c];if(cut.donor<0)continue;
                    auto dx=c&1?2*size[0]-u:u,dy=c&2?2*size[1]-v:v;
                    // Independent point-support envelope. One Q8 unit allows
                    // final RGB rounding at a half-coverage decision boundary.
                    supported|=dx*256*2*size[1]+dy*256*2*size[0]<=std::int64_t(cut.reach_q8+1)*4*size[0]*size[1];
                }envelope&=supported;
            }
        }
        check(envelope,"categorical silhouette displacement stays within quarter-cell contour envelope");
        if(size[0]==2048&&size[1]==1920)check(moved>100,"8x resolves geometric boundary movement, not only sub-majority blending");
        output_topology(saddle_fixture(0),render(saddle_fixture(0),Mode::SEMANTIC_HD,size[0],size[1]));
    }
    auto stale=f;stale.number=999;for(auto&s:stale.samples){s.previous_surface=123;s.confidence=255;s.motion_x=31;s.motion_y=-27;s.reactive=Reactive::stable;}
    check(render(stale,Mode::SEMANTIC_HD)==render(f,Mode::SEMANTIC_HD),"contours spatial only, even frame number grants no authority");
}
void ideal_contour_error(){
    // Independent geometric reference: ideal half-plane area estimated with a
    // 16x16 stratified grid. It does not use cell cuts or implementation labels.
    double worst_ratio=0;
    for(int slope=0;slope<3;++slope)for(int mirror=0;mirror<4;++mirror){
        int p=slope==2?2:1,q=slope==1?2:1;double intercept=128*q-120*p-0.5;
        auto inside=[&](double x,double y){if(mirror&1)x=width-x;if(mirror&2)y=height-y;return q*x-p*y>=intercept;};
        auto f=blank();for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(inside(x+0.5,y+0.5))paint(f,x,y,0x30);
        f=*observe(f);
        for(auto size:std::array<std::array<int,2>,3>{{{1024,960},{1393,1301},{2048,1920}}}){
            auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);double exact_error=0,contour_error=0;int mixed=0;
            for(int y=16*size[1]/height;y<(height-16)*size[1]/height;++y){
                for(int x=16*size[0]/width;x<(width-16)*size[0]/width;++x){
                    double xx=(x+0.5)*width/size[0],yy=(y+0.5)*height/size[1];
                    double tx=mirror&1?width-xx:xx,ty=mirror&2?height-yy:yy;
                    if(std::abs(q*tx-p*ty-intercept)>2*(p+q))continue;
                    int covered=0;for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx)
                        covered+=inside((x+(sx+0.5)/16)*width/size[0],(y+(sy+0.5)/16)*height/size[1]);
                    double truth=220.0*covered/256;
                    int base=f.indices[int(yy)*width+int(xx)]==0x30?220:0,c=at(image,x,y)[0];
                    exact_error+=std::abs(base-truth);contour_error+=std::abs(c-truth);mixed+=c>2&&c<218;
                }
            }
            check(mixed>100,"eligible diagonal produces meaningful subpixel coverage");
            check(contour_error<exact_error*0.95,"ideal-line area error improves >=5%, not merely changed pixels");
            worst_ratio=std::max(worst_ratio,contour_error/exact_error);
        }
    }
    std::cout<<"M4R1_IDEAL_LINE_WORST_ERROR_RATIO="<<worst_ratio<<'\n';
}
void owner_adjacency(){
    auto f=blank();
    // Known manual owner partition, including equal-color reused content and
    // sprite-over-background visibility. Identity cannot be recovered from RGB.
    auto owner=[](int x,int y){return x<90?0:x<170?1:y<120?2:3;};
    for(auto&s:f.samples){int id=owner(s.x,s.y);s.background.nametable=std::uint16_t(id);
        if(id>=2){s.layer=Layer::sprite;s.sprite_opaque=true;s.sprite.origin_x=170;s.sprite.origin_y=id==2?0:120;s.sprite.oam_slot=std::uint8_t(id);}
        if((s.x+s.y)%24<12)paint(f,s.x,s.y,0x30);
    }
    f=*observe(f);auto ir=semantic_contours(f);
    for(int i=0;i<pixels;++i)for(auto cut:ir.cells[i].cuts)if(cut.donor>=0)
        check(owner(i%width,i/width)==owner(cut.donor%width,cut.donor/width),"independent manual owner partition forbids all cross-owner donors");
    auto pairs=[&](int w,int h){std::set<std::pair<int,int>> out;
        auto at_owner=[&](int x,int y){return owner(int(std::int64_t(2*x+1)*width/(2*w)),int(std::int64_t(2*y+1)*height/(2*h)));};
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int a=at_owner(x,y);
            for(auto step:std::array<std::array<int,2>,2>{{{1,0},{0,1}}}){int xx=x+step[0],yy=y+step[1];if(xx>=w||yy>=h)continue;
                int b=at_owner(xx,yy);if(a!=b)out.emplace(std::min(a,b),std::max(a,b));}}
        return out;
    };
    auto expected=pairs(width,height);
    for(auto size:std::array<std::array<int,2>,4>{{{257,241},{901,803},{1024,960},{2048,1920}}}){
        check(pairs(size[0],size[1])==expected,"independent owner adjacency set preserved at all extents");
        auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);
        for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x){int sx=int(std::int64_t(2*x+1)*width/(2*size[0])),sy=int(std::int64_t(2*y+1)*height/(2*size[1]));
            if(sx==89||sx==90||sx==169||sx==170||(sx>=170&&(sy==119||sy==120)))
                check(at(image,x,y)==digital_color(f.indices[sy*width+sx],0),"owner/occlusion adjacency boundary has exact color support");
        }
    }
}
void curves_and_phase_sweep(){
    auto f=blank();
    auto inside=[](double x,double y){double r=(x-128)*(x-128)+(y-120)*(y-120);return r<=1600&&r>=400;};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(inside(x+0.5,y+0.5))paint(f,x,y,0x30);
    f=*observe(f);double worst=0;
    for(auto size:std::array<std::array<int,2>,3>{{{1024,960},{1429,1317},{2048,1920}}}){
        auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);output_topology(f,image);
        double before=0,after=0;
        for(int y=70*size[1]/height;y<170*size[1]/height;++y)for(int x=78*size[0]/width;x<178*size[0]/width;++x){
            double xx=(x+0.5)*width/size[0],yy=(y+0.5)*height/size[1],r=(xx-128)*(xx-128)+(yy-120)*(yy-120);
            if(std::abs(r-1600)>180&&std::abs(r-400)>90)continue;
            int covered=0;for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx)
                covered+=inside((x+(sx+0.5)/16)*width/size[0],(y+(sy+0.5)/16)*height/size[1]);
            double truth=220.0*covered/256;int base=f.indices[int(yy)*width+int(xx)]==0x30?220:0;
            before+=std::abs(base-truth);after+=std::abs(at(image,x,y)[0]-truth);
        }
        check(after<before*0.95,"convex and concave curve area error improves >=5%");worst=std::max(worst,after/before);
    }
    std::cout<<"M4R1_CURVE_WORST_ERROR_RATIO="<<worst<<'\n';
    // A fixed integer generator, translated structure, and near-8x phases test
    // interactions without inferring topology from production component labels.
    std::uint32_t rng=0x471521u;
    for(int trial=0;trial<12;++trial){auto field=blank();
        for(int block=0;block<60;++block){rng=rng*1664525u+1013904223u;int x=80+int(rng%80);
            rng=rng*1664525u+1013904223u;int y=70+int(rng%80);
            rng=rng*1664525u+1013904223u;int w=1+int(rng%13);
            rng=rng*1664525u+1013904223u;int h=1+int(rng%13);
            rect(field,x,y,x+w,y+h,block%4?0x30:0x0f);
        }
        field=*observe(field);
        for(auto size:std::array<std::array<int,2>,3>{{{1027+trial*3,961+trial*5},{2048-trial,1920-trial},{521+trial,1799-trial}}})
            output_topology(field,render(field,Mode::SEMANTIC_HD,size[0],size[1]));
    }
}
// Independently construct coherent NES fetch coordinates. Assets deliberately
// vary across neighboring tiles, while the sampled name-table plane is continuous.
void tiled_source(Sample&s,int x,int y,bool varied=true){
    auto&b=s.background;
    b.nametable=std::uint16_t(0x2000|((x/256)&1)*0x400|((y/240)&1)*0x800|((y%240)/8)*32|((x%256)/8));
    b.attribute=std::uint16_t(0x23c0|(b.nametable&0xc00)|((b.nametable>>4)&0x38)|((b.nametable>>2)&7));
    b.origin_x=std::int16_t(x/8*8);b.origin_y=std::int16_t(y/8*8);
    b.local_x=std::uint8_t(x%8);b.local_y=std::uint8_t(y%8);
    b.tile=varied?std::uint8_t((x/8+3*(y/8))%32):1;b.pattern=std::uint16_t(b.tile*16);
    b.scroll_v=std::uint16_t((b.nametable&0xfff)|(b.local_y<<12));
    b.fine_x=std::uint8_t((s.y+3)%8);b.chr_source=17;
}
void spatial_authority(){
    using R=TopologyRelation;
    auto flat=blank(0x30);for(auto&s:flat.samples)tiled_source(s,s.x,s.y);
    flat=*observe(flat);auto graph=semantic_topology(flat);
    check(graph.components.regions.size()==1,"continuous tile plane is one spatial region despite per-tile Asset/Surface keys");
    auto flat_ir=semantic_contours(flat);
    for(const auto&cell:flat_ir.cells)check(!cell.material,"new spatial joins cannot enlarge the material domain");
    for(int y=1;y<height-1;y+=7)for(int x=1;x<width-1;++x){int i=y*width+x;
        check(graph.adjacency[i][1]==R::same_topology_region&&graph.adjacency[i][3]==R::same_topology_region,
              "coherent tile origins, attributes, scroll rows and fine-x are spatial continuation");
    }
    int seam=79*width+79;
    check(flat.samples[seam].asset_key!=flat.samples[seam+1].asset_key
        &&flat.samples[seam].surface_key!=flat.samples[seam+1].surface_key,"fixture crosses different source instances");
    check(!same_semantic_owner(flat.samples[seam],flat.samples[seam+8]),"spatial compatibility cannot act as a global asset equivalence");
    auto untiled=blank();for(auto&s:untiled.samples)if(s.x>=40+s.y/2)paint(untiled,s.x,s.y,0x30);
    untiled=*observe(untiled);auto original=semantic_contours(untiled);
    auto tiled=untiled;for(auto&s:tiled.samples)tiled_source(s,s.x,s.y);tiled=*observe(tiled);
    auto ir=semantic_contours(tiled);int across=0;
    for(int i=0;i<pixels;++i)for(int c=0;c<4;++c){const auto&a=original.cells[i].cuts[c];const auto&b=ir.cells[i].cuts[c];
        check(a.donor==b.donor&&a.reach_q8==b.reach_q8,"source tiling leaves eligible contour geometry identical");
        if(b.donor>=0&&tiled.samples[i].surface_key!=tiled.samples[b.donor].surface_key)++across;
    }
    check(across>20,"actual contour cuts cross locally continuous tile instances");
    for(auto size:std::array<std::array<int,2>,3>{{{1024,960},{1393,1301},{2048,1920}}}){
        auto a=render(untiled,Mode::SEMANTIC_HD,size[0],size[1]),b=render(tiled,Mode::SEMANTIC_HD,size[0],size[1]);
        output_topology(tiled,b);
        for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x){int sx=int(std::int64_t(2*x+1)*width/(2*size[0])),sy=int(std::int64_t(2*y+1)*height/(2*size[1]));
            if(std::abs(sx-40-sy/2)<=1)check(at(a,x,y)==at(b,x,y),"tile-spanning line retains independently gated contour quality");
        }
    }
    // Repeated content with identical keys is not sufficient: the source jumps
    // back a tile rather than advancing with neighboring screen coordinates.
    auto repeated=blank(0x30);for(auto&s:repeated.samples)tiled_source(s,s.x>=48&&s.x<56?s.x-8:s.x,s.y,false);
    repeated=*observe(repeated);auto split=semantic_topology(repeated);int i=10*width+47;
    check(repeated.samples[i].asset_key==repeated.samples[i+1].asset_key
        &&repeated.samples[i].surface_key==repeated.samples[i+1].surface_key,"repeat control has equal asset AND surface keys");
    check(split.adjacency[i][1]==R::hard_owner_boundary&&split.components.labels[i]!=split.components.labels[i+1],
          "repeated source coordinates cannot join disconnected spatial instances");
    for(int variant=0;variant<7;++variant){auto f=flat;
        for(auto&s:f.samples)if(s.x>=128){auto&b=s.background;
            if(variant==0)tiled_source(s,s.x+256,s.y); // Distinct name-table context.
            if(variant==1){b.chr_source++;}
            if(variant==2){b.high_chr_epoch++;}
            if(variant==3){b.palette=1;}
            if(variant==4){s.sprite_opaque=true;}
            if(variant==5){b.origin_x++;} // Contradictory redundant provenance.
            if(variant==6){b.local_x=std::uint8_t((b.local_x+1)%8);}
        }
        f=*observe(f);auto g=semantic_topology(f);
        check(g.adjacency[100*width+127][1]==(variant==3?R::palette_effect_boundary:variant==4?R::occlusion_boundary:R::hard_owner_boundary),
              "valid context/visibility split or broken source continuity vetoes spatial bridge");
        check(g.components.labels[100*width+127]!=g.components.labels[100*width+128],"equal appearance cannot override spatial conflict");
    }
    auto scroll=tiled;for(auto&s:scroll.samples)tiled_source(s,s.x+3,s.y+2);scroll=*observe(scroll);
    auto shifted=semantic_contours(scroll);
    for(int y=8;y<height-8;++y)for(int x=8;x<width-8;++x)for(int c=0;c<4;++c){int i=y*width+x;
        check(shifted.cells[i].cuts[c].donor==ir.cells[i].cuts[c].donor,"scroll phase changes tile seams, not spatial geometry");
    }
    auto stale=tiled;stale.number+=19;for(auto&s:stale.samples){s.previous_surface^=91;s.motion_x=17;s.motion_y=-11;s.confidence=255;s.reactive=Reactive::ambiguous;}
    check(render(stale,Mode::SEMANTIC_HD)==render(tiled,Mode::SEMANTIC_HD),"new tile continuity has no temporal authority");
    auto atlas=topology_atlas();for(auto&s:atlas.samples)tiled_source(s,s.x,s.y);atlas=*observe(atlas);
    for(auto size:std::array<std::array<int,2>,3>{{{1024,960},{2047,1919},{521,1799}}})
        output_topology(atlas,render(atlas,Mode::SEMANTIC_HD,size[0],size[1]));
}
void sprite_spatial_identity(){
    auto f=blank();
    for(auto&s:f.samples)if(s.x>=80&&s.x<88&&s.y>=80&&s.y<88){
        s.layer=Layer::sprite;s.sprite_opaque=true;s.sprite.origin_x=80;s.sprite.origin_y=80;
        s.sprite.local_x=std::uint8_t(s.x-80);s.sprite.local_y=std::uint8_t(s.y-80);
        s.sprite.tile=1;s.sprite.pattern=16;s.sprite.oam_slot=3;
        if(s.x>=s.y)paint(f,s.x,s.y,0x30);
    }
    f=*observe(f);auto slots=f;for(auto&s:slots.samples)if(s.layer==Layer::sprite)s.sprite.oam_slot=std::uint8_t(s.y%8+8);
    slots=*observe(slots);auto a=semantic_topology(f),b=semantic_topology(slots);
    check(a.components.labels==b.components.labels&&a.adjacency==b.adjacency,"OAM winner slot changes do not split identical visible placement");
    int cuts=0;for(const auto&cell:semantic_contours(slots).cells)for(auto cut:cell.cuts)cuts+=cut.donor>=0;
    check(cuts>0,"sprite slot fixture exercises actual contour coverage");
    check(render(f,Mode::SEMANTIC_HD,2048,1920)==render(slots,Mode::SEMANTIC_HD,2048,1920),"sprite contour independent of OAM slot ordering");
    auto misplaced=f;for(auto&s:misplaced.samples)if(s.layer==Layer::sprite&&s.y>=84)s.sprite.origin_y+=8;
    misplaced=*observe(misplaced);auto c=semantic_topology(misplaced);
    check(c.adjacency[83*width+86][3]==TopologyRelation::hard_owner_boundary,"same OAM slot cannot join conflicting sprite placement");
    auto hidden=slots;for(auto&s:hidden.samples)if(s.layer==Layer::sprite){s.background.origin_x+=64;s.background.chr_source^=23;s.background.fine_x^=7;}
    check(render(hidden,Mode::SEMANTIC_HD)==render(slots,Mode::SEMANTIC_HD),"hidden transform/source changes grant no sprite geometry authority");
}
void local_saddles_and_lobes(){
    for(int t=0;t<4;++t){auto f=saddle_fixture(t);for(auto&s:f.samples)tiled_source(s,s.x+3,s.y+2);f=*observe(f);
        auto ir=semantic_contours(f);int x=t&1?width-102:100,y=t&2?height-102:100,i=y*width+x;
        check(ir.diagonals[i]==((t==0||t==3)?DiagonalResolution::connected_ad:DiagonalResolution::connected_bc),
              "local island continuation resolves saddle across source tile coordinates");
        output_topology(f,render(f,Mode::SEMANTIC_HD,2047,1919));
    }
    // Same global connectivity as a resolvable fixture, but continuation is far
    // outside the saddle neighborhood. A label alone cannot choose its pairing.
    auto remote=blank(0x30);rect(remote,101,70,132,101,0x0f);rect(remote,70,101,101,132,0x0f);
    remote=*observe(remote);auto ir=semantic_contours(remote);int i=100*width+100;
    check(ir.topology.components.labels[i]==ir.topology.components.labels[i+width+1],"remote control has global four-connected continuation");
    check(ir.diagonals[i]==DiagonalResolution::ambiguous_diagonal,"remote bridge alone cannot resolve a local saddle");
    for(int n:{i,i+1,i+width,i+width+1})check(ir.cells[n].locked,"uncertain local continuation remains exact");
    auto compact=blank();rect(compact,80,80,83,83,0x30);paint(compact,82,80,0x0f);compact=*observe(compact);
    auto lobe=semantic_contours(compact);int cuts=0;
    for(int y=80;y<83;++y)for(int x=80;x<83;++x)if(compact.indices[y*width+x]==0x30){int n=y*width+x;
        check(lobe.topology.components.regions[lobe.topology.components.labels[n]-1].protected_fine,"compact feature retains protected-fine evidence");
        for(auto cut:lobe.cells[n].cuts)cuts+=cut.donor>=0;
    }
    check(cuts>=2,"protected compact lobe without a 3x3 core admits certified boundary coverage");
    for(auto size:std::array<std::array<int,2>,5>{{{257,241},{1024,960},{2047,1919},{2048,1920},{521,1799}}})
        output_topology(compact,render(compact,Mode::SEMANTIC_HD,size[0],size[1]));
    auto corner=blank();rect(corner,80,80,96,96,0x30);
    corner.samples[79*width+81].background.nametable=1;corner=*observe(corner);
    auto guarded=semantic_contours(corner);
    check(guarded.cells[80*width+80].cuts[0].donor>=0,"unrelated diagonal hard owner does not freeze a certified corner");
    for(int n:{79*width+81,79*width+80,79*width+82,78*width+81,80*width+81})
        check(guarded.cells[n].locked,"all cells incident to the hard edge remain exact");
    for(int n=0;n<pixels;++n)for(auto cut:guarded.cells[n].cuts)if(cut.donor>=0)
        check(corner.samples[n].background.nametable==corner.samples[cut.donor].background.nametable,"corner-local eligibility cannot cross a hard owner");
    for(auto size:std::array<std::array<int,2>,2>{{{901,803},{2048,1920}}})output_topology(corner,render(corner,Mode::SEMANTIC_HD,size[0],size[1]));
}
void multicolor_surface(){
    constexpr std::array<std::uint8_t,3> palette{0x16,0x1a,0x12};
    auto f=blank(palette[0]);
    for(auto&s:f.samples){int category=s.x>=48+s.y/2?1:0;if(s.y>40+s.x/2&&s.x<190)category=2;
        paint(f,s.x,s.y,palette[category]);tiled_source(s,s.x,s.y);
    }
    f=*observe(f);auto ir=semantic_contours(f);int cuts=0;
    for(int i=0;i<pixels;++i)for(auto cut:ir.cells[i].cuts)if(cut.donor>=0){++cuts;
        check(f.indices[i]!=f.indices[cut.donor]&&f.samples[i].layer==f.samples[cut.donor].layer,
              "multi-color contour uses a visible same-surface appearance boundary");
    }
    check(cuts>100,"multi-color appearance relations emit actual contour geometry");
    auto adjacency=[](const std::vector<int>&categories,int w,int h){std::set<std::pair<int,int>> pairs;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){int a=categories[y*w+x];
            for(auto step:std::array<std::array<int,2>,2>{{{1,0},{0,1}}}){int xx=x+step[0],yy=y+step[1];
                if(xx>=w||yy>=h)continue;
                int b=categories[yy*w+xx];if(a!=b)pairs.emplace(std::min(a,b),std::max(a,b));}}
        return pairs;
    };
    std::vector<int> source_categories(pixels);for(int i=0;i<pixels;++i)
        source_categories[i]=int(std::find(palette.begin(),palette.end(),f.indices[i])-palette.begin());
    const auto expected_adjacency=adjacency(source_categories,width,height);
    std::array<Color,3> colors{};for(int k=0;k<3;++k)colors[k]=digital_color(palette[k],0);
    for(auto size:std::array<std::array<int,2>,4>{{{1024,960},{1393,1301},{2048,1920},{521,1799}}}){
        auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);std::vector<int> decoded(static_cast<std::size_t>(size[0])*size[1]);int mixed=0;
        for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x){auto c=at(image,x,y);int best=INT_MAX,category=-1;
            for(int k=0;k<3;++k){int d=0;for(int j=0;j<3;++j)d+=(int(c[j])-colors[k][j])*(int(c[j])-colors[k][j]);
                if(d<best){best=d;category=k;}}
            decoded[std::size_t(y)*size[0]+x]=category;
            int sx=int(std::int64_t(2*x+1)*width/(2*size[0])),sy=int(std::int64_t(2*y+1)*height/(2*size[1])),i=sy*width+sx;
            bool active=false;Color low=digital_color(f.indices[i],0),high=low;
            for(auto cut:ir.cells[i].cuts)if(cut.donor>=0){active=true;auto donor=digital_color(f.indices[cut.donor],0);
                for(int k=0;k<3;++k){low[k]=std::min(low[k],donor[k]);high[k]=std::max(high[k],donor[k]);}}
            if(active){bool bounded=true;for(int k=0;k<3;++k)bounded&=c[k]>=low[k]&&c[k]<=high[k];
                check(bounded,"multicolor boundary stays in local observed envelope");mixed+=best>4;}
        }
        check(mixed>100,"multi-color subpixel coverage active independently of material gain");
        check(adjacency(decoded,size[0],size[1])==expected_adjacency,"independent multicolor appearance adjacency unchanged");
        for(int k=0;k<3;++k){std::vector<bool> src(pixels),dst(decoded.size());
            for(int i=0;i<pixels;++i)src[i]=f.indices[i]==palette[k];
            for(std::size_t i=0;i<dst.size();++i)dst[i]=decoded[i]==k;
            check(topology(src,width,height)==topology(dst,size[0],size[1]),"independently decoded multicolor components and holes preserved");
        }
    }
}
std::array<int,2> symmetry(int x,int y,int side,int t){
    if(t&4)x=side-1-x;
    for(int k=0;k<(t&3);++k){int old=x;x=side-1-y;y=old;}
    return {x,y};
}
int corner_symmetry(int c,int t){auto p=symmetry(c&1,c>>1,2,t);return p[0]+2*p[1];}
int edge_symmetry(int e,int t){
    auto p=symmetry(e==0?0:e==1?2:1,e==2?0:e==3?2:1,3,t);
    return p[0]==0?0:p[0]==2?1:p[1]==0?2:3;
}
void receiver_symmetries(){
    // Exhaust every binary 3x3 neighborhood (including complements), embedded
    // independently in twelve-cell squares. D4 acts on the observations only;
    // expected geometry and pixels are transformed independently of the builder.
    std::uint64_t corners=0,edges=0,mixed_cells=0;
    for(int batch=0;batch<2;++batch){auto base=blank();
        for(int k=0;k<256;++k)for(int bit=0;bit<9;++bit)if(((batch*256+k)>>bit)&1)
            paint(base,8+(k%16)*12+4+bit%3,8+(k/16)*12+4+bit/3,0x30);
        base=*observe(base);auto original=semantic_contours(base);
        auto reference=render(base,Mode::SEMANTIC_HD,2048,1920);
        for(int t=0;t<8;++t){auto f=blank();
            for(int k=0;k<256;++k)for(int bit=0;bit<9;++bit)if(((batch*256+k)>>bit)&1){
                auto p=symmetry(4+bit%3,4+bit/3,12,t);
                paint(f,8+(k%16)*12+p[0],8+(k/16)*12+p[1],0x30);
            }
            f=*observe(f);auto ir=semantic_contours(f);auto image=render(f,Mode::SEMANTIC_HD,2048,1920);
            output_topology(f,image);
            // Every source center and its cardinal corridors retain categorical
            // support, independently decoded from RGB, including all thin cells.
            for(int k=0;k<256;++k)for(int y=2;y<10;++y)for(int x=2;x<10;++x){
                int ox=8+(k%16)*12,oy=8+(k/16)*12,i=(oy+y)*width+ox+x;
                auto p=symmetry(x,y,12,t);int n=(oy+p[1])*width+ox+p[0];
                const auto&a=original.cells[i];const auto&b=ir.cells[n];bool active=false;
                if(t==0){mixed_cells+=a.eligible_corners!=0&&a.eligible_corners!=15;
                    for(auto cut:a.cuts)corners+=cut.donor>=0;
                    for(int donor:a.edge_donors)edges+=donor>=0;}
                for(int c=0;c<4;++c){int d=corner_symmetry(c,t);
                    check(bool(a.eligible_corners&(1<<c))==bool(b.eligible_corners&(1<<d)),"exhaustive D4 local eligibility equivalent");
                    check((a.cuts[c].donor>=0)==(b.cuts[d].donor>=0)&&a.cuts[c].reach_q8==b.cuts[d].reach_q8,
                          "exhaustive D4 emitted corner geometry equivalent");
                    check((a.edge_donors[c]>=0)==(b.edge_donors[edge_symmetry(c,t)]>=0),"exhaustive D4 edge eligibility equivalent");
                    active|=a.cuts[c].donor>=0||a.edge_donors[c]>=0;
                }
                for(auto q:std::array<std::array<int,2>,5>{{{4,4},{0,4},{7,4},{4,0},{4,7}}})
                    check((at(image,(ox+p[0])*8+q[0],(oy+p[1])*8+q[1])[0]>110)==(f.indices[n]==0x30),
                          "every permitted cut retains source center and cardinal support");
                if(active)for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx){
                    auto q=symmetry(x*8+xx,y*8+yy,96,t);
                    check(at(reference,(ox+x)*8+xx,(oy+y)*8+yy)==at(image,ox*8+q[0],oy*8+q[1]),
                          "exhaustive D4 fixed-point boundary raster equivalent");
                }
            }
            // Noninteger and anisotropic footprints must preserve the same
            // components and holes, not merely their canonical cell centers.
            for(auto size:std::array<std::array<int,2>,2>{{{769+t,721+2*t},{521+t,1799-t}}})
                output_topology(f,render(f,Mode::SEMANTIC_HD,size[0],size[1]));
        }
    }
    check(corners>0&&edges>0&&mixed_cells>0,"exhaustive controls exercise both geometry types and mixed eligibility");
    std::cout<<"M4R04_EXHAUSTIVE_CORNERS="<<corners<<" EDGES="<<edges<<" MIXED_CELLS="<<mixed_cells<<'\n';
}
Frame local_receiver_fixture(int t,int obstruction){
    auto f=blank(0x30);
    // Two compact six-cell receivers, with no 3x3 core. Their outward saddle
    // corners have inward support; their other corners contain terminals.
    constexpr std::array<std::array<int,2>,6> lobe{{{0,0},{1,0},{0,-1},{1,-1},{1,-2},{2,-2}}};
    for(auto p:lobe)for(int side=0;side<2;++side){int x=side?5-p[0]:6+p[0],y=side?6-p[1]:5+p[1];
        auto q=symmetry(x,y,12,t);paint(f,96+q[0],96+q[1],0x0f);}
    if(obstruction){
        // Terminal: remove both inward arms. Neck: retain only the bent 1px
        // connection. Hole: complete a thin ring around the missing diagonal.
        auto set=[&](int x,int y,std::uint8_t color){auto q=symmetry(x,y,12,t);paint(f,96+q[0],96+q[1],color);};
        if(obstruction==1){set(7,5,0x30);set(6,4,0x30);}
        if(obstruction>=2)set(7,4,0x30);
        if(obstruction==3){set(6,3,0x0f);set(8,4,0x0f);set(8,5,0x0f);}
    }
    return *observe(f);
}
void local_receiver_contract(){
    for(int t=0;t<8;++t){auto f=local_receiver_fixture(t,0);auto ir=semantic_contours(f);
        int nw=101*width+101;auto p=symmetry(6,5,12,t);int i=(96+p[1])*width+96+p[0];
        int c=corner_symmetry(2,t);
        check(ir.topology.components.regions[ir.topology.components.labels[i]-1].protected_fine,"receiver remains a protected fine region");
        check((ir.cells[i].eligible_corners&(1<<c))&&!(ir.cells[i].eligible_corners&(1<<(c^3))),
              "same protected receiver has an eligible and a forbidden corner");
        check(ir.cells[i].cuts[c].donor>=0,"local receiver succeeds despite protected terminal at another corner");
        check(ir.diagonals[nw]==((corner_symmetry(0,t)==0||corner_symmetry(0,t)==3)?DiagonalResolution::connected_ad:DiagonalResolution::connected_bc),
              "unique component continuation succeeds with only affected corners safe");
        for(int obstruction=1;obstruction<=3;++obstruction){auto bad=local_receiver_fixture(t,obstruction);auto blocked=semantic_contours(bad);
            check(blocked.diagonals[nw]==DiagonalResolution::ambiguous_diagonal,"affected terminal, neck or hole prevents saddle resolution");
            check(blocked.cells[i].cuts[c].donor<0,"affected protected corner stays exact");
            output_topology(bad,render(bad,Mode::SEMANTIC_HD,2047,1919));
        }
        for(int conflict=0;conflict<4;++conflict){auto bad=f;auto&s=bad.samples[i];
            if(conflict==0)s.background.origin_x=8; // Equal AssetKey and SurfaceKey, distinct context.
            if(conflict==1)s.sprite_opaque=true;
            if(conflict==2){s.emphasis=1;bad.effects[i]=32;}
            if(conflict==3)s.background.palette=1;
            bad=*observe(bad);auto blocked=semantic_contours(bad);
            if(conflict==0)check(bad.samples[i].asset_key==f.samples[i].asset_key&&bad.samples[i].surface_key==f.samples[i].surface_key,
                                "receiver conflict control retains repeated keys");
            check(blocked.diagonals[nw]!=DiagonalResolution::connected_ad&&blocked.diagonals[nw]!=DiagonalResolution::connected_bc&&blocked.cells[i].cuts[c].donor<0,
                  "hard context, occlusion, effect and palette boundaries cannot supply saddle coverage");
            for(const auto&cell:blocked.cells){for(auto cut:cell.cuts)check(cut.donor!=i,"foreign receiver never becomes a corner donor");
                for(int donor:cell.edge_donors)check(donor!=i,"foreign receiver never becomes an edge donor");}
        }
        output_topology(f,render(f,Mode::SEMANTIC_HD,2048,1920));
    }
    // Fine interiors remain exact while the outside of their silhouette receives
    // boundary AA. Includes an isolated point, glyph, neck, ring and tiny hole.
    auto f=blank();paint(f,40,40,0x30);
    rect(f,60,40,61,49,0x30);rect(f,60,40,66,41,0x30);rect(f,60,44,65,45,0x30);
    rect(f,80,40,85,45,0x30);rect(f,90,40,95,45,0x30);rect(f,85,42,90,43,0x30);
    rect(f,110,40,119,49,0x30);rect(f,111,41,118,48,0x0f);
    rect(f,130,40,139,49,0x30);paint(f,134,44,0x0f);f=*observe(f);
    auto ir=semantic_contours(f);int isolated_edges=0;
    for(const auto&cell:ir.cells)for(int donor:cell.edge_donors)isolated_edges+=donor==40*width+40;
    check(isolated_edges==4,"isolated point has four safe exterior silhouette edges");
    for(auto size:std::array<std::array<int,2>,5>{{{1024,960},{2048,1920},{2047,1919},{257,241},{521,1799}}}){
        auto image=render(f,Mode::SEMANTIC_HD,size[0],size[1]);output_topology(f,image);
        std::array<int,5> activity{};
        for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x){
            int sx=int(std::int64_t(2*x+1)*width/(2*size[0])),sy=int(std::int64_t(2*y+1)*height/(2*size[1]));
            int i=sy*width+sx;auto color=at(image,x,y);bool exterior=false,cut=false;
            for(int donor:ir.cells[i].edge_donors)exterior|=donor>=0;
            for(auto corner:ir.cells[i].cuts)cut|=corner.donor>=0;
            auto source=digital_color(f.indices[i],0);
            if(exterior&&!cut)check(std::abs(int(color[0])-source[0])<=55,"exterior fine coverage never reaches categorical majority");
            constexpr std::array<int,5> feature_x{40,60,85,110,130};
            if(exterior&&color!=source)for(int k=0;k<5;++k)if(sx>=feature_x[k]-2&&sx<=feature_x[k]+11&&sy>=38&&sy<=50)++activity[k];
            if(i==40*width+40||(sx>=60&&sx<66&&sy>=40&&sy<49&&f.indices[i]==0x30)
               ||(sy==42&&sx>=85&&sx<90)||(sx>=110&&sx<119&&sy>=40&&sy<49&&f.indices[i]==0x30)||i==44*width+134)
                check(color==digital_color(f.indices[i],0),"isolated, glyph, neck, ring and tiny hole interiors stay exact");
        }
        if(size[0]>=1024)for(int k=0;k<4;++k)check(activity[k]>0,"protected feature receives real exterior AA");
    }
}
void visual_witness(const std::filesystem::path&dir){
    if(!dir.is_absolute())throw std::invalid_argument("visual evidence directory must be absolute");
    auto raw=blank(0x11);
    for(int y=20;y<210;++y)for(int x=20;x<236;++x){
        if(x>=50+y/2)paint(raw,x,y,0x30);
    }
    rect(raw,30,40,90,100,0x28);rect(raw,42,52,78,88,0x11);
    rect(raw,32,160,72,161,0x30);rect(raw,32,163,72,164,0x30);
    for(int n=0;n<20;++n)paint(raw,35+n,180+n,0x30);
    for(int y=30;y<38;++y)for(int x=180;x<188;++x)if((x+y)&1)paint(raw,x,y,0x0f);
    auto f=observe(raw);std::filesystem::create_directories(dir);
    auto atlas=topology_atlas();auto saddle=saddle_fixture(0);
    for(int scale:{4,8})for(auto mode:{Mode::EXACT_DIGITAL,Mode::SEMANTIC_HD}){
        auto suffix=std::to_string(scale)+"x-"+std::to_string(static_cast<int>(mode))+".ppm";
        write_ppm(dir/("topology-atlas-"+suffix),render(atlas,mode,width*scale,height*scale));
        write_ppm(dir/("resolved-saddle-"+suffix),render(saddle,mode,width*scale,height*scale));
    }
    for(auto mode:{Mode::EXACT_DIGITAL,Mode::AUTHENTIC_SIGNAL,Mode::SEMANTIC_HD})
        write_ppm(dir/("synthetic-4x-"+std::to_string(static_cast<int>(mode))+".ppm"),render(*f,mode,1024,960));
    for(auto mode:{Diagnostic::layer,Diagnostic::asset,Diagnostic::protected_structure,Diagnostic::motion,Diagnostic::reactive})
        write_ppm(dir/("synthetic-debug-"+std::to_string(static_cast<int>(mode))+".ppm"),render_diagnostic(*f,mode));
}
}
int main(int argc,char**argv){
    try{exact_compatibility();boundaries();fine_structures();ownership();signal_and_material();diagnostics_and_bounds();invariance();topology_graph_contract();saddle_resolution();contour_topology_and_support();ideal_contour_error();owner_adjacency();curves_and_phase_sweep();spatial_authority();sprite_spatial_identity();local_saddles_and_lobes();multicolor_surface();receiver_symmetries();local_receiver_contract();}
    catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    try{if(argc==3&&std::string(argv[1])=="--visual-dir")visual_witness(argv[2]);
        else if(argc!=1)throw std::invalid_argument("usage: vkrt_nes_m4r_tests [--visual-dir ABSOLUTE_DIR]");}
    catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    check(checks>=5000,"M4R minimum substantive checks");
    std::cout<<"CHECKS="<<checks<<" FAILURES="<<failures<<'\n';return failures?1:0;
}
