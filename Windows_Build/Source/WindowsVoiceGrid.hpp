#pragma once
// Windows-only grid and browser colours: current state is persisted in
// theme_current.json; named themes live only in Themes/*.json files.
// There is no fallback to legacy themes.json or voice_grid_themes.json.
// The shared 13-colour Palette and Mac/iPad theme contract are unchanged.
#include "EditorCore/Themes.hpp"
#include "EditorCore/Json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace oxo::winvoice {
namespace fs = std::filesystem;
using Palette = std::array<uint32_t,6>;
inline constexpr std::array<const char*,6> Keys={
    "voiceGridBorder","voiceGridEmpty","voiceGridPlaying","voiceGridReleased",
    "browserSelection","browserBackground"};
inline constexpr std::array<const wchar_t*,6> Labels={
    L"Voice Grid Border",L"Voice Grid Empty",L"Voice Grid Playing",L"Voice Grid Released",
    L"Browser Selection",L"Browser Background"};
inline Palette Default(const themes::Palette& theme) {
    const uint32_t label=theme[themes::kLabel];
    const uint32_t border=(((label>>16)&255u)*17u/100u<<16)|
                          (((label>>8)&255u)*17u/100u<<8)|((label&255u)*17u/100u);
    return {border,0x000000,0xFFFFFF,0x999999,theme[themes::kDisplayText],
            theme[themes::kDisplayBottom]};
}
inline bool ReadJson(const fs::path& path,json::Value& out) {
    std::error_code ec;
    if(!fs::is_regular_file(path,ec) || ec || fs::file_size(path,ec)>1024*1024 || ec)return false;
    std::ifstream f(path,std::ios::binary);
    if(!f)return false;
    std::string content(std::istreambuf_iterator<char>{f},{});
    return !f.bad() && json::Parse(content,out) && out.IsObject();
}
inline bool ReadExtras(const json::Value& obj,Palette& out) {
    if(!obj.IsObject())return false;
    Palette p=out;
    bool any=false;
    for(size_t i=0;i<Keys.size();i++){
        const auto* v=obj.Find(Keys[i]);uint32_t c=0;
        if(v && v->IsString() && themes::ParseHex(v->string,c)) {p[i]=c;any=true;}
    }
    if(any)out=p;
    return any;
}
inline bool FromCurrentState(const fs::path& file,const themes::Palette& theme,Palette& result) {
    json::Value data;
    if(!ReadJson(file,data))return false;
    const auto* current=data.Find("current");
    return current && current->IsObject() && themes::FromHexMap(*current)==theme &&
           ReadExtras(*current,result);
}
inline bool FromFiles(const fs::path& folder,const themes::Palette& theme,Palette& result) {
    std::error_code ec;
    for(fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;
        !ec && it!=end;it.increment(ec)) {
        std::string ext=it->path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(ext!=".json")continue;
        json::Value custom;
        if(ReadJson(it->path(),custom) && themes::FromHexMap(custom)==theme && ReadExtras(custom,result))
            return true;
    }
    return false;
}
// A freshly selected factory theme uses its remembered live extras (if any)
// or its defaults. It does not borrow extras from similarly coloured files.
inline Palette LoadCurrent(const fs::path& root,const themes::Palette& theme) {
    Palette palette=Default(theme);
    FromCurrentState(root/"theme_current.json",theme,palette);
    return palette;
}
// Session restoration: prefer matching current unsaved state, then look for
// extras on a named theme file with the same base palette.
inline Palette Load(const fs::path& root,const themes::Palette& theme) {
    Palette palette=Default(theme);
    if(FromCurrentState(root/"theme_current.json",theme,palette))return palette;
    FromFiles(root/"Themes",theme,palette);
    return palette;
}
// Persist the live 19-colour selection in theme_current.json, separately
// from the standalone named theme files.
inline bool Remember(themes::Store& store,const Palette& palette) {
    themes::ExtraColors extras=store.CurrentExtras();
    for(size_t i=0;i<Keys.size();++i)extras[Keys[i]]=themes::Hex(palette[i]);
    return store.SetCurrentExtras(std::move(extras));
}
inline bool Save(themes::Store& store,const Palette& palette) {
    const themes::ExtraColors previous=store.CurrentExtras();
    Remember(store,palette);
    if(store.Write())return true;
    store.SetCurrentExtras(previous);
    return false;
}
} // namespace oxo::winvoice
