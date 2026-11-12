#pragma once

// Synth1-style patch banks, portable C++17 + zlib (mac VST3 and iPad AUv3).
//
// The library is ONE folder we own (no references to outside folders). A
// bank = 128 program SLOTS. Slot n is the file "NNN.sy1" (001..128, Synth1's
// own naming); other .sy1 names fill the free slots in name order. Kinds:
//   folder bank  a folder of .sy1 files
//   container    a folder holding only sub-folders (no bank of its own)
// Sub-folders are banks named "parent / sub".
// Loose .sy1 files directly in the root form a bank named after the root.
// Zips are never read in place: "Import" copies folders and EXTRACTS zips
// (Synth1 zip banks, `zipbank/` folders) into the library (ImportItems); a
// zip found in the library on a rescan is extracted next to itself, then
// moved to the Trash. Bundled starter banks are copied in once
// (SeedStarterBanks).

#include "PatchCategories.hpp"
#include "Sy1Codec.hpp"
#include "TextEncoding.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace oxo::s2 {

inline constexpr int kBankSlots = 128;

// ---- minimal zip reader (stored + deflate) ---------------------------------

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t compressedSize = 0, size = 0, localOffset = 0;
};

class ZipArchive {
public:
    bool Open(const std::filesystem::path &path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        return OpenData(std::string(std::istreambuf_iterator<char>(f), {}));
    }
    bool OpenData(std::string data) {
        data_ = std::move(data);
        entries_.clear();
        return ParseDirectory();
    }
    const std::vector<ZipEntry> &Entries() const { return entries_; }

    bool Read(const ZipEntry &e, std::string &out) const {
        if (size_t(e.localOffset) + 30 > data_.size() || U32(e.localOffset) != 0x04034b50) return false;
        const size_t start = size_t(e.localOffset) + 30 + U16(e.localOffset + 26) + U16(e.localOffset + 28);
        if (start + e.compressedSize > data_.size()) return false;
        const char *src = data_.data() + start;
        if (e.method == 0) {
            out.assign(src, e.compressedSize);
            return true;
        }
        if (e.method != 8) return false;
        out.resize(e.size);
        z_stream zs{};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
        zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src));
        zs.avail_in = e.compressedSize;
        zs.next_out = reinterpret_cast<Bytef *>(out.data());
        zs.avail_out = e.size;
        const int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        return rc == Z_STREAM_END;
    }

    bool Read(const std::string &name, std::string &out) const {
        for (const ZipEntry &e : entries_)
            if (e.name == name) return Read(e, out);
        return false;
    }

private:
    uint16_t U16(size_t at) const { return uint16_t(uint8_t(data_[at]) | uint8_t(data_[at + 1]) << 8); }
    uint32_t U32(size_t at) const { return uint32_t(U16(at)) | uint32_t(U16(at + 2)) << 16; }

    bool ParseDirectory() {
        if (data_.size() < 22) return false;
        size_t eocd = std::string::npos;
        for (size_t i = data_.size() - 22, stop = data_.size() > 65557 ? data_.size() - 65557 : 0;; --i) {
            if (U32(i) == 0x06054b50) {
                eocd = i;
                break;
            }
            if (i == stop) break;
        }
        if (eocd == std::string::npos) return false;
        size_t at = U32(eocd + 16);
        const size_t count = U16(eocd + 10);
        for (size_t n = 0; n < count && at + 46 <= data_.size() && U32(at) == 0x02014b50; ++n) {
            ZipEntry e;
            e.method = U16(at + 10);
            e.compressedSize = U32(at + 20);
            e.size = U32(at + 24);
            const size_t nameLen = U16(at + 28), extra = U16(at + 30), comment = U16(at + 32);
            e.localOffset = U32(at + 42);
            if (at + 46 + nameLen > data_.size()) break;
            e.name.assign(data_.data() + at + 46, nameLen);
            entries_.push_back(std::move(e));
            at += 46 + nameLen + extra + comment;
        }
        return true;
    }

    std::string data_;
    std::vector<ZipEntry> entries_;
};

// ---- names ------------------------------------------------------------------

inline std::string Lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Valid UTF-8 passes through, invalid bytes become `repl` (decode legacy
// names first: LegacyToUtf8).
inline std::string Utf8Clean(const std::string &s, char repl) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        bool ok = len > 0 && i + size_t(len) <= s.size();
        for (int k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(s[i + size_t(k)]) & 0xC0) == 0x80;
        if (ok) o.append(s, i, size_t(len)), i += size_t(len);
        else o += repl, ++i;
    }
    return o;
}

inline std::string JsonString(const std::string &s) {
    std::string o = "\"";
    for (const char c : Utf8Clean(s, '?')) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}

// A file / folder name we create: one path component, valid UTF-8, no
// leading dot.
inline std::string SafeName(std::string name) {
    name = Utf8Clean(LegacyToUtf8(name), '_');
    for (char &c : name)
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) c = '-';
    while (!name.empty() && (name.front() == '.' || name.front() == ' ')) name.erase(name.begin());
    return name.empty() ? std::string("Bank") : name;
}

inline bool IsSy1(const std::string &name) { return name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".sy1"; }
inline bool IsHiddenName(const std::string &fn) { return fn.empty() || fn[0] == '.' || fn == "__MACOSX"; }

// "Bank", then "Bank 2", "Bank 3"… ("Name 2.sy1" for patch files).
inline std::filesystem::path UniquePath(const std::filesystem::path &p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return p;
    const bool file = IsSy1(p.filename().string());
    const std::string stem = file ? (p.parent_path() / p.stem()).string() : p.string();
    const std::string ext = file ? p.extension().string() : "";
    for (int i = 2;; ++i) {
        std::filesystem::path q = stem + " " + std::to_string(i) + ext;
        if (!std::filesystem::exists(q, ec)) return q;
    }
}

// Visible entries of a folder, in name order. Entries keep the listing's
// file type, so is_directory() costs no stat (big libraries).
inline std::vector<std::filesystem::directory_entry> FolderEntries(const std::filesystem::path &dir) {
    std::vector<std::pair<std::string, std::filesystem::directory_entry>> keyed;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string fn = e.path().filename().string();
        if (!IsHiddenName(fn)) keyed.push_back({Lower(fn), e});
    }
    std::sort(keyed.begin(), keyed.end(), [](auto &a, auto &b) { return a.first < b.first; });
    std::vector<std::filesystem::directory_entry> items;
    items.reserve(keyed.size());
    for (auto &k : keyed) items.push_back(std::move(k.second));
    return items;
}
inline std::vector<std::filesystem::path> FolderChildren(const std::filesystem::path &dir) {
    std::vector<std::filesystem::path> items;
    for (const auto &e : FolderEntries(dir)) items.push_back(e.path());
    return items;
}

// ---- banks ------------------------------------------------------------------

struct PatchRef {
    std::string name; // first line of the .sy1 ("" = empty slot)
    std::string file; // .sy1 path ("" = empty)
    // The `.sy1`'s own `color=` line (Synth1 tags patches this way), so a
    // colour travels with the file through copy / export / import and needs
    // no sidecar. "default" = untinted.
    std::string color = "default";
    // Categories / favourites / tags (PatchCategories.hpp), resolved during
    // the scan while the library has a TagStore. Kept off the .sy1: the
    // format is third party and is never extended.
    std::string hash; // lower hex SHA-256 of the file bytes ("" = not hashed)
    int category = kCatUnclassified; // stored override, else derived from the name
    bool favourite = false;
    std::vector<std::string> tags;
    bool Empty() const { return file.empty(); }
};

// The bank shipped inside the plugin (read-only, first in the list, updated
// with the plugin); also its folder name in the bundle resources.
inline constexpr const char *kFactoryBankName = "Synth2 Factory";
// The empty bank a new library starts with; also where the write dialog
// proposes to save a patch out of a read-only bank (created by that write
// when the user deleted it). Swift mirror: SynthModel.userBankName.
inline constexpr const char *kUserBankName = "User";
// Its LibraryProgram::bank (sessions): not a path, so it survives the
// bundle moving. ':' can't come from Finder in a folder name.
inline constexpr const char *kFactoryProgramBank = ":factory";

struct PatchBank {
    std::string name;   // display name
    std::string source; // folder path
    std::array<PatchRef, kBankSlots> slots;
    bool readOnly = false; // the factory bank: no write / move / delete

