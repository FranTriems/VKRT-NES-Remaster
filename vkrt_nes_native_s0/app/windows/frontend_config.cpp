#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <frontend_config.hpp>
#include "vkrt/nes_native_s0/presentation_profile.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace vkrt::nes::frontend {
std::string utf8(std::wstring_view s){
    if(s.empty())return {};
    if(s.size()>32768)throw std::invalid_argument("text too long");
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::invalid_argument("invalid Unicode text");
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring from_utf8(std::string_view s){
    if(s.empty())return {};
    if(s.size()>max_config_bytes)throw std::invalid_argument("text too long");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n)throw std::invalid_argument("invalid UTF-8 text");
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
std::wstring normalized_rom_path(std::wstring_view text){
    if(text.empty()||text.size()>max_path_units)return {};
    for(wchar_t c:text)if(c<32||c==L'"'||c==L'<'||c==L'>'||c==L'|'||c==L'?'||c==L'*')return {};
    try{
        (void)utf8(text);
        std::wstring path(text);std::replace(path.begin(),path.end(),L'/',L'\\');
        const bool drive=path.size()>3&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&path[2]==L'\\';
        const bool unc=path.starts_with(L"\\\\")&&path.size()>5&&path[2]!=L'\\'&&path.find(L'\\',2)!=std::wstring::npos;
        if(!drive&&!unc)return {};
        if(path.find(L':',drive?2:0)!=std::wstring::npos)return {};
        std::array<wchar_t,max_path_units+1> buffer{};
        auto size=GetFullPathNameW(path.c_str(),DWORD(buffer.size()),buffer.data(),nullptr);
        if(!size||size>=buffer.size())return {};
        std::wstring result(buffer.data(),size);
        if(result.size()<5||CompareStringOrdinal(result.data()+result.size()-4,4,L".nes",4,TRUE)!=CSTR_EQUAL)return {};
        return result;
    }catch(...){return {};}
}
void remember(Config& c,std::wstring_view text){
    auto path=normalized_rom_path(text);if(path.empty())return;
    std::erase_if(c.recents,[&](const auto& p){
        auto q=normalized_rom_path(p);
        return q.empty()||CompareStringOrdinal(q.c_str(),int(q.size()),path.c_str(),int(path.size()),TRUE)==CSTR_EQUAL;
    });
    c.recents.insert(c.recents.begin(),std::move(path));
    if(c.recents.size()>max_recents)c.recents.resize(max_recents);
}
namespace {
bool number(std::string_view s,unsigned& n){
    auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    return ec==std::errc{}&&end==s.data()+s.size();
}
std::string hex(std::string_view in){
    constexpr char digits[]="0123456789abcdef";std::string s;s.reserve(in.size()*2);
    for(unsigned char c:in){s+=digits[c>>4];s+=digits[c&15];}return s;
}
std::wstring unhex(std::string_view s){
    if(s.size()%2||s.size()>max_path_units*6)return {};
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    std::string out;out.reserve(s.size()/2);
    for(std::size_t i=0;i<s.size();i+=2){int a=digit(s[i]),b=digit(s[i+1]);if(a<0||b<0)return {};out+=char(a*16+b);}
    try{return normalized_rom_path(from_utf8(out));}catch(...){return {};}
}
}
Config parse_config(std::string_view input){
    Config c;
    if(input.size()>max_config_bytes||!input.starts_with("version=1\n")||!input.ends_with("end=1\n"))return c;
    Bindings keys=default_bindings;PadBindings pad=default_pad_bindings;std::vector<std::wstring> paths;
    std::size_t cursor=10;
    while(cursor<input.size()){
        auto end=input.find('\n',cursor);if(end==std::string_view::npos)break;
        auto line=input.substr(cursor,end-cursor);cursor=end+1;
        auto eq=line.find('=');if(eq==std::string_view::npos)continue;
        auto key=line.substr(0,eq),value=line.substr(eq+1);unsigned n=0;bool numeric=number(value,n);
        if(key=="mode")c.visual_mode=numeric?profile_mode(PresentationProfile::standard,n):3;
        else if(key=="integer")c.integer_scaling=numeric&&n<2?bool(n):true;
        else if(key=="fullscreen")c.fullscreen=numeric&&n==1;
        else if(key=="stats")c.stats=numeric&&n==1;
        else if(key=="focus_pause")c.pause_on_focus_loss=numeric&&n<2?bool(n):true;
        else if(key=="volume")c.volume=numeric&&n<=100?n:100;
        else if(key=="mute")c.mute=numeric&&n==1;
        else if(key=="gamepad")c.gamepad_enabled=numeric&&n<2?bool(n):true;
        else if(key=="state_slot")c.state_slot=numeric&&n>=1&&n<=4?n:1;
        else if(key.size()==4&&key.starts_with("key")&&key[3]>='0'&&key[3]<='7')keys[key[3]-'0']=numeric?n:0;
        else if(key.size()==4&&key.starts_with("pad")&&key[3]>='0'&&key[3]<='3')pad[key[3]-'0']=numeric?n:0;
        else if(key=="recent"&&paths.size()<max_recents){auto p=unhex(value);if(!p.empty())paths.push_back(std::move(p));}
    }
    c.keys=valid_bindings(keys)?keys:default_bindings;
    c.pad=valid_pad_bindings(pad)?pad:default_pad_bindings;
    for(auto it=paths.rbegin();it!=paths.rend();++it)remember(c,*it);
    return c;
}
std::string serialize_config(const Config& c){
    std::string out="version=1\nmode="+std::to_string(profile_mode(PresentationProfile::standard,c.visual_mode))+"\n";
    auto flag=[&](const char* key,bool b){out+=key;out+=b?"=1\n":"=0\n";};
    flag("integer",c.integer_scaling);flag("fullscreen",c.fullscreen);flag("stats",c.stats);flag("focus_pause",c.pause_on_focus_loss);
    out+="volume="+std::to_string(std::min(c.volume,100u))+"\nstate_slot="+std::to_string(std::clamp(c.state_slot,1u,4u))+"\n";
    flag("mute",c.mute);flag("gamepad",c.gamepad_enabled);
    const auto& keys=valid_bindings(c.keys)?c.keys:default_bindings;
    for(unsigned i=0;i<keys.size();++i)out+="key"+std::to_string(i)+"="+std::to_string(keys[i])+"\n";
    const auto& pad=valid_pad_bindings(c.pad)?c.pad:default_pad_bindings;
    for(unsigned i=0;i<pad.size();++i)out+="pad"+std::to_string(i)+"="+std::to_string(pad[i])+"\n";
    Config clean;
    const auto count=std::min(c.recents.size(),max_recents);
    for(std::size_t i=count;i>0;--i)remember(clean,c.recents[i-1]);
    for(const auto& path:clean.recents)out+="recent="+hex(utf8(path))+"\n";
    out+="end=1\n";
    if(out.size()>max_config_bytes)throw std::length_error("config bound");
    return out;
}
std::filesystem::path config_path(){
    PWSTR directory=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&directory)))return {};
    std::filesystem::path result=std::filesystem::path(directory)/L"VKRT NES"/L"frontend.ini";
    CoTaskMemFree(directory);return result;
}
Config load_config(const std::filesystem::path& p,std::wstring& warning){
    warning.clear();if(p.empty()){warning=L"Local settings storage is unavailable.";return {};}
    std::error_code ec;if(!std::filesystem::exists(p,ec)&&!ec)return {};
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if(!f){warning=L"Settings could not be read. Defaults are active.";return {};}
    auto size=f.tellg();
    if(size<0||size>std::streamoff(max_config_bytes)){warning=L"Settings file is too large. Defaults are active.";return {};}
    std::string text(std::size_t(size),'\0');f.seekg(0);f.read(text.data(),size);
    if(!f||!text.starts_with("version=1\n")||!text.ends_with("end=1\n")){
        warning=L"Settings were incomplete or unsupported. Defaults are active.";return {};
    }
    return parse_config(text);
}
bool save_config(const std::filesystem::path& p,const Config& c,std::wstring& error){
    error.clear();std::filesystem::path temp;
    try{
        if(p.empty())throw std::runtime_error("settings path unavailable");
        auto bytes=serialize_config(c);
        std::filesystem::create_directories(p.parent_path());
        temp=p;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("temporary settings file unavailable");
        DWORD written=0;bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size();
        if(ok)ok=FlushFileBuffers(file)!=0;
        CloseHandle(file);
        if(ok){
            if(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES)ok=ReplaceFileW(p.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr)!=0;
            else ok=MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
        }
        if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("atomic settings replacement failed");}
        return true;
    }catch(...){error=L"Settings could not be saved. Your choices still apply for this session.";return false;}
}
}
