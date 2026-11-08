#pragma once
// Windows-only file-backed factory themes. The original 13 colour keys and
// seven palettes are copied from Shared/SynthTheme.swift without changes;
// four Windows voice-grid colour keys are appended.
// Create <active settings root>/Themes/*.json once; do not overwrite user edits.
// These files are NOT preset files and never alter the synthesis engine.
#include <array>
#include <algorithm>
#include <cwctype>
#include <cstddef>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace oxo::wintheme {
namespace fs = std::filesystem;
inline constexpr size_t kCoreColours=13;
inline constexpr size_t kColourCount=17;
using Palette = std::array<std::uint32_t, kColourCount>;

inline constexpr std::array<const char*, kColourCount> Keys = {
    "panel", "header", "headerText", "label", "knobBody", "knobGradient", "knobDial",
    "button", "led", "radio", "displayText", "displayTop", "displayBottom",
    "voiceGridBorder", "voiceGridEmpty", "voiceGridPlaying", "voiceGridReleased"
};
// Names shown in the editor only; JSON key names stay identical.
inline constexpr std::array<const wchar_t*, kColourCount> Labels = {
    L"Main Panel", L"Section Headers", L"Section Header Text", L"Parameter Labels",
    L"Knob Body", L"Knob Gradient", L"Knob Dial", L"Buttons", L"Button LED's",
    L"Radio LED's", L"Display Text", L"Display Gradient Top", L"Display Gradient Bottom",
    L"Voice Grid Border", L"Voice Grid Empty", L"Voice Grid Playing", L"Voice Grid Released"
};
// Preserve the original Windows voice grid's per-theme label-derived border
// and white/grey/black states when upgrading the existing 13-key JSON themes.
constexpr std::uint32_t VoiceBorder(std::uint32_t label) {
    return ((((label >> 16) & 255u) * 17u / 100u) << 16) |
           ((((label >> 8) & 255u) * 17u / 100u) << 8) |
           ((label & 255u) * 17u / 100u);
}
constexpr Palette WithVoiceColours(std::array<std::uint32_t,kCoreColours> base) {
    Palette out{};
    for(size_t i=0;i<kCoreColours;i++)out[i]=base[i];
    out[13]=VoiceBorder(base[3]);
    out[14]=0x000000;
    out[15]=0xFFFFFF;
    out[16]=0x999999;
    return out;
}
inline constexpr std::array<const char*, 7> Names = {
    "Synth2", "Blue", "Gray Dark", "Gray Light", "Red", "Warm", "Synth1"
};
inline constexpr std::array<Palette, 7> Originals = {{
    WithVoiceColours({0xB7B79B, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A}),
    WithVoiceColours({0x8AA7B8, 0x1A1D26, 0x9DA198, 0x181A19, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xA4DE87, 0x73F2FF, 0x24272F, 0x1C1F26}),
    WithVoiceColours({0x3F434D, 0x5F5F60, 0xD1D5D7, 0xE4E7EC, 0x100F0F, 0x151516, 0xB5B6B9, 0x363630, 0x8ED9B5, 0xF3F9F1, 0xC2D3E1, 0x161A1A, 0x0D100C}),
    WithVoiceColours({0xCECEC0, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A}),
    WithVoiceColours({0xA71818, 0x474747, 0xFFFFFF, 0xFFFFFF, 0x171717, 0x212121, 0xFFFFFF, 0x363630, 0x5CF266, 0xA4DE87, 0x73F2FF, 0x24272F, 0x1C1F26}),
    WithVoiceColours({0x313336, 0xB78F0F, 0x000000, 0xE4E7EC, 0x1B1B1B, 0x181818, 0xB5B6B9, 0x363630, 0x8ED9B5, 0xF3F9F1, 0xC2D3E1, 0x161A1A, 0x0D100C}),
    WithVoiceColours({0xC7C786, 0x3B3B38, 0xE6E0B8, 0x1A1A0F, 0x171717, 0x212121, 0xFF2D2D, 0x363630, 0x5CF266, 0xFF331A, 0x73F2FF, 0x123D40, 0x05171A})
}};

