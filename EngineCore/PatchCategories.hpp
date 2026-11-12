#pragma once

// Categories, favourites and tags for the patch library.
//
// A .sy1 file has no room for extra data (it is Synth1's format: one value per
// line, index = line number) and we must never write into one, so all of this
// lives in a SIDECAR file at the library root, `categories.json`, keyed by the
// SHA-256 of the patch's BYTES. Bytes, not paths: Write / Move rename files and
// users rename banks, but an untouched patch keeps its hash - and a copied patch
// (a favourite copied into another bank) carries its tags along for free. The
// sidecar holds no absolute paths, so moving the whole library keeps everything.
//
// Two axes, on purpose:
//   * category - ONE value from a short, type-only list (what the sound IS).
//     Derived from the patch name by an ordered rule table, so a fresh library
//     is already sorted; the user can override it per patch.
//   * tags     - free-form, many values (how the sound FEELS / is used). A
//     single-value field cannot say "dark pad", a second one can.
//
// Self-contained (PatchLibrary.hpp includes it) and portable: own ASCII helpers
// and a small SHA-256. No CommonCrypto, no JSON dependency, no platform headers
// (Windows / Linux ports are wanted).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oxo::s2 {

// --- categories -------------------------------------------------------------

enum Category : std::uint8_t {
    kCatUnclassified = 0,
    kCatBass,
    kCatLead,
    kCatPluck,
    kCatPad,
    kCatStrings,
    kCatBrass,
    kCatKeys,
    kCatOrgan,
    kCatPiano,
    kCatBell,
    kCatArp,
    kCatChord,
    kCatSequence,
    kCatFX,
    kCatPerc,
    kCatVocal,
    kCatGuitar,
    kCatWind,
    kCatMallet,
    kCatTexture,
    kCatInit,
    kCategoryCount
};

// Display / sidecar names, indexed by Category. The sidecar stores the NAME
// (never the index) so inserting a category here cannot silently re-label files
// written by an older build.
inline constexpr const char *kCategoryNames[kCategoryCount] = {
    "Unclassified", "Bass",     "Lead",   "Pluck",  "Pad",    "Strings", "Brass",
    "Keys",         "Organ",    "Piano",  "Bell",   "Arp",    "Chord",   "Sequence",
    "FX",           "Perc",     "Vocal",  "Guitar", "Wind",   "Mallet",  "Texture",
    "Init",
};

static_assert(sizeof(kCategoryNames) / sizeof(kCategoryNames[0]) == kCategoryCount,
              "kCategoryNames must list every category");

inline bool IsCategory(int id) { return id >= 0 && id < kCategoryCount; }

// Out of range (e.g. an id from a newer build) reads as Unclassified.
inline const char *CategoryName(int id) { return IsCategory(id) ? kCategoryNames[id] : kCategoryNames[0]; }

// -1 = unknown name. Case-insensitive, surrounding blanks ignored.
inline int CategoryId(std::string_view name) {
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
    for (int i = 0; i < kCategoryCount; ++i) {
        const std::string_view candidate(kCategoryNames[i]);
        if (candidate.size() != name.size()) continue;
        bool same = true;
        for (size_t c = 0; c < candidate.size() && same; ++c) {
            char a = name[c], b = candidate[c];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            same = a == b;
        }
        if (same) return i;
    }
    return -1;
}

// --- name rules -------------------------------------------------------------

struct CategoryRule {
    std::string_view phrase;  // one or more words, matched whole (case-insensitive)
    Category category;
    bool leadingOnly;         // true = must be the FIRST word (bucket abbreviations)
};