    int Count() const {
        int n = 0;
        for (const PatchRef &p : slots) n += !p.Empty();
        return n;
    }
};

inline bool ReadTextFile(const std::filesystem::path &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// Patch names (a .sy1's first line) and colours (its second, `color=blue`)
// by file: a rescan only opens new or changed files (path + size + mtime),
// the rest comes from here. Thousands of .sy1 = seconds of opens otherwise.
// Process-wide: every instance and the background scans share it.
class PatchNameCache {
public:
    static PatchNameCache &Shared() {
        static PatchNameCache cache;
        return cache;
    }
    bool Name(std::filesystem::directory_entry e, std::string &name, std::string &color) {
        std::error_code ec;
        e.refresh(ec); // one stat, then size + time come from the entry
        const std::uintmax_t size = e.file_size(ec);
        if (ec) return false;
        const std::filesystem::file_time_type time = e.last_write_time(ec);
        if (ec) return false;
        const std::string key = e.path().string();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = names_.find(key);
            if (it != names_.end() && it->second.size == size && it->second.time == time) {
                name = it->second.name;
                color = it->second.color;
                return true;
            }
        }
        std::ifstream f(e.path(), std::ios::binary);
        if (!f) return false;
        std::string line;
        if (!std::getline(f, line)) return false;
        name = LegacyToUtf8(line.substr(0, line.find('\r')));
        color = "default";
        if (std::getline(f, line)) { // `color=`; patches without one stay default
            line = line.substr(0, line.find('\r'));
            if (line.rfind("color=", 0) == 0) color = line.substr(6);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        names_[key] = {size, time, name, color};
        return true;
    }
    void Forget(const std::filesystem::path &path) {
        std::lock_guard<std::mutex> lock(mutex_);
        names_.erase(path.string());
    }

    // A rewritten file must not be served from cache by a stale stat.
    void Forget(const std::string &path) {
        std::lock_guard<std::mutex> lock(mutex_);
        names_.erase(path);
    }

private:
    struct Entry {
        std::uintmax_t size;
        std::filesystem::file_time_type time;
        std::string name;
        std::string color;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> names_;
};

// SHA-256 of a patch file by file: same trick as PatchNameCache (rescan only
// hashes new or changed files). Only consulted when the library has a
// TagStore, so a library nobody tagged pays nothing.
class PatchHashCache {
public:
    static PatchHashCache &Shared() {
        static PatchHashCache cache;
        return cache;
    }
    bool Hash(std::filesystem::directory_entry e, std::string &hash) {
        std::error_code ec;
        e.refresh(ec);
        const std::uintmax_t size = e.file_size(ec);
        if (ec) return false;
        const std::filesystem::file_time_type time = e.last_write_time(ec);
        if (ec) return false;
        const std::string key = e.path().string();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = hashes_.find(key);
            if (it != hashes_.end() && it->second.size == size && it->second.time == time) {
                hash = it->second.hash;
                return true;
            }
        }
        std::string computed;
        if (!Sha256File(e.path(), computed)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        hashes_[key] = {size, time, computed};
        hash = std::move(computed);
        return true;
    }
    // A rewritten file must not be served from the cache by a stale stat.
    void Forget(const std::string &path) {
        std::lock_guard<std::mutex> lock(mutex_);
        hashes_.erase(path);
    }

private:
    struct Entry {
        std::uintmax_t size;
        std::filesystem::file_time_type time;
        std::string hash;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> hashes_;
};

// The store's record for a patch: category override, star, tags. `derived` =
// what the name rule says (also used when the patch has no record), so a
// cleared tag / star really clears.
inline void ApplyStoredTags(PatchRef &ref, const TagStore &tags, int derived) {
    ref.category = derived;
    ref.favourite = false;
    ref.tags.clear();
    const PatchTags *stored = ref.hash.empty() ? nullptr : tags.Find(ref.hash);
    if (stored == nullptr) return;
    if (stored->overridden && IsCategory(stored->category)) ref.category = stored->category;
    ref.favourite = stored->favourite;
    ref.tags = stored->tags;
}

// Hashes a patch file unless that already happened (scans skip it while the
// store is empty, so the first tag / star is what pays for a library's
// hashes).
inline bool EnsureHash(PatchRef &ref) {
    if (!ref.hash.empty()) return true;
    return !ref.file.empty() && Sha256File(ref.file, ref.hash);
}

// Synth1 slot number of a file name ("012.sy1" / "012_Lead.sy1" -> 11), -1 if none.
inline int SlotFromFileName(const std::string &fileName) {
    int v = 0, digits = 0;
    while (digits < 3 && digits < int(fileName.size()) && std::isdigit(static_cast<unsigned char>(fileName[size_t(digits)])))
        v = v * 10 + (fileName[size_t(digits++)] - '0');
    if (digits == 0 || (digits < int(fileName.size()) && std::isdigit(static_cast<unsigned char>(fileName[size_t(digits)]))))
        return -1;
    return v >= 1 && v <= kBankSlots ? v - 1 : -1;
}

inline std::string SlotFileName(int slot) {
    char b[16];
    std::snprintf(b, sizeof b, "%03d.sy1", slot + 1);
    return b;
}

class PatchLibrary {
public:
    // `factory` = the read-only factory bank folder ("" / missing = none),
    // listed first. `tags` = the sidecar (nullptr / empty = no hashing at
    // all, every slot Unclassified with no stars or tags).
    void Scan(const std::filesystem::path &root, const std::filesystem::path &factory = {},
              const TagStore *tags = nullptr) {
        banks_.clear();
        stopAtPatch_ = found_ = false;
        tags_ = tags != nullptr && !tags->Empty() ? tags : nullptr;
        AddFactory(factory);
        ScanFolder(root, "", true);
    }
    // Like Scan, but stops after the first bank holding a patch (the banks
    // before it are the same as Scan's, so its index is Scan's too).
    void ScanToFirstPatch(const std::filesystem::path &root, const std::filesystem::path &factory = {},
                          const TagStore *tags = nullptr) {
        banks_.clear();
        stopAtPatch_ = true, found_ = false;
        tags_ = tags != nullptr && !tags->Empty() ? tags : nullptr;
        AddFactory(factory);
        if (!found_) ScanFolder(root, "", true);
    }

    // One bank folder alone (its own .sy1 files, slots as Scan assigns them).
    void ScanBank(const std::filesystem::path &dir) {
        banks_.clear();
        stopAtPatch_ = found_ = false;
        std::error_code ec;
        if (!dir.empty() && std::filesystem::is_directory(dir, ec)) AddFolderBank(dir, dir.filename().string(), FolderEntries(dir));
    }

    std::vector<PatchBank> &Banks() { return banks_; }
    const std::vector<PatchBank> &Banks() const { return banks_; }

    // Streams each finished bank to a listener (the background scan's queue);
    // the synchronous Scan leaves it empty.
    void SetPublishSink(std::function<void(const PatchBank &)> sink) { publish_ = std::move(sink); }

#if defined(_WIN32)
    // Restore a previously scanned Windows-only preset index. The index
    // contains names and paths, never preset bytes or synthesis parameters.
    void AdoptIndex(std::vector<PatchBank> banks) { banks_ = std::move(banks); }
#endif

    int FindBySource(const std::string &source) const {
        for (size_t i = 0; i < banks_.size(); ++i)
            if (banks_[i].source == source) return int(i);
        return -1;
    }

    // Re-resolves one slot against the store (after a rewrite / tag edit) and
    // returns its hash.
    std::string ResolveTags(size_t bank, size_t slot, const TagStore &tags) {
        if (bank >= banks_.size() || slot >= size_t(kBankSlots)) return {};
        PatchRef &ref = banks_[bank].slots[slot];
        if (!EnsureHash(ref)) return {};
        ApplyStoredTags(ref, tags, DeriveCategory(ref.name, std::filesystem::path(ref.file).filename().string()));
        return ref.hash;
    }

    const PatchRef *RefAt(size_t bank, size_t slot) const {
        return bank < banks_.size() && slot < size_t(kBankSlots) ? &banks_[bank].slots[slot] : nullptr;
    }

    // The .sy1 file of a slot ("" = empty / out of range).
    std::string SlotFile(size_t bank, size_t slot) const {
        return bank < banks_.size() && slot < size_t(kBankSlots) ? banks_[bank].slots[slot].file : std::string();
    }

    bool Load(size_t bank, size_t slot, Sy1Patch &out) const {
        std::string text;
        const std::string file = SlotFile(bank, slot);
        if (file.empty() || !ReadTextFile(file, text)) return false;
        out = ParseSy1(text);
        out.name = LegacyToUtf8(out.name);
        return true;
    }

    // Synth1 "write": the patch becomes slot `slot` of a bank, as NNN.sy1; a
    // differently named file that held the slot is replaced.
    bool Write(size_t bank, size_t slot, const Sy1Patch &patch) {
        if (bank >= banks_.size() || slot >= size_t(kBankSlots) || banks_[bank].readOnly) return false;
        PatchBank &b = banks_[bank];
        const std::filesystem::path target = std::filesystem::path(b.source) / SlotFileName(int(slot));
        {
            std::ofstream f(target, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f << WriteSy1(patch);
            if (!f) return false;
        }
        std::error_code ec;
        if (!b.slots[slot].Empty() && std::filesystem::path(b.slots[slot].file) != target)
            std::filesystem::remove(b.slots[slot].file, ec);
        PatchHashCache::Shared().Forget(target.string()); // rewritten bytes
        PatchNameCache::Shared().Forget(target.string());
        b.slots[slot] = MakeRef(patch.name, target);
        b.slots[slot].color = patch.color;
        return true;
    }

    // Browser drag of a patch onto a bank: a copy of its file (bytes kept)
    // into the first free slot of `to` (its own bank too = a duplicate), as
    // NNN.sy1. Returns the slot, -1 = `to` is full, -2 = refused / failed.
    int Copy(size_t bank, size_t slot, size_t to) {
        if (bank >= banks_.size() || to >= banks_.size() || slot >= size_t(kBankSlots) || banks_[to].readOnly ||
            banks_[bank].slots[slot].Empty())
            return -2;
        const PatchRef src = banks_[bank].slots[slot];
        PatchBank &dst = banks_[to];
        std::error_code ec;
        for (int s = 0; s < kBankSlots; ++s) {
            const std::filesystem::path target = std::filesystem::path(dst.source) / SlotFileName(s);
            if (!dst.slots[size_t(s)].Empty() || std::filesystem::exists(target, ec)) continue;
            if (!std::filesystem::copy_file(src.file, target, ec)) return -2;
            PatchRef ref = src; // identical bytes: same hash, star, tags
            ref.file = target.string();
            dst.slots[size_t(s)] = std::move(ref);
            return s;
        }
        return -1;
    }

private:
    // Numbered names take their slot, the rest fill free slots in name order.
    static void AssignSlots(PatchBank &bank, std::vector<std::pair<std::string, PatchRef>> files) {
        std::sort(files.begin(), files.end(), [](auto &a, auto &b) { return Lower(a.first) < Lower(b.first); });
        std::vector<bool> placed(files.size(), false);
        for (size_t i = 0; i < files.size(); ++i) {
            const int s = SlotFromFileName(files[i].first);
            if (s >= 0 && bank.slots[size_t(s)].Empty()) bank.slots[size_t(s)] = files[i].second, placed[i] = true;
        }
        size_t next = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            if (placed[i]) continue;
            while (next < size_t(kBankSlots) && !bank.slots[next].Empty()) ++next;
            if (next >= size_t(kBankSlots)) break;
            bank.slots[next] = files[i].second;
        }
    }

    void AddFactory(const std::filesystem::path &factory) {
        std::error_code ec;
        if (factory.empty() || !std::filesystem::is_directory(factory, ec)) return;
        AddFolderBank(factory, kFactoryBankName, FolderEntries(factory), true);
    }

    // `isRoot`: loose .sy1 files directly in the root form a bank named
    // after it.
    void ScanFolder(const std::filesystem::path &dir, const std::string &prefix, bool isRoot) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec)) ScanEntries(dir, FolderEntries(dir), prefix, isRoot);
    }
    // Every folder is listed once (its entries serve the bank and the walk).
    void ScanEntries(const std::filesystem::path &dir, const std::vector<std::filesystem::directory_entry> &items,
                     const std::string &prefix, bool isRoot) {
        std::error_code ec;
        if (isRoot && std::any_of(items.begin(), items.end(), [](auto &e) { return IsSy1(e.path().filename().string()); }))
            AddFolderBank(dir, dir.filename().string(), items);
        for (const auto &e : items) {
            if (found_) return;
            if (!e.is_directory(ec)) continue;
            const std::string fn = e.path().filename().string();
            const auto children = FolderEntries(e.path());
            bool hasSy1 = false, hasDir = false;
            for (const auto &c : children) {
                hasSy1 |= IsSy1(c.path().filename().string());
                hasDir |= c.is_directory(ec);
            }
            if (hasSy1 || !hasDir) AddFolderBank(e.path(), prefix + fn, children);
            if (hasDir) ScanEntries(e.path(), children, prefix + fn + " / ", false);
        }
    }

    void AddFolderBank(const std::filesystem::path &dir, const std::string &name,
                       const std::vector<std::filesystem::directory_entry> &items, bool readOnly = false) {
        PatchBank bank;
        bank.name = name;
        bank.source = dir.string();
        bank.readOnly = readOnly; // before the publish: a streamed factory bank is read-only too
        std::vector<std::pair<std::string, PatchRef>> files;
        for (const auto &f : items) {
            const std::string fn = f.path().filename().string();
            std::string patchName, color;
            if (IsSy1(fn) && PatchNameCache::Shared().Name(f, patchName, color))
                files.push_back({fn, MakeRef(patchName, f.path(), &f, color)});
        }
        AssignSlots(bank, std::move(files));
        banks_.push_back(std::move(bank));
        found_ = stopAtPatch_ && banks_.back().Count() > 0;
        if (publish_) publish_(banks_.back()); // streamed to the UI mid-scan
    }

    // A resolved reference: the stored category override beats the name rule,
    // derived categories work without any sidecar at all.
    PatchRef MakeRef(const std::string &patchName, const std::filesystem::path &file,
                     const std::filesystem::directory_entry *entry = nullptr, std::string color = "default") const {
        PatchRef ref;
        ref.name = patchName;
        ref.file = file.string();
        ref.color = std::move(color);
        const int derived = DeriveCategory(ref.name, file.filename().string());
        ref.category = derived;
        if (tags_ == nullptr) return ref;
        const bool hashed = entry != nullptr ? PatchHashCache::Shared().Hash(*entry, ref.hash) : Sha256File(file, ref.hash);
        if (hashed) ApplyStoredTags(ref, *tags_, derived);
        return ref;
    }

    std::vector<PatchBank> banks_;
    const TagStore *tags_ = nullptr; // nullptr = nobody tagged: no hashing
    bool stopAtPatch_ = false, found_ = false;
    std::function<void(const PatchBank &)> publish_; // set by the background scan
};