inline fs::path Folder(const fs::path& dataRoot) { return dataRoot / "Themes"; }
inline fs::path File(const fs::path& dataRoot, int index) {
    if(dataRoot.empty() || index<0 || index>=static_cast<int>(Names.size())) return {};
    return Folder(dataRoot) / (std::string(Names[static_cast<size_t>(index)]) + ".json");
}
// Built-in themes come first; custom JSON files are discovered on demand.
// Files retain their own names, without adding a second theme format.
struct Entry {
    std::wstring name;
    fs::path file;
};
inline std::wstring FoldCase(std::wstring name) {
    std::transform(name.begin(),name.end(),name.begin(),
        [](wchar_t ch) {return static_cast<wchar_t>(std::towlower(ch));});
    return name;
}
inline std::vector<Entry> List(const fs::path& root) {
    std::vector<Entry> out;
    if(root.empty())return out;
    for(size_t i=0;i<Names.size();++i) {
        out.push_back({fs::path(Names[i]).wstring(),File(root,static_cast<int>(i))});
    }
    std::error_code ec;
    fs::directory_iterator it(Folder(root),fs::directory_options::skip_permission_denied,ec),end;
    while(!ec && it!=end) {
        const auto path=it->path();
        std::error_code itemError;
        if(it->is_regular_file(itemError) &&
           FoldCase(path.extension().wstring())==L".json") {
            const auto name=path.stem().wstring();
            bool builtin=false;
            for(const char* original:Names) {
                if(FoldCase(name)==FoldCase(fs::path(original).wstring())) {
                    builtin=true;break;
                }
            }
            if(!builtin && !name.empty())out.push_back({name,path});
        }
        it.increment(ec);
    }
    std::sort(out.begin()+static_cast<std::ptrdiff_t>(Names.size()),out.end(),
        [](const Entry& a,const Entry& b) {
            const auto left=FoldCase(a.name),right=FoldCase(b.name);
            return left==right?a.name<b.name:left<right;
        });
    return out;
}
inline std::string ToJSON(const Palette& colors) {
    constexpr char digits[]="0123456789ABCDEF";
    std::string json="{\n";
    for(size_t i=0;i<Keys.size();++i) {
        json += "  \"";json+=Keys[i];json+="\": \"#";
        for(int shift=20; shift>=0; shift-=4) json += digits[(colors[i]>>shift)&15];
        json += (i+1==Keys.size()?"\"\n":"\",\n");
    }
    return json+"}\n";
}
// Strict parser: 13 original colours required, 4 new grid colours optional.
// Existing v17 JSON files retain all edits and receive default grid colours
// in memory until their next explicit Save.
// Reject invalid/incomplete files rather than silently applying a partial theme.
struct Parser {
    const std::string& json;
    size_t at=0;
    void space() { while(at<json.size() && (json[at]==' ' || json[at]=='\r' || json[at]=='\t' || json[at]=='\n')) ++at; }
    bool eat(char c) { space(); if(at<json.size() && json[at]==c) {++at;return true;}return false; }
    bool quoted(std::string& s) {
        s.clear();if(!eat('"'))return false;
        while(at<json.size()) {
            const char c=json[at++];
            if(c=='"')return true;
            // Generated factory files need no escaping. Explicitly reject
            // escapes to avoid silently misreading a name or hex colour.
            if(c=='\\' || static_cast<unsigned char>(c)<32)return false;
            s+=c;
        }
        return false;
    }
};
inline bool FromJSON(const std::string& input, Palette& colors) {
    std::string json=input;
    if(json.size()>=3 && json.compare(0,3,"\xEF\xBB\xBF")==0) json.erase(0,3); // UTF-8 BOM
    Parser p{json};
    if(!p.eat('{'))return false;
    Palette result{};
    std::array<bool,kColourCount> found{};
    int count=0;
    while(true) {
        if(p.eat('}'))break;
        if(count!=0 && !p.eat(','))return false;
        std::string key,value;
        if(!p.quoted(key) || !p.eat(':') || !p.quoted(value))return false;
        size_t field=0;
        while(field<Keys.size() && key!=Keys[field])++field;
        if(field==Keys.size() || found[field] || value.size()!=7 || value.front()!='#')return false;
        unsigned colour=0;
        for(size_t j=1;j<7;j++) {
            const char ch=value[j];unsigned v;
            if(ch>='0'&&ch<='9')v=static_cast<unsigned>(ch-'0');
            else if(ch>='A'&&ch<='F')v=static_cast<unsigned>(ch-'A'+10);
            else if(ch>='a'&&ch<='f')v=static_cast<unsigned>(ch-'a'+10);
            else return false;
            colour=colour*16+v;
        }
        result[field]=colour;
        found[field]=true;
        ++count;
    }
    p.space();
    if(p.at!=json.size())return false;
    for(size_t i=0;i<kCoreColours;i++) if(!found[i])return false;
    if(!found[13])result[13]=VoiceBorder(result[3]);
    if(!found[14])result[14]=0x000000;
    if(!found[15])result[15]=0xFFFFFF;
    if(!found[16])result[16]=0x999999;
    colors=result;
    return true;
}
inline bool Read(const fs::path& file, Palette& result) {
    if(file.empty())return false;
    std::error_code ec;
    if(!fs::is_regular_file(file,ec) || ec)return false;
    const auto size=fs::file_size(file,ec);
    if(ec || size>8192)return false;
    std::ifstream in(file,std::ios::binary);
    if(!in)return false;
    std::string json(std::istreambuf_iterator<char>{in},{});
    if(in.bad())return false;
    return FromJSON(json,result);
}
// Each file is only created if missing. CREATE_NEW avoids clobbering existing
// files if two plugin instances are opened at the same time on Windows.
inline bool CreateMissing(const fs::path& root) {
    if(root.empty())return false;
    std::error_code ec;
    fs::create_directories(Folder(root),ec);
    if(ec)return false;
    bool success=true;
    for(size_t i=0;i<Names.size();i++) {
        const fs::path path=File(root,static_cast<int>(i));
        if(fs::exists(path,ec) && !ec)continue;
        if(ec){success=false;ec.clear();continue;}
        const std::string contents=ToJSON(Originals[i]);
#ifdef _WIN32
        HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE) {
            if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)success=false;
            continue;
        }
        DWORD written=0;
        const bool ok=contents.size()<=MAXDWORD &&
            WriteFile(h,contents.data(),static_cast<DWORD>(contents.size()),&written,nullptr)!=0 &&
            written==contents.size() && FlushFileBuffers(h)!=0;
        CloseHandle(h);
        if(!ok){DeleteFileW(path.c_str());success=false;}
#else
        std::ofstream out(path,std::ios::binary | std::ios::trunc);
        if(!out){success=false;continue;}
        out.write(contents.data(),static_cast<std::streamsize>(contents.size()));
        out.flush();
        if(!out)success=false;
#endif
    }
    return success;
}
} // namespace oxo::wintheme
