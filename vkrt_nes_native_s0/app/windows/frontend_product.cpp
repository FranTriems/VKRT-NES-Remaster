#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <frontend_product.hpp>
#include <frontend_config.hpp>
#include <runtime_audio.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace vkrt::nes::frontend {
Bytes read_product_file(const std::filesystem::path& path,std::size_t cap){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("Save file could not be opened");
    auto size=f.tellg();
    if(size<0||size>std::streamoff(cap))throw std::runtime_error("Save file exceeds size limit");
    Bytes bytes(static_cast<std::size_t>(size));f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!f||f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Save file changed or is incomplete");
    return bytes;
}
void atomic_product_write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){
    if(bytes.empty()||bytes.size()>max_state_bytes)throw std::runtime_error("Save size outside bound");
    std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Temporary save file unavailable; previous save retained");
    DWORD count{};
    bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size();
    if(ok)ok=FlushFileBuffers(file)!=0;
    if(!CloseHandle(file))ok=false;
    if(ok){
        if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)ok=ReplaceFileW(path.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0;
        else ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
    }
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Save replacement failed; previous save retained");}
}
namespace {
std::wstring identity(const Machine& m){std::wostringstream s;s<<std::hex<<std::setfill(L'0')<<std::setw(16)<<m.rom_identity();return s.str();}
}
void ProductStore::validate_root() const {if(root_.empty()||!root_.is_absolute())throw std::runtime_error("Local save storage unavailable");}
std::filesystem::path ProductStore::state_path(const Machine& m,unsigned slot) const {
    validate_root();if(slot<1||slot>4)throw std::runtime_error("Choose state slot 1 to 4");
    return root_/L"states"/identity(m)/(L"slot-"+std::to_wstring(slot)+L".vks");
}
std::filesystem::path ProductStore::battery_path(const Machine& m) const {validate_root();return root_/L"saves"/(identity(m)+L".sav");}
void ProductStore::save_state(const Machine& m,unsigned slot) const {const auto bytes=m.save_state();atomic_product_write(state_path(m,slot),bytes);}
bool ProductStore::load_state(Machine& m,unsigned slot,std::string& error) const {
    try{return m.load_state(read_product_file(state_path(m,slot),max_state_bytes),error);}
    catch(const std::exception& e){error=e.what();return false;}
}
void ProductStore::save_battery(const Machine& m) const {if(m.has_battery()){const auto bytes=m.save_battery();atomic_product_write(battery_path(m),bytes);}}
bool ProductStore::load_battery(Machine& m,std::string& error) const {
    error.clear();if(!m.has_battery())return true;
    try{
        const auto path=battery_path(m);if(!std::filesystem::exists(path))return true;
        return m.load_battery(read_product_file(path,8192+44),error);
    }catch(const std::exception& e){error=e.what();return false;}
}
int product_probe(const std::filesystem::path& rom,const std::filesystem::path& output){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto bytes=read_product_file(rom,32u*1024u*1024u+528);
    const auto output_root=std::filesystem::absolute(output);
    std::filesystem::create_directories(output_root);
    const auto root=output_root/(L"product-probe-"+std::to_wstring(GetCurrentProcessId()));
    // Exclusive new namespace prevents even an accidentally supplied application
    // save directory from exposing or replacing any existing user save/config.
    require(std::filesystem::create_directory(root),"Probe run directory already exists");
    ProductStore store(root);
    Machine machine(bytes);std::string error;
    // Never load a pre-existing save, even inside the diagnostic directory.
    auto battery_rom=synthetic_nrom();battery_rom[6]|=2;Machine battery_model(battery_rom);
    auto& battery_machine=machine.has_battery()?machine:battery_model;
    auto ram=battery_machine.mapper->battery_ram();for(std::size_t i=0;i<ram.size();++i)ram[i]=std::uint8_t(i*37u+11u);
    const auto battery=battery_machine.save_battery();store.save_battery(battery_machine);
    std::fill(ram.begin(),ram.end(),0);require(store.load_battery(battery_machine,error),"Probe SRAM reload failed");
    require(battery_machine.save_battery()==battery,"Probe SRAM mismatch");
    machine.run_frames(2);machine.apu.pcm.clear();
    for(unsigned i=0;i<123;++i)machine.step();
    const auto state=machine.save_state();
    for(unsigned slot=1;slot<=4;++slot)store.save_state(machine,slot);
    machine.run_frames(2);
    const auto canonical=machine.serialize(),continuation=machine.save_state();
    const auto picture=frame_hash(*machine.frame()),audio=machine.audio_hash();
    for(unsigned slot=1;slot<=4;++slot){
        require(store.load_state(machine,slot,error),"Probe state reload failed");
        require(machine.save_state()==state,"Probe state round trip mismatch");
        machine.run_frames(2);
        require(machine.serialize()==canonical&&machine.save_state()==continuation,"Probe continuation mismatch");
        require(frame_hash(*machine.frame())==picture&&machine.audio_hash()==audio,"Probe frame/audio mismatch");
    }
    Config c;c.volume=25;c.mute=true;c.gamepad_enabled=false;c.state_slot=4;
    std::wstring warning;require(save_config(root/L"frontend.ini",c,warning),"Probe config write failed");
    require(load_config(root/L"frontend.ini",warning)==c,"Probe config mismatch");
    host::OutputGain gain;runtime::EndpointFormat f;
    std::array<std::uint8_t,8> sample{};
    for(unsigned level:{0u,25u,50u,100u}){
        gain.set(level,false);
        for(unsigned i=0;i<1000;++i){runtime::encode_endpoint(.5f,f,sample);gain.apply(sample,1,f);}
        float actual{};std::memcpy(&actual,sample.data(),4);
        require(gain.current()==level/100.&&actual==.5f*(level/100.f),"Probe output gain mismatch");
    }
    gain.set(100,true);
    for(unsigned i=0;i<1000;++i){runtime::encode_endpoint(.5f,f,sample);gain.apply(sample,1,f);}
    require(std::all_of(sample.begin(),sample.end(),[](auto b){return b==0;}),"Probe mute mismatch");
    PadModel pad;pad.update(true,true,0x1010,0,0);require(pad.buttons()==9,"Probe pad map mismatch");
    pad.update(true,false,0,0,0);require(pad.buttons()==0&&!pad.connected(),"Probe pad disconnect mismatch");
    pad.update(true,true,0x4020,0,0);require(pad.buttons()==6,"Probe pad reconnect mismatch");
    std::cout<<"PRODUCT_PROBE=PASS\nSTATE=V1 FOUR_SLOTS ROUNDTRIP CONTINUATION_FRAME_AUDIO_MATCH\nSRAM="
        <<(machine.has_battery()?"8192_BYTES EXACT_ATOMIC_ROUNDTRIP":"8192_BYTES FIRST_PARTY_MODEL_ROUNDTRIP (SUPPLIED_ROM_HAS_NO_BATTERY)")
        <<"\nVOLUME=0_25_50_100_PERCENT RAMPED MUTE_SILENT CONFIG_PERSISTED\nGAMEPAD_MODEL=MAPPING_DISCONNECT_RECONNECT_PASS\nUSER_SAVES=NOT_ACCESSED\nPERSISTENCE=NEW_RUN_DIRECTORY_UNDER_OUTPUT\n";
    return 0;
}
}