// A program of the library: its bank folder RELATIVE to the library root
// ("" = the root's own loose patches; portable across machines) + the slot.
struct LibraryProgram {
    std::string bank;
    int slot = -1; // -1 = none (Init, a patch file, an unknown session)
    bool operator==(const LibraryProgram &) const = default;
};

inline std::string BankRelative(const std::filesystem::path &root, const std::string &source) {
    const std::string rel = std::filesystem::path(source).lexically_relative(root).generic_string();
    return rel == "." ? std::string() : rel;
}
inline std::string BankAbsolute(const std::filesystem::path &root, const std::string &rel) {
    return rel.empty() ? root.string() : (root / rel).string();
}
// LibraryProgram::bank of a bank of `lib` (scanned from `root`), and back
// (-1 = not in the library).
inline std::string ProgramBank(const PatchLibrary &lib, const std::filesystem::path &root, size_t bank) {
    const PatchBank &b = lib.Banks()[bank];
    return b.readOnly ? kFactoryProgramBank : BankRelative(root, b.source);
}
inline int FindProgramBank(const PatchLibrary &lib, const std::filesystem::path &root, const std::string &bank) {
    if (bank != kFactoryProgramBank) return lib.FindBySource(BankAbsolute(root, bank));
    for (size_t i = 0; i < lib.Banks().size(); ++i)
        if (lib.Banks()[i].readOnly) return int(i);
    return -1;
}