// Order breaks same-position TIES only (CategoryFromWords prefers the last
// match); it exists so that a drum word reads as Perc before a type word. The
// bucket abbreviations (bas, syn, dub, arp) are leading-only and so never
// compete: they are the Synth1 prefix convention, not English words.
inline constexpr CategoryRule kCategoryRules[] = {
    // 1. drums and percussion (whole words: "bs" must not match "obsession").
    {"bass drum", kCatPerc, false},   {"snare drum", kCatPerc, false},
    {"hi hat", kCatPerc, false},      {"closed hat", kCatPerc, false},
    {"open hat", kCatPerc, false},    {"kick", kCatPerc, false},
    {"snare", kCatPerc, false},       {"hat", kCatPerc, false},
    {"clap", kCatPerc, false},        {"tom", kCatPerc, false},
    {"rim", kCatPerc, false},         {"conga", kCatPerc, false},
    {"shaker", kCatPerc, false},      {"tamb", kCatPerc, false},
    {"cowbell", kCatPerc, false},     {"perc", kCatPerc, false},
    {"drum", kCatPerc, false},        {"drums", kCatPerc, false},

    // 2. the designer's buckets and the plainest type words.
    {"pad", kCatPad, false},          {"padding", kCatPad, false},
    {"string", kCatStrings, false},   {"strings", kCatStrings, false},
    {"bass", kCatBass, false},        {"arp", kCatArp, false},
    {"arpeggio", kCatArp, false},     {"sequence", kCatSequence, false},
    {"seq", kCatSequence, false},     {"chord", kCatChord, false},
    {"chords", kCatChord, false},     {"stab", kCatChord, false},
    {"stabs", kCatChord, false},      {"pluck", kCatPluck, false},
    {"plucks", kCatPluck, false},     {"koto", kCatPluck, false},
    {"harp", kCatPluck, false},       {"brass", kCatBrass, false},
    {"synbrass", kCatBrass, false},   {"horn", kCatBrass, false},
    {"horns", kCatBrass, false},      {"fanfare", kCatBrass, false},
    {"pizzicato", kCatStrings, false}, {"cello", kCatStrings, false},
    {"viola", kCatStrings, false},    {"violin", kCatStrings, false},
    {"organ", kCatOrgan, false},      {"piano", kCatPiano, false},
    {"harpsichord", kCatPiano, false}, {"clav", kCatKeys, false},
    {"keys", kCatKeys, false},        {"keyboard", kCatKeys, false},
    {"bell", kCatBell, false},        {"bells", kCatBell, false},
    {"chime", kCatBell, false},       {"chimes", kCatBell, false},
    {"music box", kCatBell, false},   {"mallet", kCatMallet, false},
    {"vibes", kCatMallet, false},     {"marimba", kCatMallet, false},
    {"dulcimer", kCatMallet, false},  {"guitar", kCatGuitar, false},
    {"strat", kCatGuitar, false},     {"vocal", kCatVocal, false},
    {"vox", kCatVocal, false},        {"voice", kCatVocal, false},
    {"voices", kCatVocal, false},     {"choir", kCatVocal, false},
    {"flute", kCatWind, false},       {"whistle", kCatWind, false},
    {"theramin", kCatWind, false},    {"theremin", kCatWind, false},
    {"lead", kCatLead, false},        {"leader", kCatLead, false},
    {"sweep", kCatTexture, false},    {"ambience", kCatTexture, false},
    {"ambient", kCatTexture, false},  {"texture", kCatTexture, false},
    {"noise", kCatFX, false},         {"fx", kCatFX, false},
    {"sfx", kCatFX, false},           {"init", kCatInit, false},

    // 3. Synth1 bucket abbreviations: leading word only.
    {"arp", kCatArp, true},  {"bas", kCatBass, true}, {"pad", kCatPad, true},
    {"dub", kCatChord, true}, {"syn", kCatKeys, true}, {"lead", kCatLead, true},
    {"init", kCatInit, true},
};

// ASCII lower, then split on blanks and the punctuation real patch names use
// ("D-50 Brass-String", "JX-3P", "Synth1's"). Digits stay: "303" is a word.
inline std::vector<std::string> CategoryWords(std::string_view text) {
    std::vector<std::string> words;
    std::string current;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') {
            current += char(u - 'A' + 'a');
        } else if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) {
            current += char(u);
        } else if (u >= 0x80) {
            current += char(u);  // never matches a rule, but keeps words apart
        } else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) words.push_back(current);
    return words;
}

// True when the rule's words appear as WHOLE tokens at `at`.
inline bool CategoryPhraseMatches(const std::vector<std::string> &words, size_t at, std::string_view phrase) {
    const std::vector<std::string> needle = CategoryWords(phrase);
    if (needle.empty() || at + needle.size() > words.size()) return false;
    for (size_t i = 0; i < needle.size(); ++i)
        if (words[at + i] != needle[i]) return false;
    return true;
}

