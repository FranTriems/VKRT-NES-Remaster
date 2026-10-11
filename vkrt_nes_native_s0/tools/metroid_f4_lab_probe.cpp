#include "vkrt/nes_native_s0/core.hpp"
#include "vkrt/nes_native_s0/metroid_smrc.hpp"
#include <metroid_lab_png.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <iterator>

using namespace vkrt::nes;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string canonical(const Machine& m,const Encoder& audio){
    Encoder e;e.data(m.serialize());e.data(m.save_state());e.data(audio.bytes);
    e.data(m.frame()->indices);e.data(m.frame()->effects);return sha256(e.bytes);
}
void capture_audio(Machine& m,Encoder& audio){for(auto sample:m.apu.pcm)audio.u16(std::uint16_t(sample));m.apu.pcm.clear();}
struct Difference {unsigned pixels{};double mean{};};
Difference difference(const Image& a,const Image& b){Difference r;std::uint64_t sum=0;
    for(std::size_t i=0;i<a.rgb.size();i+=3){bool changed=false;for(int k=0;k<3;++k){int d=std::abs(int(a.rgb[i+k])-b.rgb[i+k]);changed|=d!=0;sum+=d;}r.pixels+=changed;}
    r.mean=double(sum)/a.rgb.size();return r;}
template<class F>std::array<double,3> measure(F fn){
    std::vector<double> times;for(int n=0;n<21;++n){auto t=std::chrono::steady_clock::now();fn();auto end=std::chrono::steady_clock::now();
        if(n)times.push_back(std::chrono::duration<double,std::milli>(end-t).count());}
    std::sort(times.begin(),times.end());return {times.front(),times[times.size()/2],times[times.size()*95/100]};
}
struct View {const char* name;SmrcView view;};
constexpr View views[]{{"height",SmrcView::height},{"normal",SmrcView::normal},{"ao",SmrcView::ao},
    {"contact",SmrcView::contact},{"light",SmrcView::light},{"emissive-local-light",SmrcView::emissive},
    {"material",SmrcView::material},{"owner",SmrcView::owner},{"identity",SmrcView::identity},
    {"churn",SmrcView::churn},{"coverage-owner-check",SmrcView::coverage},{"delta",SmrcView::delta},{"albedo",SmrcView::albedo}};
}
int main(int argc,char** argv){try{
    std::filesystem::path rom,out;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i];require(i+1<argc,"expected --rom and --out");
        if(arg=="--rom")rom=argv[++i];else if(arg=="--out")out=argv[++i];else throw std::runtime_error("unknown option");}
    require(!rom.empty()&&!out.empty(),"expected --rom and --out");
    std::ifstream input(rom,std::ios::binary);require(bool(input),"cannot read lab input");
    Bytes bytes((std::istreambuf_iterator<char>(input)),{});
    require(presentation_profile(sha256(bytes))==PresentationProfile::metroid_usa_f1f4,"lab digest mismatch");
    const auto info=inspect_ines(bytes);require(info.mapper==1&&info.chr_size==0,"lab header mismatch");
    std::filesystem::create_directories(out);
    auto f1_machine=std::make_unique<Machine>(bytes),f4_machine=std::make_unique<Machine>(bytes);
    bytes.clear();
    auto baseline=std::make_unique<ModernScene>();
    auto scene=std::make_unique<ModernScene>(PresentationProfile::metroid_usa_f1f4);
    Encoder audio1,audio4;
    std::ofstream report(out/"report.json"),performance(out/"performance.json");
    report.exceptions(std::ios::failbit|std::ios::badbit);performance.exceptions(std::ios::failbit|std::ios::badbit);
    report<<std::setprecision(10);performance<<std::setprecision(8);
    report<<"{\n\"schema\":\"vkrt.nes.metroid_f4_lab.v2\",\n\"rom_sha256\":\""<<metroid_digest
        <<"\",\n\"profile\":\"metroid_usa_f1f4\",\n\"mode_policy\":\"F1_F4_ONLY\",\n\"mapper\":1,\n\"chr_rom_bytes\":0,\n"
        <<"\"commercial_rom_scope\":\"Deterministic power-on, title, start menu and first Brinstar room only; no imported or invented save states.\",\n"
        <<"\"thresholds_source\":\"Measured retained R5.2 baseline; validation/METROID_F4_THRESHOLDS.json. Exact invariants are exact; visual quality has no scalar pass threshold.\",\n"
        <<"\"route\":{\"frames\":1000,\"start_held\":[[241,248],[361,368]],\"right_held\":[[700,819]],\"otherwise_buttons\":0},\n"
        <<"\"churn_scope\":\"Retained scene observes checkpoint snapshots, not every emulated frame.\",\n"
        <<"\"checkpoints\":[\n";
    performance<<"{\"unit\":\"milliseconds\",\"clock\":\"steady_clock\",\"samples\":20,\"warmup\":1,\"extent\":[512,480],\"checkpoints\":[\n";
    bool first=true;unsigned transformed=0;
    for(unsigned frame=1;frame<=1000;++frame){
        auto buttons=std::uint8_t((frame>=241&&frame<=248)||(frame>=361&&frame<=368)?8:frame>=700&&frame<820?128:0);
        f1_machine->controllers[0].buttons=buttons;f4_machine->controllers[0].buttons=buttons;
        f1_machine->run_frames(1);f4_machine->run_frames(1);
        capture_audio(*f1_machine,audio1);capture_audio(*f4_machine,audio4);
        if(frame!=180&&frame!=340&&frame!=600&&frame!=820&&frame!=1000)continue;
        const char* name=frame==180?"title":frame==340?"start-menu":frame==600?"arrival":frame==820?"first-room":"first-room-enemies";
        auto f=f4_machine->frame();auto start=std::chrono::steady_clock::now();observe_modern(*scene,f);
        const double compile_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const auto stats=scene->stats();observe_modern(*baseline,f);
        const auto before=canonical(*f4_machine,audio4);
        auto f1=render_profile(*f1_machine->frame(),Mode::EXACT_DIGITAL,PresentationProfile::metroid_usa_f1f4,512,480);
        SmrcDiagnostics diag;Image f4;raster_metroid_smrc_into(f4,*scene,{},512,480,&diag);
        auto legacy=render_modern_scene(*baseline,{},512,480);
        require(render_profile(*f,Mode::MODERN_REMASTER,PresentationProfile::metroid_usa_f1f4,512,480)==f4,"frontend/lab render mismatch");
        const auto digest1=canonical(*f1_machine,audio1),digest4=canonical(*f4_machine,audio4);
        require(before==digest4&&digest1==digest4,"canonical state/frame/audio/persistence changed");
        require(render_modern_scene(*scene,{},512,480)==f4,"non-deterministic repeat");
        require(render_modern_scene(*scene,{0,0},512,480)==f1,"neutral controls do not preserve F1");
        require(diag.coverage_xor_pixels==0&&diag.owner_mismatch_pixels==0,"coverage or owner changed");
        for(int y=0;y<480;++y)for(int x=0;x<512;++x)require(diag.source_cells[y*512+x]==unsigned((y/2)*256+x/2),"source-cell displacement");
        const auto stem=std::to_string(frame)+"-";
        const auto h1=lab::write_png(out/(stem+"f1.png"),f1),h4=lab::write_png(out/(stem+"f4.png"),f4);
        const auto hb=lab::write_png(out/(stem+"baseline.png"),legacy);
        for(const auto& view:views)lab::write_png(out/(stem+view.name+".png"),render_smrc_diagnostic(*f,f1,f4,diag,view.view));
        auto diff=difference(f1,f4),old=difference(f1,legacy);transformed+=diff.pixels>0;
        std::array<unsigned,4> materials{};unsigned protected_pixels=0,fallback_pixels=0,contact_pixels=0;
        for(const auto& t:diag.texels){++materials[unsigned(t.relief.family)];protected_pixels+=t.protected_flat;fallback_pixels+=t.fallback;contact_pixels+=t.light.contact>0;}
        Image sheet{1536,480,Bytes(1536*480*3)};
        for(int y=0;y<480;++y)for(int n=0;n<3;++n){const auto& im=n==0?f1:n==1?legacy:f4;
            std::copy_n(im.rgb.begin()+y*512*3,512*3,sheet.rgb.begin()+(y*1536+n*512)*3);}
        lab::write_png(out/(stem+"comparison.png"),sheet);
        Image temp;auto old_time=measure([&]{raster_modern_scene_into(temp,*baseline,{},512,480);});
        auto new_time=measure([&]{raster_modern_scene_into(temp,*scene,{},512,480);});
        auto observe_time=measure([&]{observe_modern(*scene,f);});
        require(scene->stats().shape_builds==0&&scene->stats().form_builds==0&&scene->stats().background_updates==0,"steady retained cache churn");
        if(!first){report<<",\n";performance<<",\n";}first=false;
        report<<"{\"name\":\""<<name<<"\",\"frame\":"<<frame<<",\"canonical_digest_f1\":\""<<digest1<<"\",\"canonical_digest_f4\":\""<<digest4
            <<"\",\"f1_sha256\":\""<<h1<<"\",\"f4_sha256\":\""<<h4<<"\",\"baseline_sha256\":\""<<hb<<"\",\"hash_encoding\":\"PNG bytes\","
            <<"\"coverage_xor_pixels\":"<<diag.coverage_xor_pixels<<",\"owner_mismatch_pixels\":"<<diag.owner_mismatch_pixels
            <<",\"different_pixels\":"<<diff.pixels<<",\"different_percent\":"<<100.*diff.pixels/(512*480)<<",\"mean_abs_channel_delta\":"<<diff.mean
            <<",\"baseline_different_pixels\":"<<old.pixels<<",\"baseline_mean_abs_channel_delta\":"<<old.mean
            <<",\"spans\":"<<stats.spans<<",\"shape_builds\":"<<stats.shape_builds<<",\"form_builds\":"<<stats.form_builds
            <<",\"background_updates\":"<<stats.background_updates<<",\"sprite_updates\":"<<stats.sprite_updates<<",\"unstable_spans\":"<<stats.unstable_spans
            <<",\"protected_pixels\":"<<protected_pixels<<",\"fallback_pixels\":"<<fallback_pixels<<",\"contact_pixels\":"<<contact_pixels
            <<",\"material_pixels_flat_bevel_round_porous\":["<<materials[0]<<","<<materials[1]<<","<<materials[2]<<","<<materials[3]<<"]}";
        performance<<"{\"frame\":"<<frame<<",\"observe_cold_or_transition_ms\":"<<compile_ms
            <<",\"baseline_shade_median_ms\":"<<old_time[1]<<",\"smrc_shade_min_ms\":"<<new_time[0]<<",\"smrc_shade_median_ms\":"<<new_time[1]
            <<",\"smrc_shade_p95_ms\":"<<new_time[2]<<",\"observe_warm_median_ms\":"<<observe_time[1]<<",\"shade_ratio\":"<<new_time[1]/old_time[1]<<"}";
        std::cout<<name<<" frame="<<frame<<" changed="<<diff.pixels<<" shade_ms="<<new_time[1]<<"\n";
    }
    require(transformed>=2,"fewer than two transformed checkpoints");
    report<<"],\n\"canonical_invariance\":true,\n\"deterministic_repeat\":true,\n\"diagnostics\":[\"f1\",\"f4\",\"baseline\",\"comparison\"";
    for(const auto& view:views)report<<",\""<<view.name<<"\"";
    report<<"],\n\"performance\":{\"measurements\":\"performance.json\",\"timings_excluded_from_deterministic_report\":true,\"steady_geometry_rebuilds\":0,\"legacy_observer_calls\":0,\"shade_location\":\"presentation CPU\"},\n"
        <<"\"limitations\":[\"Only title/menu/first room, not all areas or enemy classes.\",\"No validated emission authority on this ROM: emissive/local-light view is zero.\","
        <<"\"HUD band and topology-thin forms are conservatively flat; material names are geometric hypotheses, not game object labels.\","
        <<"\"Bounded fetched 3x3 context; unseen/irregular neighbors are empty and unstable fetches fall back to F1.\","
        <<"\"Provenance exposes logical nametable coordinates, not physical mirrored VRAM IDs; no guessed mirroring is fed back into the core.\","
        <<"\"Timing is a local CPU sample, not a physical presenter/audio qualification. Visual improvement requires human adjudication.\"]\n}\n";
    performance<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