// What a fresh instance plays (Synth1: program 001 of the first bank): the
// first patch of the first bank holding one, in browser order. Reads only
// the banks up to that one (the factory bank comes first). False = no patch
// in the library.
inline bool FirstProgram(const std::filesystem::path &root, const std::filesystem::path &factory, Sy1Patch &patch,
                         LibraryProgram &where) {
    PatchLibrary lib;
    lib.ScanToFirstPatch(root, factory);
    if (lib.Banks().empty()) return false;
    const PatchBank &bank = lib.Banks().back();
    for (int s = 0; s < kBankSlots; ++s)
        if (!bank.slots[size_t(s)].Empty() && lib.Load(lib.Banks().size() - 1, size_t(s), patch)) {
            where = {ProgramBank(lib, root, lib.Banks().size() - 1), s};
            return true;
        }
    return false;
}

// ---- import: copy into the library ------------------------------------------

// What an import reads: a folder or a zip (zips inside either too) as one
// list of files, each with its folder path under the import destination.
struct ImportFile {
    std::vector<std::string> dirs;
    std::string name, data;
};

inline bool IsImportSource(const std::string &name) {
    return IsSy1(name) || Lower(std::filesystem::path(name).extension().string()) == ".zip";
}

inline std::string StemOf(const std::string &name) { return std::filesystem::path(name).stem().string(); }

// A zip's files under `prefix`, keeping its folders (one folder wrapping
// everything is dropped); an inner zip goes into a folder of its name.
inline void CollectZip(const std::string &bytes, const std::vector<std::string> &prefix, std::vector<ImportFile> &out,
                       int depth = 0) {
    ZipArchive z;
    if (!z.OpenData(bytes)) return;
    std::vector<std::pair<std::vector<std::string>, const ZipEntry *>> files;
    for (const ZipEntry &e : z.Entries()) {
        std::vector<std::string> parts;
        bool hidden = false;
        for (size_t at = 0; at < e.name.size() && !hidden;) {
            size_t end = e.name.find_first_of("/\\", at);
            if (end == std::string::npos) end = e.name.size();
            const std::string part = e.name.substr(at, end - at);
            at = end + 1;
            if (part.empty()) continue;
            hidden = IsHiddenName(part); // also "." / ".."
            parts.push_back(part);
        }
        if (!hidden && !parts.empty() && e.name.back() != '/' && IsImportSource(parts.back()))
            files.push_back({std::move(parts), &e});
    }
    while (!files.empty() && files[0].first.size() > 1 &&
           std::all_of(files.begin(), files.end(), [&](auto &f) { return f.first.size() > 1 && f.first[0] == files[0].first[0]; }))
        for (auto &f : files) f.first.erase(f.first.begin());
    for (auto &[parts, entry] : files) {
        ImportFile f{prefix, parts.back(), {}};
        f.dirs.insert(f.dirs.end(), parts.begin(), parts.end() - 1);
        if (!z.Read(*entry, f.data)) continue;
        if (Lower(std::filesystem::path(f.name).extension().string()) != ".zip") out.push_back(std::move(f));
        else if (depth < 4) f.dirs.push_back(StemOf(f.name)), CollectZip(f.data, f.dirs, out, depth + 1);
    }
}

// A folder's files under `prefix`: a zip goes into a folder of its name, a
// Synth1 `zipbank/` folder merges into its parent; a name taken by a sibling
// that holds something gets " 2" ("Dark" + "zipbank/Dark.zip").
inline void CollectFolder(const std::filesystem::path &src, const std::vector<std::string> &prefix,
                          std::vector<ImportFile> &out, std::vector<std::string> *taken = nullptr) {
    std::vector<std::string> local;
    if (!taken) taken = &local;
    const auto unique = [&](const std::string &name) {
        std::string n = name;
        for (int i = 2; std::find(taken->begin(), taken->end(), Lower(n)) != taken->end(); ++i) n = name + " " + std::to_string(i);
        return n;
    };
    std::error_code ec;
    for (const auto &p : FolderChildren(src)) {
        const std::string fn = p.filename().string();
        std::vector<ImportFile> sub;
        if (std::filesystem::is_directory(p, ec)) {
            if (Lower(fn) == "zipbank") {
                CollectFolder(p, prefix, out, taken);
                continue;
            }
            std::vector<std::string> dirs = prefix;
            dirs.push_back(unique(fn));
            CollectFolder(p, dirs, sub);
        } else if (Lower(p.extension().string()) == ".zip") {
            std::string bytes;
            std::vector<std::string> dirs = prefix;
            dirs.push_back(unique(p.stem().string()));
            if (ReadTextFile(p, bytes)) CollectZip(bytes, dirs, sub);
        } else if (IsImportSource(fn)) {
            ImportFile f{prefix, fn, {}};
            if (ReadTextFile(p, f.data)) out.push_back(std::move(f));
            continue;
        }
        if (sub.empty()) continue;
        taken->push_back(Lower(sub[0].dirs[prefix.size()]));
        for (auto &f : sub) out.push_back(std::move(f));
    }
}

// Writes what an import collected into `dest`: .sy1 files at their folder.
// Folders are created only for real files.
// Returns the number of patches written.
inline int WriteImport(const std::vector<ImportFile> &files, const std::filesystem::path &dest) {
    int n = 0;
    std::error_code ec;
    for (const ImportFile &f : files) {
        if (!IsSy1(f.name)) continue;
        std::filesystem::path out = dest;
        for (const auto &d : f.dirs) out /= SafeName(d);
        std::filesystem::create_directories(out, ec);
        std::ofstream file(UniquePath(out / SafeName(f.name)), std::ios::binary);
        file << f.data;
        n += bool(file);
    }
    return n;
}

// A zip's patches into `dest`.
inline int ExtractZipBank(const std::filesystem::path &zip, const std::filesystem::path &dest) {
    std::string bytes;
    std::vector<ImportFile> files;
    if (ReadTextFile(zip, bytes)) CollectZip(bytes, {}, files);
    return WriteImport(files, dest);
}

// A folder's patches into `dest`, recursively (zips extracted).
inline int CopyBankTree(const std::filesystem::path &src, const std::filesystem::path &dest) {
    std::vector<ImportFile> files;
    CollectFolder(src, {}, files);
    return WriteImport(files, dest);
}

// Banks shipped next to the factory bank (bundle resources) and COPIED into
// the library once per installed build: each sub-folder becomes a bank the
// user owns (write, delete, rename). The library's kSeededFile holds the
// build that seeded last: within one build a deleted / renamed bank stays
// so; a new build (install / update) adds again every starter bank whose
// name is free, never writing into an existing one.
inline constexpr const char *kStarterBanksFolder = "Synth2 Starter Banks";
inline constexpr const char *kSeededFile = ".starter_banks";

// rename() that never replaces `to` (POSIX rename would replace an empty
// folder created meanwhile): Apple's RENAME_EXCL, else exists + rename.
inline bool RenameNoReplace(const std::filesystem::path &from, const std::filesystem::path &to) {
#if defined(__APPLE__)
    return renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0;
#else
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::symlink_status(to, ec))) return false;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

// Unless `build` (the plugin's CFBundleVersion) already seeded this library,
// copies every starter bank whose name is free; a name already taken in the
// library root (the user's own bank, even empty, a file, a link) is NEVER
// written into. Each bank is written to a hidden temp folder renamed into
// place without replacing anything, so two instances starting at once can't
// leave a half bank or a "Bank 2". Returns the number of banks copied.
inline int SeedStarterBanks(const std::filesystem::path &root, const std::filesystem::path &starter,
                            const std::string &build) {
    std::error_code ec;
    if (root.empty() || starter.empty() || !std::filesystem::is_directory(starter, ec) ||
        !std::filesystem::is_directory(root, ec))
        return 0;
    const std::filesystem::path record = root / kSeededFile;
    const std::string stamp = "build " + build + "\n";
    std::string text;
    if (ReadTextFile(record, text) && text == stamp) return 0;
    int n = 0;
    for (const auto &src : FolderChildren(starter)) {
        const std::filesystem::path dest = root / SafeName(src.filename().string());
        if (!std::filesystem::is_directory(src, ec) || std::filesystem::exists(std::filesystem::symlink_status(dest, ec)))
            continue;
        const std::filesystem::path temp = root / (".oxo_seed_" + std::to_string(std::random_device{}()) + ".tmp");
        if (CopyBankTree(src, temp) > 0) n += RenameNoReplace(temp, dest);
        if (std::filesystem::exists(temp, ec)) std::filesystem::remove_all(temp, ec); // lost the race
    }
    std::ofstream(record, std::ios::binary | std::ios::trunc) << stamp;
    return n;
}