inline int CategoryFromWords(const std::vector<std::string> &words) {
    if (words.empty()) return kCatUnclassified;
    // The LAST matching phrase wins: patch names put their head noun at the end
    // ("Analog Strings", "Closed Hat", "Korg M1 Perc Organ"), so a trailing type
    // word is the most specific. Ties (two rules on the same word) fall back to
    // table order. Leading-only bucket prefixes never compete, so they only fire
    // as a fallback for the Synth1 convention ("SYN Sine JP-8" = Keys).
    int best = kCatUnclassified;
    size_t bestAt = 0;
    for (const CategoryRule &rule : kCategoryRules) {
        const size_t last = rule.leadingOnly ? 0 : words.size() - 1;
        for (size_t at = 0; at <= last; ++at) {
            if (!CategoryPhraseMatches(words, at, rule.phrase)) continue;
            if (best == kCatUnclassified || at > bestAt) {
                best = rule.category;
                bestAt = at;
            }
        }
    }
    return best;
}

// Patch name first (it is the one the designer wrote), file name second (some
// libraries only number their files).
inline int DeriveCategory(std::string_view patchName, std::string_view fileName) {
    int id = CategoryFromWords(CategoryWords(patchName));
    if (id != kCatUnclassified) return id;
    return CategoryFromWords(CategoryWords(fileName));
}

// --- SHA-256 ----------------------------------------------------------------

// FIPS 180-4, header-only and portable (~70 lines). Only used to key the
// sidecar; it is not a security boundary, so no constant-time care.
class Sha256 {
  public:
    Sha256() { reset(); }

    void reset() {
        state_[0] = 0x6a09e667u;
        state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u;
        state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu;
        state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu;
        state_[7] = 0x5be0cd19u;
        pending_ = 0;
        total_ = 0;
    }

    void update(const void *data, size_t len) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        total_ += len;
        if (pending_ != 0) {
            const size_t take = std::min(len, size_t(64) - pending_);
            std::memcpy(block_ + pending_, p, take);
            pending_ += take;
            p += take;
            len -= take;
            if (pending_ < 64) return;
            compress(block_);
            pending_ = 0;
        }
        while (len >= 64) {
            compress(p);
            p += 64;
            len -= 64;
        }
        if (len != 0) {
            std::memcpy(block_, p, len);
            pending_ = len;
        }
    }

    void update(std::string_view text) { update(text.data(), text.size()); }

    std::string hex() {
        unsigned char tail[32];
        const std::uint64_t bits = total_ * 8u;
        size_t len = pending_;
        block_[len++] = 0x80;
        if (len > 56) {
            std::memset(block_ + len, 0, 64 - len);
            compress(block_);
            len = 0;
        }
        std::memset(block_ + len, 0, 56 - len);
        for (int i = 0; i < 8; ++i) block_[56 + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        compress(block_);
        for (int i = 0; i < 8; ++i) {
            tail[4 * i + 0] = static_cast<unsigned char>(state_[i] >> 24);
            tail[4 * i + 1] = static_cast<unsigned char>(state_[i] >> 16);
            tail[4 * i + 2] = static_cast<unsigned char>(state_[i] >> 8);
            tail[4 * i + 3] = static_cast<unsigned char>(state_[i]);
        }
        static const char *kHex = "0123456789abcdef";
        std::string out(64, '0');
        for (int i = 0; i < 32; ++i) {
            out[2 * i] = kHex[tail[i] >> 4];
            out[2 * i + 1] = kHex[tail[i] & 15];
        }
        reset();
        return out;
    }

  private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const unsigned char *block) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
            0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
            0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
            0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
            0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
            0xc67178f2u,
        };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block[4 * i]) << 24) | (std::uint32_t(block[4 * i + 1]) << 16) |
                   (std::uint32_t(block[4 * i + 2]) << 8) | std::uint32_t(block[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::uint32_t state_[8];
    unsigned char block_[64];
    size_t pending_;
    std::uint64_t total_;
};

inline std::string Sha256Hex(std::string_view bytes) {
    Sha256 sha;
    sha.update(bytes);
    return sha.hex();
}

// Streams the file (patches are ~700 bytes, but a library is 19,200 of them and
// this runs on the scan worker: no whole-library buffer).
inline bool Sha256File(const std::filesystem::path &path, std::string &hexOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    Sha256 sha;
    char buffer[8192];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        sha.update(buffer, static_cast<size_t>(in.gcount()));
        if (!in) {
            if (!in.eof()) return false;
            break;
        }
    }
    if (in.bad()) return false;
    hexOut = sha.hex();
    return true;
}

// --- tags -------------------------------------------------------------------

inline constexpr size_t kMaxTags = 12;
inline constexpr size_t kMaxTagLength = 24;

