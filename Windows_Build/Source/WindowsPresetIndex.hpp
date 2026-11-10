#pragma once

// Windows-only *index* of preset bank names, slots, and filenames.
// The index contains no .sy1 content and never moves or changes presets.
// Paths for the preset library are stored relative to that library root so
// a portable Synth2_Data directory may be moved without invalidating it.
// An explicit Refresh in the browser rebuilds this index from the filesystem.

#include "EngineCore/PatchLibrary.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace oxo::winindex {
namespace fs = std::filesystem;

inline constexpr char kMagic[] = "OxoSynth2PresetIndex01";
inline constexpr unsigned kMaxBanks = 50000;
inline constexpr unsigned kMaxName = 65536;
inline constexpr std::uintmax_t kMaxIndexSize = 128 * 1024 * 1024;
inline fs::path Path(const fs::path& dataRoot) { return dataRoot / "preset_index.bin"; }

inline void Put32(std::ostream& output, uint32_t value) {
    for (unsigned i=0;i<4;++i) output.put(char((value >> (i*8)) & 255));
}
inline bool Get32(std::istream& input, uint32_t& result) {
    result=0;
    for (unsigned i=0;i<4;++i) {
        const int ch=input.get();
        if (ch==std::char_traits<char>::eof()) return false;
        result |= uint32_t(uint8_t(ch)) << (i*8);
    }
    return true;
}
inline bool PutString(std::ostream& output, const std::string& value) {
    if (value.size()>kMaxName) return false;
    Put32(output,uint32_t(value.size()));
    output.write(value.data(),std::streamsize(value.size()));
    return bool(output);
}
inline bool GetString(std::istream& input, std::string& result) {
    uint32_t length=0;
    if (!Get32(input,length) || length>kMaxName) return false;
    result.resize(length);
    input.read(result.data(),std::streamsize(length));
    return bool(input) && result.find('\0')==std::string::npos;
}
inline bool SafeRelative(const fs::path& rel) {
    if (rel.empty() || rel.is_absolute() || rel.has_root_path()) return false;
    for (const auto& part:rel)
        if (part==".." || part.empty()) return false;
    return true;
}
inline bool SafeFileName(const std::string& name) {
    if (name.empty() || name=="." || name=="..") return false;
    const fs::path p(name);
    return p.filename()==p && !p.has_parent_path() && !p.has_root_path() && s2::IsSy1(name);
}

inline bool Save(const fs::path& dataRoot, const fs::path& libraryRoot,
                 const fs::path& factoryRoot, const std::vector<s2::PatchBank>& banks) {
    if (dataRoot.empty() || libraryRoot.empty() || banks.size()>kMaxBanks) return false;
    const fs::path path=Path(dataRoot), temp=path.string()+".tmp";
    std::error_code ec;
    fs::create_directories(dataRoot,ec);
    if (ec) return false;
    std::ofstream out(temp,std::ios::binary|std::ios::trunc);
    if (!out) return false;
    out.write(kMagic,sizeof(kMagic));
    Put32(out,uint32_t(banks.size()));
    bool good=bool(out);
    for (const auto& b:banks) {
        const bool factory=b.readOnly;
        const fs::path source=fs::path(b.source).lexically_normal();
        const fs::path rel=source.lexically_relative(libraryRoot.lexically_normal());
        if (factory) good = good && !factoryRoot.empty() && source==factoryRoot.lexically_normal();
        else good = good && (rel=="." || SafeRelative(rel));
        if (!good || !PutString(out,b.name) || !PutString(out,factory ? ":factory" : rel.generic_string())) {
            good=false;break;
        }
        unsigned count=0;
        for (const auto& slot:b.slots) if (!slot.Empty()) ++count;
        Put32(out,count);
        for (unsigned i=0;good && i<b.slots.size();++i) {
            const auto& slot=b.slots[i];
            if (slot.Empty()) continue;
            const fs::path file(slot.file);
            const std::string filename=file.filename().string();
            if (file.parent_path().lexically_normal()!=source || !SafeFileName(filename)) {
                good=false;break;
            }
            Put32(out,i);
            good=PutString(out,slot.name) && PutString(out,filename);
        }
    }
    out.flush();good=good && bool(out);
    out.close();
    if (!good) {fs::remove(temp,ec);return false;}
#if defined(_WIN32)
    // Atomic replacement also works when the previous index already exists.
    const bool replaced=MoveFileExW(temp.c_str(),path.c_str(),
                                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if (!replaced) {fs::remove(temp,ec);return false;}
#else
    fs::rename(temp,path,ec);
    if (ec) {fs::remove(temp,ec);return false;}
#endif
    return true;
}

inline bool Load(const fs::path& dataRoot, const fs::path& libraryRoot,
                 const fs::path& factoryRoot, std::vector<s2::PatchBank>& banks) {
    if (dataRoot.empty() || libraryRoot.empty()) return false;
    const fs::path path=Path(dataRoot);
    std::error_code ec;
    const auto bytes=fs::file_size(path,ec);
    if (ec || bytes>kMaxIndexSize) return false;
    std::ifstream in(path,std::ios::binary);
    if (!in) return false;
    char header[sizeof(kMagic)]{};
    in.read(header,sizeof(header));
    if (!in || std::string(header,sizeof(header)) != std::string(kMagic,sizeof(kMagic))) return false;
    uint32_t count=0;
    if (!Get32(in,count) || count>kMaxBanks) return false;
    std::vector<s2::PatchBank> loaded;
    loaded.reserve(count);
    for (uint32_t b=0;b<count;++b) {
        s2::PatchBank bank;
        std::string location;
        if (!GetString(in,bank.name) || !GetString(in,location)) return false;
        const bool factory=location==":factory";
        if (factory) {
            if (b!=0 || factoryRoot.empty() || !fs::is_directory(factoryRoot,ec)) return false;
            bank.readOnly=true;
            bank.source=factoryRoot.string();
        } else {
            const fs::path rel(location);
            if (location!="." && !SafeRelative(rel)) return false;
            const fs::path dir=location=="." ? libraryRoot : libraryRoot/rel;
            if (!fs::is_directory(dir,ec)) return false;
            bank.source=dir.lexically_normal().string();
        }
        uint32_t slots=0;
        if (!Get32(in,slots) || slots>s2::kBankSlots) return false;
        for (uint32_t j=0;j<slots;++j) {
            uint32_t id=0;
            std::string label,file;
            if (!Get32(in,id) || id>=s2::kBankSlots || !bank.slots[id].Empty() ||
                !GetString(in,label) || !GetString(in,file) || !SafeFileName(file)) return false;
            bank.slots[id]={std::move(label),(fs::path(bank.source)/file).string()};
        }
        loaded.push_back(std::move(bank));
    }
    // Reject trailing bytes from a corrupt or incompatible cache.
    if (in.peek()!=std::char_traits<char>::eof()) return false;
    banks=std::move(loaded);
    return true;
}
} // namespace oxo::winindex