// What an import did: the new top-level folders (the first one gets
// selected), how many .sy1 files landed, and why anything was left out
// ("path: reason") so the browser can say more than "nothing imported".
struct ImportResult {
    // Folders the import created, each with how many .sy1 files landed in
    // it, so the browser can say "3 patches into Imported" and not just a
    // total.
    std::vector<std::pair<std::filesystem::path, int>> added;
    std::vector<std::string> refused;
    int patches = 0;
    bool Empty() const { return added.empty(); }
};

// The C-hook / IPC shape: one "patches=N" line, then "+<count> <folder>"
// per added bank, then "!<path>: <reason>" per refusal.
inline std::string ImportPayload(const ImportResult &r) {
    std::string out = "patches=" + std::to_string(r.patches);
    for (const auto &a : r.added) out += "\n+" + std::to_string(a.second) + " " + a.first.string();
    for (const auto &s : r.refused) out += "\n!" + s;
    return out;
}

// "Import…": COPIES what the user picked into the library root. A folder (a
// bank, a container, a whole Synth1 bank folder) is copied, a zip is
// extracted into a folder of its name, loose .sy1 files fill the free slots
// of the "Imported" bank. Never clobbers ("Bank 2"). Anything skipped is
// reported in `refused` - a silent no-op looked like a broken import.
inline ImportResult ImportItems(const std::filesystem::path &root, const std::vector<std::filesystem::path> &items) {
    ImportResult r;
    std::vector<std::filesystem::path> loose;
    std::error_code ec;
    for (std::filesystem::path p : items) {
        if (p.filename().empty()) p = p.parent_path(); // "dir/"
        const std::filesystem::path rel = root.lexically_relative(p);
        if (!rel.empty() && *rel.begin() != "..") {
            r.refused.push_back(p.string() + ": already in the library");
            continue;
        }
        std::filesystem::path dest;
        int n = 0;
        if (std::filesystem::is_directory(p, ec)) n = CopyBankTree(p, dest = UniquePath(root / SafeName(p.filename().string())));
        else if (Lower(p.extension().string()) == ".zip") n = ExtractZipBank(p, dest = UniquePath(root / SafeName(p.stem().string())));
        else if (IsSy1(p.filename().string())) { loose.push_back(p); continue; }
        else {
            r.refused.push_back(p.string() + ": not a bank folder, a zip or a .sy1 patch");
            continue;
        }
        if (n > 0) { r.added.emplace_back(dest, n); r.patches += n; }
        else r.refused.push_back(p.string() + ": no .sy1 patches found");
    }
    if (!loose.empty()) {
        const std::filesystem::path bank = root / "Imported";
        std::filesystem::create_directories(bank, ec);
        PatchLibrary existing;
        existing.Scan(bank);
        const int b = existing.FindBySource(bank.string());
        size_t next = 0;
        int n = 0, full = 0;
        for (const auto &p : loose) {
            while (next < size_t(kBankSlots) && ((b >= 0 && !existing.Banks()[size_t(b)].slots[next].Empty()) ||
                                                 std::filesystem::exists(bank / SlotFileName(int(next)), ec)))
                ++next;
            if (next >= size_t(kBankSlots)) { ++full; continue; } // keep going: say how many were left out
            n += std::filesystem::copy_file(p, bank / SlotFileName(int(next++)), ec);
        }
        if (n > 0) { r.added.emplace_back(bank, n); r.patches += n; }
        if (full > 0) r.refused.push_back("Imported bank is full (" + std::to_string(kBankSlots) + " patches): " + std::to_string(full) + " left out");
    }
    return r;
}

// Paths as newline-separated text in, the payload above out (C hooks).
inline std::string ImportItemsText(const std::filesystem::path &root, const std::string &paths) {
    std::vector<std::filesystem::path> items;
    for (size_t at = 0; at < paths.size();) {
        size_t end = paths.find('\n', at);
        if (end == std::string::npos) end = paths.size();
        if (end > at) items.emplace_back(paths.substr(at, end - at));
        at = end + 1;
    }
    return ImportPayload(ImportItems(root, items));
}

// ---- the user's library: one root folder + operations -------------------------

class PatchManager {
public:
    PatchManager() = default;
    PatchManager(const PatchManager &) = delete;
    PatchManager &operator=(const PatchManager &) = delete;
    ~PatchManager() {
        if (worker_.joinable()) worker_.join();
    }

    // `root` = the library folder (created with an empty "User" bank),
    // `factory` = the read-only factory bank folder in the plugin bundle; its
    // sibling kStarterBanksFolder is seeded into the library once per `build`
    // (the plugin's CFBundleVersion).
    void Configure(const std::filesystem::path &root, const std::filesystem::path &factory = {},
                   const std::string &build = {}) {
        root_ = root;
        factory_ = factory;
        EnsureRoot();
        tags_.Load(root_);
        if (!factory.empty()) SeedStarterBanks(root_, factory.parent_path() / kStarterBanksFolder, build);
    }
    // Moves a file to the Trash, false = no Trash here (then it is deleted).
    void SetTrash(std::function<bool(const std::filesystem::path &)> trash) { trash_ = std::move(trash); }

    const std::filesystem::path &Root() const { return root_; }
    const std::filesystem::path &Factory() const { return factory_; }
    const PatchLibrary &Library() const { return lib_; }
    const TagStore &Tags() const { return tags_; }

#if defined(_WIN32)
    void AdoptIndex(std::vector<PatchBank> banks) {
        if (scanned_) return;
        lib_.AdoptIndex(std::move(banks));
        scanned_ = true;
        ++revision_;
    }
#endif

    void Rescan() {
        EnsureRoot();
        ++generation_;
        tags_.Load(root_); // the sidecar may have changed under us (sync, import)
        ExpandZips(root_, trash_, trashed_, removed_);
        lib_.Scan(root_, factory_, &tags_);
        scanned_ = true;
        ++revision_;
    }

    // The same rescan on a worker thread (the browser opens at once, big
    // libraries take a while). One at a time: a call while one runs is
    // dropped (the running one started moments ago). PollRescan (the
    // caller's thread, the editor's poll) adopts the result; a library
    // change made in between (write, move, sync rescan...) wins, the
    // worker's older list is dropped.
    void StartRescan() {
        if (worker_.joinable()) return;
        EnsureRoot();
        ready_ = false;
        tags_.Load(root_);
        // The browser shows what is being built, so the list it was showing
        // goes away NOW (also on Refresh): it fills in bank by bank.
        lib_ = PatchLibrary();
        scanned_ = true; // not false: Json() would scan synchronously
        scan_.Reset();
        // The denominator is known before the worker starts: the count is of the
        // library root's own entries, no file is opened for it.
        scanTotal_.store(TopLevelCount(root_), std::memory_order_relaxed);
        ++revision_;
        const int gen = generation_;
        worker_ = std::thread([this, root = root_, factory = factory_, trash = trash_, gen, tags = tags_] {
            BackgroundScan s;
            s.generation = gen;
            s.lib.SetPublishSink([&s](const PatchBank &bank) {
                std::lock_guard<std::mutex> lock(s.mu);
                s.pending.push_back(bank);
            });
            ExpandZips(root, trash, s.trashed, s.removed);
            s.lib.Scan(root, factory, &tags);
            std::lock_guard<std::mutex> lock(scan_.mu);
            scan_.generation = s.generation;
            scan_.lib = std::move(s.lib);
            scan_.pending = std::move(s.pending);
            scan_.trashed = std::move(s.trashed);
            scan_.removed = std::move(s.removed);
            ready_.store(true, std::memory_order_release);
        });
    }
    // True = a new list was adopted (Revision() moved).
    bool PollRescan() {
        if (!worker_.joinable()) return false;
        // Banks finished since the last poll: adopted while the scan still
        // runs, so the list GROWS instead of arriving all at once. The scan
        // order is the bank order (factory, then a depth-first walk), so a
        // partial list is always a prefix and bank indices stay put.
        std::vector<PatchBank> arrived;
        {
            std::lock_guard<std::mutex> lock(scan_.mu);
            if (scan_.generation == generation_) arrived.swap(scan_.pending);
        }
        if (!arrived.empty()) {
            for (PatchBank &b : arrived) lib_.Banks().push_back(std::move(b));
            ++revision_;
        }
        if (!ready_.load(std::memory_order_acquire)) return !arrived.empty();
        worker_.join();
        trashed_.insert(trashed_.end(), scan_.trashed.begin(), scan_.trashed.end()); // done either way
        removed_.insert(removed_.end(), scan_.removed.begin(), scan_.removed.end());
        const bool fresh = scan_.generation == generation_;
        if (fresh) {
            // The worker's own library holds every bank: the list on screen is
            // that one, whatever it had adopted so far (a partial list is a
            // prefix, so this only appends).
            lib_.Banks() = std::move(scan_.lib.Banks());
            lastScanTotal_.store(int32_t(lib_.Banks().size()), std::memory_order_relaxed);
            ++revision_;
        }
        scan_.Reset();
        scanned_ = true;
        if (!fresh) Rescan(); // an edit won mid-stream: put the whole list back
        return true;
    }