// Trim, ASCII-lowercase and collapse blanks so "Dark Pad" and "dark  pad" are
// the same tag. Anything else is dropped by AddTag: a tag must be non-empty,
// <= kMaxTagLength bytes (UTF-8 is fine, we only count bytes) and printable.
inline std::string NormalizeTag(std::string_view tag) {
    std::string out;
    bool space = false;
    for (char c : tag) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == ' ' || u == '\t') {
            space = !out.empty();
            continue;
        }
        if (u < 0x20 || u == 0x7f) continue;
        if (space) out += ' ';
        space = false;
        out += (u >= 'A' && u <= 'Z') ? char(u - 'A' + 'a') : char(u);
    }
    if (out.size() > kMaxTagLength) {
        size_t cut = kMaxTagLength;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80) --cut;
        out.resize(cut);
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out;
}

inline bool ContainsTag(const std::vector<std::string> &tags, const std::string &tag) {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

inline void SortTags(std::vector<std::string> &tags) {
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
}

struct PatchTags {
    int category = kCatUnclassified;
    bool overridden = false;  // the sidecar carried a category (maybe "Unclassified")
    bool favourite = false;
    std::vector<std::string> tags;  // normalised, sorted, unique, <= kMaxTags

    // False only when nothing is stored: a category the user explicitly reset to
    // Unclassified is an override, and must survive (Empty() would erase it and
    // let the name rule put the patch back where the user moved it away from).
    bool Empty() const { return !overridden && !favourite && tags.empty(); }
};

// false = invalid / already there / full.
inline bool AddTag(PatchTags &patch, std::string_view tag) {
    std::string normalised = NormalizeTag(tag);
    if (normalised.empty() || ContainsTag(patch.tags, normalised) || patch.tags.size() >= kMaxTags) return false;
    patch.tags.push_back(std::move(normalised));
    SortTags(patch.tags);
    return true;
}

inline bool RemoveTag(PatchTags &patch, std::string_view tag) {
    const std::string normalised = NormalizeTag(tag);
    const auto it = std::find(patch.tags.begin(), patch.tags.end(), normalised);
    if (normalised.empty() || it == patch.tags.end()) return false;
    patch.tags.erase(it);
    return true;
}

// --- the sidecar ------------------------------------------------------------

// `categories.json` at the library root:
//   { "version": 1,
//     "patches": { "<sha256 hex>": { "name": "PAD Juno Soft", "category": "Pad",
//                                    "favourite": true, "tags": ["warm", "juno"] } } }
// Sparse (only touched patches), sorted keys (stable diffs), "name" is a comment
// hint written for humans and ignored on load.
class TagStore {
  public:
    static constexpr int kVersion = 1;

    void Clear() { patches_.clear(); }

    bool Empty() const { return patches_.empty(); }

    std::size_t Size() const { return patches_.size(); }

    // Missing / unreadable / corrupt = empty store (a broken sidecar must never
    // cost the user their library). A HIGHER version is refused wholesale: a
    // newer build's file must not be silently downgraded by an older one.
    void Load(const std::filesystem::path &root) { LoadFile(root / kFileName); }

    // A tags file anywhere (export / import on a new machine).
    void LoadFile(const std::filesystem::path &file) {
        patches_.clear();
        Decode(ReadFile(file));
    }

    // nullptr = nothing stored.
    const PatchTags *Find(std::string_view hash) const {
        const auto it = patches_.find(std::string(hash));
        return it == patches_.end() ? nullptr : &it->second;
    }

    // An idle patch (Empty()) leaves the map, so the file stays sparse.
    void Set(const std::string &hash, const PatchTags &patch) {
        if (hash.empty()) return;
        if (patch.Empty()) {
            patches_.erase(hash);
            return;
        }
        patches_[hash] = patch;
    }

    // Read-modify-write in one step, so a caller can flip a category or a star
    // without carrying the rest of the patch around. Prunes when it ends idle.
    template <class Fn>
    void Update(const std::string &hash, Fn &&fn) {
        if (hash.empty()) return;
        PatchTags patch;
        if (const PatchTags *found = Find(hash)) patch = *found;
        fn(patch);
        SortTags(patch.tags);
        if (patch.tags.size() > kMaxTags) patch.tags.resize(kMaxTags);
        patch.category = IsCategory(patch.category) ? patch.category : int(kCatUnclassified);
        Set(hash, patch);
    }

    // Comment hint for humans reading the file / a merge conflict; never loaded.
    void SetNameHint(const std::string &hash, std::string name) { hints_[hash] = std::move(name); }

    void ClearNameHints() { hints_.clear(); }

    // Import semantics: MERGE, incoming wins per hash, never a wholesale replace
    // (a sidecar imported from a new machine must not drop tags made here).
    void Merge(const TagStore &incoming) {
        for (const auto &[hash, patch] : incoming.patches_) patches_[hash] = patch;
    }

    // Add-only merge: keeps every record we already have (a file brought from
    // another machine must never overwrite what was tagged here), returns how
    // many records it added.
    std::size_t MergeMissing(const TagStore &incoming) {
        std::size_t added = 0;
        for (const auto &[hash, patch] : incoming.patches_)
            if (patches_.emplace(hash, patch).second) ++added;
        return added;
    }

    // Atomic: temp file + rename (same directory, so it stays on one volume).
    bool Save(const std::filesystem::path &root) const { return SaveFile(root / kFileName); }

    bool SaveFile(const std::filesystem::path &path) const {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        const std::filesystem::path tmp = path.parent_path() / (path.filename().string() + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            const std::string text = Encode();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out) return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }

    // Deterministic: keys sorted, tags sorted, so two machines produce the same
    // bytes for the same data and a diff only shows real edits.
    std::string Encode() const {
        std::vector<const std::pair<const std::string, PatchTags> *> rows;
        rows.reserve(patches_.size());
        for (const auto &entry : patches_) rows.push_back(&entry);
        std::sort(rows.begin(), rows.end(),
                  [](const auto *a, const auto *b) { return a->first < b->first; });

        std::string out = "{\n  \"version\": " + std::to_string(kVersion) + ",\n  \"patches\": {";
        if (rows.empty()) {
            out += "}\n}\n";
            return out;
        }
        out += "\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            const std::string &hash = rows[i]->first;
            const PatchTags &patch = rows[i]->second;
            out += "    \"" + hash + "\": {";
            bool first = true;
            auto key = [&out, &first](const char *name) {
                out += first ? "\n      \"" : ",\n      \"";
                out += name;
                out += "\": ";
                first = false;
            };
            if (const auto hint = hints_.find(hash); hint != hints_.end() && !hint->second.empty()) {
                key("name");
                out += Quote(hint->second);
            }
            if (!patch.tags.empty()) key("tags");
            if (!patch.tags.empty()) {
                out += "[";
                for (size_t t = 0; t < patch.tags.size(); ++t) {
                    if (t != 0) out += ", ";
                    out += Quote(patch.tags[t]);
                }
                out += "]";
            }
            if (patch.favourite) {
                key("favourite");
                out += "true";
            }
            if (patch.overridden) {
                key("category");
                out += Quote(CategoryName(patch.category));
            }
            out += first ? "}" : "\n    }";
            out += i + 1 == rows.size() ? "\n" : ",\n";
        }
        out += "  }\n}\n";
        return out;
    }

    // Tolerant scanner (no JSON dependency, same spirit as DecodeMidiLearn):
    // unknown keys and malformed entries are skipped, never fatal.
    bool Decode(std::string_view text) {
        if (text.empty()) return false;
        int version = kVersion;
        if (const auto found = FindValue(text, "version")) {
            const int parsed = ParseInt(text, *found);
            if (parsed > 0) version = parsed;
        }
        // Written by a newer build: refuse the WHOLE file and leave the store
        // untouched (a downgrade must not drop data it cannot understand).
        if (version > kVersion) return false;
        Clear();

        const auto patchesKey = FindValue(text, "patches");
        if (!patchesKey) return true;  // no patches: a valid, empty store
        size_t pos = text.find('{', *patchesKey);
        if (pos == std::string_view::npos) return true;
        ++pos;

        while (pos < text.size()) {
            pos = SkipSpace(text, pos);
            if (pos >= text.size() || text[pos] == '}') break;
            if (text[pos] == ',') {
                ++pos;
                continue;
            }
            if (text[pos] != '"') break;
            const std::string hash = ReadString(text, pos);
            pos = SkipSpace(text, pos);
            if (hash.empty() || pos >= text.size() || text[pos] != ':') break;
            pos = SkipSpace(text, pos + 1);
            if (pos >= text.size() || text[pos] != '{') break;
            ++pos;

            PatchTags patch;
            while (pos < text.size()) {
                pos = SkipSpace(text, pos);
                if (pos >= text.size() || text[pos] == '}') {
                    ++pos;
                    break;
                }
                if (text[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (text[pos] != '"') break;
                const std::string key = ReadString(text, pos);
                pos = SkipSpace(text, pos);
                if (pos >= text.size() || text[pos] != ':') break;
                pos = SkipSpace(text, pos + 1);

                if (key == "category") {
                    // An unknown name drops the override but keeps the rest.
                    const int id = CategoryId(ReadString(text, pos));
                    if (id >= 0) {
                        patch.category = id;
                        patch.overridden = true;
                    }
                } else if (key == "favourite") {
                    patch.favourite = ParseBool(text, pos);
                SkipValue(text, pos); // ParseBool leaves pos on the literal
                } else if (key == "tags") {
                    if (pos < text.size() && text[pos] == '[') {
                        ++pos;
                        while (pos < text.size()) {
                            pos = SkipSpace(text, pos);
                            if (pos >= text.size() || text[pos] == ']') break;
                            if (text[pos] == ',') {
                                ++pos;
                                continue;
                            }
                            if (text[pos] != '"') break;
                            AddTag(patch, ReadString(text, pos));
                        }
                        if (pos < text.size() && text[pos] == ']') ++pos;
                    }
                } else {
                    SkipValue(text, pos);
                }
            }
            Set(hash, patch);
        }
        return true;
    }

    // Sorted, deduped tags across the whole store (the tag filter chips).
    std::vector<std::string> Vocabulary() const {
        std::vector<std::string> out;
        for (const auto &entry : patches_)
            for (const std::string &tag : entry.second.tags) out.push_back(tag);
        SortTags(out);
        return out;
    }

    static constexpr const char *kFileName = "categories.json";

  private:
    static std::string Quote(std::string_view text) {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            if (static_cast<unsigned char>(c) >= 0x20) out += c;
        }
        out += '"';
        return out;
    }

    static std::string ReadFile(const std::filesystem::path &path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static size_t SkipSpace(std::string_view text, size_t pos) {
        while (pos < text.size()) {
            const char c = text[pos];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++pos;
        }
        return pos;
    }

    // Position just past the closing quote.
    static std::string ReadString(std::string_view text, size_t &pos) {
        std::string out;
        if (pos >= text.size() || text[pos] != '"') return out;
        ++pos;
        while (pos < text.size()) {
            const char c = text[pos++];
            if (c == '"') return out;
            if (c == '\\' && pos < text.size()) {
                const char esc = text[pos++];
                switch (esc) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'u': pos = std::min(pos + 4, text.size()); break;  // not used by us
                    default: out += esc; break;
                }
                continue;
            }
            out += c;
        }
        return out;
    }

    static bool ParseBool(std::string_view text, size_t pos) {
        return text.compare(pos, 4, "true") == 0 || text.compare(pos, 1, "1") == 0;
    }

    static int ParseInt(std::string_view text, size_t pos) {
        pos = SkipSpace(text, pos);
        bool negative = false;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) negative = text[pos++] == '-';
        int value = 0;
        bool any = false;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (value > 1000000) return negative ? -value : value;
            value = value * 10 + (text[pos++] - '0');
            any = true;
        }
        if (!any) return 0;
        return negative ? -value : value;
    }

    // Skip one JSON value so an unknown key cannot desync the scanner.
    static void SkipValue(std::string_view text, size_t &pos) {
        pos = SkipSpace(text, pos);
        if (pos >= text.size()) return;
        const char c = text[pos];
        if (c == '"') {
            ReadString(text, pos);
            return;
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            int depth = 0;
            while (pos < text.size()) {
                const char d = text[pos++];
                if (d == '"') {
                    --pos;
                    ReadString(text, pos);
                    continue;
                }
                if (d == c) ++depth;
                if (d == close && --depth <= 0) return;
            }
            return;
        }
        while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']') ++pos;
    }

    // "key": -> position just past the colon. Only matches a real key, not a
    // string that merely contains the word.
    static std::optional<size_t> FindValue(std::string_view text, std::string_view key) {
        size_t pos = 0;
        while ((pos = text.find('"', pos)) != std::string_view::npos) {
            std::string_view at = text.substr(pos + 1);
            const size_t close = at.find('"');
            if (close == std::string_view::npos) return std::nullopt;
            const bool match = at.substr(0, close) == key;
            pos += close + 2;
            if (!match) continue;
            size_t after = SkipSpace(text, pos);
            if (after >= text.size() || text[after] != ':') continue;
            return SkipSpace(text, after + 1);
        }
        return std::nullopt;
    }

    std::unordered_map<std::string, PatchTags> patches_;
    std::unordered_map<std::string, std::string> hints_;
};

} // namespace oxo::s2