    int32_t LibraryPatchCount() const {
        int32_t n = 0;
        for (const PatchBank &b : lib_.Banks()) n += b.Count();
        return n;
    }

    // Folders directly in the library root. Every one is a bank (or a
    // container of them), so it is the closest cheap estimate of how many
    // banks a scan will find. Loose root .sy1 files form a bank too.
    static int32_t TopLevelCount(const std::filesystem::path &root) {
        std::error_code ec;
        int32_t n = 0;
        bool loose = false;
        for (const auto &e : FolderEntries(root)) {
            if (e.is_directory(ec)) ++n;
            else if (IsSy1(e.path().filename().string())) loose = true;
        }
        return n + (loose ? 1 : 0);
    }

    // Banks / patches on screen so far, out of what the scan estimates it
    // will find: the finished scan's own total, else the number of folders in
    // the library root (an honest denominator for a flat library; sub-banks
    // are counted through their parent, so the estimate can undershoot and
    // the browser stops calling it a fraction once it does).
    void ScanProgress(int32_t &banks, int32_t &patches, int32_t &total) {
        banks = int32_t(lib_.Banks().size());
        patches = LibraryPatchCount();
        total = worker_.joinable() ? scanTotal_.load(std::memory_order_relaxed)
                                   : lastScanTotal_.load(std::memory_order_relaxed);
        if (total < banks) total = banks; // the estimate may undershoot sub-banks
    }
    bool Scanning() const { return worker_.joinable(); }
    // Bumps on every change of the list (rescans, writes, an adopted
    // background scan).
    int Revision() const { return revision_; }

    // What the last rescans did to zips ("Bank.zip imported and moved to
    // Trash"), "" = nothing; cleared by the call.
    std::string TakeNotice() {
        std::string n;
        const auto list = [](const std::vector<std::string> &v) {
            std::string s;
            for (const auto &x : v) s += (s.empty() ? "" : ", ") + x;
            return s;
        };
#if defined(_WIN32)
        if (!trashed_.empty()) n = list(trashed_) + " imported and moved to the Recycle Bin";
#else
        if (!trashed_.empty()) n = list(trashed_) + " imported and moved to Trash";
#endif
        if (!removed_.empty()) n += (n.empty() ? "" : "; ") + list(removed_) + " imported (zip removed)";
        trashed_.clear(), removed_.clear();
        return n;
    }
    void EnsureScanned() {
        if (!scanned_) Rescan();
    }

    bool Load(int bank, int slot, Sy1Patch &out) {
        EnsureScanned();
        return bank >= 0 && slot >= 0 && lib_.Load(size_t(bank), size_t(slot), out);
    }
    bool Write(int bank, int slot, const Sy1Patch &p) {
        EnsureScanned();
        // A rewrite changes the file's bytes, so its hash: the slot's star,
        // tags and category travel with it (the user edited THIS slot).
        // A bank that has not streamed in yet is not on screen, so nothing can
        // be written into it (the indices that ARE there are stable: a partial
        // list is always a prefix of the final one).
        std::string oldHash;
        if (bank >= 0 && slot >= 0 && size_t(bank) < lib_.Banks().size() && slot < kBankSlots)
            oldHash = lib_.Banks()[size_t(bank)].slots[size_t(slot)].hash;
        PatchTags carried;
        if (const PatchTags *stored = oldHash.empty() ? nullptr : tags_.Find(oldHash); stored != nullptr) carried = *stored;
        if (bank < 0 || slot < 0 || !lib_.Write(size_t(bank), size_t(slot), p)) return false;
        const std::string newHash = lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
        if (!oldHash.empty() && oldHash != newHash) {
            if (!carried.Empty() && !newHash.empty()) tags_.Set(newHash, carried);
            // The old bytes are gone; keep their record while another slot
            // still holds them (a copy of this patch).
            if (!HashInUse(oldHash, bank, slot)) tags_.Set(oldHash, PatchTags());
            lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
            SaveTags();
        }
        ++generation_, ++revision_;
        return true;
    }

    int Copy(int bank, int slot, int to) {
        EnsureScanned();
        const int s = bank < 0 || slot < 0 || to < 0 ? -2 : lib_.Copy(size_t(bank), size_t(slot), size_t(to));
        if (s >= 0) ++generation_, ++revision_;
        return s;
    }

    // Browser drag and drop: the patch of slot `from` goes to slot `to`, an
    // occupied `to` swaps. Done by RENAMING: every patch of the bank whose
    // name doesn't match its (new) slot becomes NNN.sy1, so files without a
    // number can't drift into the freed slot on the next scan. Two phases
    // (hidden temp names first) so no rename clobbers a file. Rescans.
    bool Move(int bank, int from, int to) {
        EnsureScanned();
        const auto &banks = lib_.Banks();
        if (bank < 0 || size_t(bank) >= banks.size() || from < 0 || to < 0 || from >= kBankSlots ||
            to >= kBankSlots || from == to || banks[size_t(bank)].readOnly ||
            banks[size_t(bank)].slots[size_t(from)].Empty())
            return false;
        const std::filesystem::path dir = banks[size_t(bank)].source;
        std::array<std::string, kBankSlots> files;
        for (int s = 0; s < kBankSlots; ++s) files[size_t(s)] = banks[size_t(bank)].slots[size_t(s)].file;
        std::swap(files[size_t(from)], files[size_t(to)]);
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames; // temp -> final
        std::error_code ec;
        bool ok = true;
        for (int s = 0; s < kBankSlots && ok; ++s) {
            if (files[size_t(s)].empty()) continue;
            const std::filesystem::path file = files[size_t(s)], final = dir / SlotFileName(s);
            if (file == final) continue;
            const std::filesystem::path temp = dir / (".oxo_move_" + std::to_string(s) + ".tmp");
            std::filesystem::rename(file, temp, ec);
            ok = !ec;
            if (ok) {
                PatchNameCache::Shared().Forget(file);
                PatchNameCache::Shared().Forget(final);
                renames.push_back({temp, final});
            }
        }
        for (const auto &[temp, final] : renames) {
            std::filesystem::rename(temp, final, ec);
            ok = ok && !ec;
        }
        Rescan();
        return ok;
    }
    // Browser > Delete bank: its folder goes to the Trash (deleted where there
    // is none), or only its .sy1 files when the folder also holds sub-folders
    // (other banks) or is the root itself. Never the factory bank, never
    // anything outside the root. Rescans.
    bool DeleteBank(int bank) {
        EnsureScanned();
        const auto &banks = lib_.Banks();
        if (bank < 0 || size_t(bank) >= banks.size() || banks[size_t(bank)].readOnly) return false;
        const std::filesystem::path dir = banks[size_t(bank)].source;
        const std::filesystem::path rel = dir.lexically_normal().lexically_relative(root_.lexically_normal());
        if (root_.empty() || rel.empty() || *rel.begin() == "..") return false;
        std::error_code ec;
        const auto items = FolderChildren(dir);
        const bool wholeFolder = rel != "." && std::none_of(items.begin(), items.end(), [&](auto &p) {
            return std::filesystem::is_directory(p, ec);
        });
        bool ok = true;
        // Bank deletion implies its patches': nobody wants dead records
        // (a hash never comes back, a re-copied file would inherit a star).
        const std::vector<PatchRef> removed(banks[size_t(bank)].slots.begin(), banks[size_t(bank)].slots.end());
        if (wholeFolder) ok = Remove(dir);
        else
            for (const auto &p : items)
                if (IsSy1(p.filename().string()) && !std::filesystem::is_directory(p, ec)) ok = Remove(p) && ok;
        Rescan();
        if (ok) ForgetPatches(removed);
        return ok;
    }

    // Browser > Export bank: a folder named after the bank's (" 2" when
    // taken) is created in `dest` with the bank's patches, file names kept
    // (so the slots stay; sub-banks are not part of it). Any bank, the
    // factory one too. Returns the new folder, "" = failed.
    std::string ExportBank(int bank, const std::filesystem::path &dest) {
        EnsureScanned();
        const auto &banks = lib_.Banks();
        std::error_code ec;
        if (bank < 0 || size_t(bank) >= banks.size() || !std::filesystem::is_directory(dest, ec)) return {};
        const PatchBank &b = banks[size_t(bank)];
        const std::filesystem::path out = UniquePath(dest / SafeName(std::filesystem::path(b.source).filename().string()));
        if (!std::filesystem::create_directory(out, ec)) return {};
        bool ok = true;
        for (const PatchRef &p : b.slots)
            if (!p.Empty()) ok = std::filesystem::copy_file(p.file, out / std::filesystem::path(p.file).filename(), ec) && ok;
        // A sidecar of just this bank's records, so importing the folder on
        // another machine restores its stars and tags. No records, no file.
        TagStore carried;
        for (const PatchRef &p : b.slots)
            if (const PatchTags *stored = p.hash.empty() ? nullptr : tags_.Find(p.hash); stored != nullptr)
                carried.Set(p.hash, *stored), carried.SetNameHint(p.hash, p.name);
        if (ok && !carried.Empty()) ok = carried.SaveFile(out / TagStore::kFileName);
        return ok ? out.string() : std::string();
    }

    std::string SlotFile(int bank, int slot) {
        EnsureScanned();
        return bank >= 0 && slot >= 0 ? lib_.SlotFile(size_t(bank), size_t(slot)) : std::string();
    }

    // New empty bank folder in the library root; returns its index.
    int NewBank(const std::string &name) {
        const std::filesystem::path dir = UniquePath(root_ / SafeName(name.empty() ? "New Bank" : name));
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) return -1;
        Rescan();
        return lib_.FindBySource(dir.string());
    }

    std::string Import(const std::string &paths) {
        EnsureRoot();
        const std::string added = ImportItemsText(root_, paths);
        // A categories.json brought along (a folder exported elsewhere, a
        // library tagged on another machine) MERGES into ours: patches we
        // already know keep their stars and tags, only hashes we never saw
        // are added. Never replace - importing must not lose local work.
        MergeBroughtTags(paths);
        Rescan();
        return added;
    }

    // ---- categories, favourites, tags ---------------------------------------
    // Each mutation updates the slot in place (the caller's list stays right),
    // saves the sidecar and does NOT rescan (nothing on disk moved).

    // `category` = a kCat* value (kCatUnclassified stores an explicit
    // "Unclassified"), or -1 = forget the override and derive it again.
    bool SetCategory(int bank, int slot, int category) {
        EnsureScanned();
        PatchRef *ref = MutableRef(bank, slot);
        if (ref == nullptr || !EnsureHash(*ref) || (category >= 0 && !IsCategory(category))) return false;
        const std::string hash = ref->hash;
        tags_.Update(hash, [&](PatchTags &patch) {
            patch.category = category < 0 ? int(kCatUnclassified) : category;
            patch.overridden = category >= 0;
        });
        lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
        return SaveTags();
    }
    // True = stars, false = un-stars (one call for both, the UI toggles).
    bool SetFavourite(int bank, int slot, bool favourite) {
        EnsureScanned();
        PatchRef *ref = MutableRef(bank, slot);
        if (ref == nullptr || !EnsureHash(*ref)) return false;
        const std::string hash = ref->hash;
        tags_.Update(hash, [&](PatchTags &patch) { patch.favourite = favourite; });
        lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
        return SaveTags();
    }
    bool AddPatchTag(int bank, int slot, const std::string &tag) {
        EnsureScanned();
        PatchRef *ref = MutableRef(bank, slot);
        if (ref == nullptr || !EnsureHash(*ref)) return false;
        const std::string hash = ref->hash;
        bool changed = false;
        tags_.Update(hash, [&](PatchTags &patch) { changed = AddTag(patch, tag); });
        if (!changed) return false; // full / duplicate / nothing left of it
        lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
        return SaveTags();
    }
    bool RemovePatchTag(int bank, int slot, const std::string &tag) {
        EnsureScanned();
        PatchRef *ref = MutableRef(bank, slot);
        if (ref == nullptr || !EnsureHash(*ref)) return false;
        const std::string hash = ref->hash;
        bool changed = false;
        tags_.Update(hash, [&](PatchTags &patch) { changed = RemoveTag(patch, tag); });
        if (!changed) return false;
        lib_.ResolveTags(size_t(bank), size_t(slot), tags_);
        return SaveTags();
    }

    // The tag chips: every tag in the library, sorted.
    std::vector<std::string> TagVocabulary() const { return tags_.Vocabulary(); }

    // A patch's colour lives in its own .sy1 (`color=`), so this rewrites the
    // file: the patch's hash changes, and Write carries its star / tags /
    // category over to the new bytes (same path a re-save takes).
    bool SetPatchColor(int bank, int slot, const std::string &color) {
        EnsureScanned();
        if (bank < 0 || slot < 0) return false;
        Sy1Patch patch;
        if (!Load(bank, slot, patch)) return false;
        if (patch.color == color) return true; // nothing to rewrite
        patch.color = color.empty() ? "default" : color;
        return Write(bank, slot, patch);
    }

    // Patches whose BYTES are identical, everywhere in the library: an
    // import that brought the same module twice, a bank copied back in,
    // a save that took another patch's bytes. Synth1 slots hold no
    // identity, so the file's SHA-256 is the only truth. Payload: a
    // "groups=N" line, then one line per group, "bank,slot" tokens
    // space separated (names / colours come from Json, so a group
    // carries only where its members ARE). "" = library still loading.
    // Nothing is connected to the scan worker: it hashes the whole
    // library (~0.2 s once on 19,200 patches, then cached), so a
    // second call is instant.
    std::string FindDuplicates() {
        EnsureScanned();
        if (Scanning()) return {};
        std::unordered_map<std::string, std::vector<int>> byHash;
        const auto &banks = lib_.Banks();
        for (size_t b = 0; b < banks.size(); ++b) {
            for (size_t s = 0; s < size_t(kBankSlots); ++s) {
                const PatchRef &ref = banks[b].slots[s];
                if (ref.Empty()) continue;
                std::string hash = ref.hash;
                if (hash.empty()) {
                    std::error_code ec;
                    const std::filesystem::directory_entry entry(std::filesystem::path(ref.file), ec);
                    if (ec || !PatchHashCache::Shared().Hash(entry, hash)) continue;
                }
                byHash[hash].push_back(int(b));
                byHash[hash].push_back(int(s));
            }
        }
        std::vector<std::vector<int>> groups;
        for (auto &pair : byHash)
            if (pair.second.size() >= 4) groups.push_back(std::move(pair.second));
        std::sort(groups.begin(), groups.end());
        std::string out = "groups=" + std::to_string(groups.size()) + "\n";
        for (const std::vector<int> &group : groups) {
            for (size_t i = 0; i + 1 < group.size(); i += 2) {
                if (i != 0) out += ' ';
                out += std::to_string(group[i]) + "," + std::to_string(group[i + 1]);
            }
            out += '\n';
        }
        return out;
    }

    // "Tags…" > Export: the whole store (every patch of every library this
    // user tagged, not just this folder) as one file to keep somewhere safe.
    bool ExportTags(const std::filesystem::path &file) const { return tags_.SaveFile(file); }

    // "Tags…" > Import: MERGE a file made on another machine into ours.
    // Returns how many patches the file added; 0 = nothing new / unreadable.
    int ImportTags(const std::filesystem::path &file) {
        TagStore incoming;
        incoming.LoadFile(file);
        if (incoming.Empty()) return 0;
        const std::size_t added = tags_.MergeMissing(incoming);
        if (added == 0 || !SaveTags()) return 0;
        Rescan(); // the list's stars / tags are now stale
        return int(added);
    }

    // {"root":..,"catNames":[..],"tagVocab":[..],
    //  "banks":[{"name","source","readOnly",
    //            "slots":[128 names, "" = empty],
    //            "cats":[128 kCat* values],"favs":[128 true/false],
    //            "tags":[[128 lists of tags]]}]}
    // The per-slot arrays are parallel to "slots", so the UI indexes them alike.
    std::string Json() {
        EnsureScanned();
        std::string j = "{\"root\":" + JsonString(root_.string()) + ",\"catNames\":[";
        for (int c = 0; c < kCategoryCount; ++c) j += (c ? "," : "") + JsonString(CategoryName(c));
        const std::vector<std::string> vocab = tags_.Vocabulary();
        j += "],\"tagVocab\":[";
        for (size_t t = 0; t < vocab.size(); ++t) j += (t ? "," : "") + JsonString(vocab[t]);
        j += "],\"banks\":[";
        const auto &banks = lib_.Banks();
        for (size_t b = 0; b < banks.size(); ++b) {
            j += (b ? ",{" : "{");
            j += "\"name\":" + JsonString(banks[b].name) + ",\"source\":" + JsonString(banks[b].source) +
                 ",\"readOnly\":" + (banks[b].readOnly ? "true" : "false") + ",\"slots\":[";
            for (int s = 0; s < kBankSlots; ++s) j += (s ? "," : "") + JsonString(banks[b].slots[size_t(s)].name);
            j += "],\"cats\":[";
            for (int s = 0; s < kBankSlots; ++s)
                j += (s ? "," : "") + std::to_string(banks[b].slots[size_t(s)].category);
            j += "],\"favs\":[";
            for (int s = 0; s < kBankSlots; ++s)
                j += (s ? "," : "") + std::string(banks[b].slots[size_t(s)].favourite ? "true" : "false");
            j += "],\"tags\":[";
            for (int s = 0; s < kBankSlots; ++s) {
                j += (s ? "," : "") + std::string("[");
                const auto &tags = banks[b].slots[size_t(s)].tags;
                for (size_t t = 0; t < tags.size(); ++t) j += (t ? "," : "") + JsonString(tags[t]);
                j += "]";
            }
            j += "],\"colors\":[";
            for (int s = 0; s < kBankSlots; ++s)
                j += (s ? "," : "") + JsonString(banks[b].slots[size_t(s)].color);
            j += "]}";
        }
        return j + "]}";
    }

private:
    PatchRef *MutableRef(int bank, int slot) {
        return bank >= 0 && slot >= 0 && size_t(bank) < lib_.Banks().size() && slot < kBankSlots
                   ? &lib_.Banks()[size_t(bank)].slots[size_t(slot)]
                   : nullptr;
    }

    // The sidecar is tiny: save it on every edit, so a crash never loses a
    // star. No records at all = no file to keep.
    bool SaveTags() {
        if (tags_.Empty()) { // nothing tagged anywhere: no file to keep
            std::error_code ec;
            std::filesystem::remove(root_ / TagStore::kFileName, ec);
            return true;
        }
        for (const auto &bank : lib_.Banks())
            for (const PatchRef &ref : bank.slots)
                if (!ref.hash.empty() && tags_.Find(ref.hash) != nullptr) tags_.SetNameHint(ref.hash, ref.name);
        return tags_.Save(root_);
    }

    // A deleted patch's record goes with it.
    void ForgetPatches(const std::vector<PatchRef> &slots) {
        bool dropped = false;
        for (const PatchRef &ref : slots)
            if (!ref.hash.empty() && tags_.Find(ref.hash) != nullptr && !HashInUse(ref.hash, -1, -1))
                tags_.Set(ref.hash, PatchTags()), dropped = true;
        if (dropped) SaveTags();
    }

    // An imported FOLDER may carry a categories.json of its own (an exported
    // bank does): its records MERGE into ours, never replacing ours.
    void MergeBroughtTags(const std::string &sourcePaths) {
        bool changed = false;
        for (size_t at = 0; at < sourcePaths.size();) {
            size_t end = sourcePaths.find('\n', at);
            if (end == std::string::npos) end = sourcePaths.size();
            const std::filesystem::path dir(sourcePaths.substr(at, end - at));
            at = end + 1;
            if (dir.empty()) continue;
            std::error_code ec;
            const std::filesystem::path file = dir / TagStore::kFileName;
            if (!std::filesystem::is_regular_file(file, ec)) continue;
            TagStore incoming;
            incoming.LoadFile(file);
            if (tags_.MergeMissing(incoming) > 0) changed = true;
        }
        if (changed) tags_.Save(root_);
    }

    // Another slot still holding these bytes (its tags must not be dropped
    // when THIS slot is rewritten).
    bool HashInUse(const std::string &hash, int bank, int slot) const {
        const auto &banks = lib_.Banks();
        for (size_t b = 0; b < banks.size(); ++b)
            for (size_t s = 0; s < size_t(kBankSlots); ++s)
                if (banks[b].slots[s].hash == hash && !(int(b) == bank && int(s) == slot)) return true;
        return false;
    }

    using TrashFn = std::function<bool(const std::filesystem::path &)>;
    // Built on the worker thread, drained by PollRescan on the UI thread. A
    // finished bank is MOVED here (O(1)), never the whole growing list
    // (O(n^2) over a scan), so the browser can show banks as they arrive.
    struct BackgroundScan {
        int generation = 0;
        PatchLibrary lib;
        std::mutex mu;
        std::vector<PatchBank> pending; // finished banks not yet adopted
        std::vector<std::string> trashed, removed;

        BackgroundScan() = default;
        BackgroundScan(const BackgroundScan &) = delete;
        BackgroundScan &operator=(const BackgroundScan &) = delete;

        // A mutex can't be assigned, so a finished scan is cleared by hand.
        void Reset() {
            std::lock_guard<std::mutex> lock(mu);
            generation = 0;
            lib = PatchLibrary();
            pending.clear();
            trashed.clear(), removed.clear();
        }
    };

    // A zip put into the library counts as an import: extracted into a
    // folder of its name next to it, then trashed (or every rescan would
    // extract it again). A zip without patches is left alone (ignored).
    // One expansion at a time process-wide (UI rescans, background scans).
    static void ExpandZips(const std::filesystem::path &dir, const TrashFn &trash, std::vector<std::string> &trashed,
                           std::vector<std::string> &removed) {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        ExpandZipsIn(dir, trash, trashed, removed);
    }
    static void ExpandZipsIn(const std::filesystem::path &dir, const TrashFn &trash, std::vector<std::string> &trashed,
                             std::vector<std::string> &removed) {
        std::error_code ec;
        for (const auto &e : FolderEntries(dir)) {
            const std::filesystem::path &p = e.path();
            if (e.is_directory(ec)) {
                ExpandZipsIn(p, trash, trashed, removed);
            } else if (Lower(p.extension().string()) == ".zip" &&
                       ExtractZipBank(p, UniquePath(dir / SafeName(p.stem().string()))) > 0) {
                const std::string name = Utf8Clean(p.filename().string(), '?');
                if (trash && trash(p)) trashed.push_back(name);
                else if (std::filesystem::remove(p, ec)) removed.push_back(name);
            }
        }
    }

    // To the Trash, else deleted (iOS: no Trash).
    bool Remove(const std::filesystem::path &p) {
        if (trash_ && trash_(p)) return true;
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        return !std::filesystem::exists(p, ec);
    }

    void EnsureRoot() const {
        std::error_code ec;
        if (!root_.empty() && !std::filesystem::exists(root_, ec)) std::filesystem::create_directories(root_ / kUserBankName, ec);
    }

    std::filesystem::path root_, factory_;
    PatchLibrary lib_;
    TagStore tags_;
    bool scanned_ = false;
    TrashFn trash_;
    std::vector<std::string> trashed_, removed_;
    int generation_ = 0, revision_ = 0; // generation: what a background scan must still match
    std::thread worker_;
    std::atomic<bool> ready_{false};
    std::atomic<int32_t> scanTotal_{0}; // the running scan's denominator
    std::atomic<int32_t> lastScanTotal_{0}; // banks the last finished scan found
    BackgroundScan scan_;
};

} // namespace oxo::s2
