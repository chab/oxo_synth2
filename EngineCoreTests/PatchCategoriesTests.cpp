#include "TestFramework.hpp"
#include "../EngineCore/PatchCategories.hpp"
#include <cstdio>
#include <fstream>

using namespace oxo::s2;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char *name) {
    const fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void WriteFile(const fs::path &p, const std::string &text) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}


} // namespace

// The type-only categories: ids, names and name<->id round trip.
OXO_TEST(CategoryNamesAreCompleteAndUnique) {
    OXO_EXPECT_EQ(int(kCategoryCount), int(kCatInit) + 1);
    for (int i = 0; i < int(kCategoryCount); ++i) {
        const char *name = CategoryName(i);
        OXO_EXPECT(name != nullptr && name[0] != '\0');
        OXO_EXPECT_EQ(CategoryId(name), i);
        for (int j = i + 1; j < int(kCategoryCount); ++j)
            OXO_EXPECT(std::string(CategoryName(i)) != std::string(CategoryName(j)));
    }
    OXO_EXPECT(IsCategory(0));
    OXO_EXPECT(IsCategory(int(kCategoryCount) - 1));
    OXO_EXPECT(!IsCategory(-1));
    OXO_EXPECT(!IsCategory(int(kCategoryCount)));
    OXO_EXPECT_EQ(CategoryId("Nope"), -1);
    OXO_EXPECT_EQ(CategoryId("pad"), kCatPad); // case insensitive
    OXO_EXPECT(std::string(CategoryName(-5)) == "Unclassified");
}

// Whole tokens only: abbreviations must not fire inside unrelated words.
OXO_TEST(CategoryMatchesWholeWords) {
    OXO_EXPECT_EQ(DeriveCategory("Obsession", "001.sy1"), kCatUnclassified);
    OXO_EXPECT_EQ(DeriveCategory("What", "001.sy1"), kCatUnclassified);
    OXO_EXPECT_EQ(DeriveCategory("Digital", "001.sy1"), kCatUnclassified);
    OXO_EXPECT_EQ(DeriveCategory("D-50 Brass", "001.sy1"), kCatBrass);
    // Two type words: the trailing one is the head noun ("... Brass-String").
    OXO_EXPECT_EQ(DeriveCategory("D-50 Brass-String", "001.sy1"), kCatStrings);
    OXO_EXPECT_EQ(DeriveCategory("JX-3P", "001.sy1"), kCatUnclassified);
}

// The factory bank's prefixes and the starter bank's instrument words.
OXO_TEST(CategoryDerivesFromShippedNames) {
    struct Case {
        const char *name;
        int want;
    } cases[] = {
        {"PAD Juno Soft", kCatPad},
        {"PAD Basic Strings", kCatStrings},   // trailing type word wins over the bucket
        {"DUB DeepH PW WH", kCatChord},
        {"BAS Solid", kCatBass},
        {"BAS TB-303 Saw", kCatBass},
        {"SYN FM Pluck", kCatPluck},
        {"SYN Juno Res Chords", kCatChord},
        {"SYN H3.1 Stabs", kCatChord},
        {"SYN Sine JP-8", kCatKeys},
        {"DUB Chords BandPass", kCatChord},
        {"ARP SH-101", kCatArp},
        {"ARP 7th WH", kCatArp},
        {"INIT", kCatInit},
        {"Moving Pad", kCatPad},
        {"Lush Pad", kCatPad},
        {"Analog Strings", kCatStrings},
        {"Pizzicato", kCatStrings},
        {"Big Brass", kCatBrass},
        {"Fanfare", kCatBrass},
        {"Korg M1 Perc Organ", kCatOrgan},
        {"DX7 Tubular Bells", kCatBell},
        {"Good Vibes", kCatMallet},
        {"D-50 12 Marimba", kCatMallet},
        {"Theramin", kCatWind}, {"Theremin", kCatWind},
        {"JV2080 Koto", kCatPluck},
        {"Harp", kCatPluck},
        {"Fairlight Vox ?", kCatVocal},
        {"Choir", kCatVocal},
        {"SH101 Lead", kCatLead},
        {"PPG Sweep", kCatTexture},
        {"Ultra Pluck", kCatPluck},
        {"Soundtrack", kCatUnclassified},
        // Percussion phrases come first, so "Bass Drum" is not a bass.
        {"Bass Drum", kCatPerc}, {"Snare Drum", kCatPerc},
        {"Closed Hat", kCatPerc},  {"Open Hi Hat", kCatPerc},
    };
    for (const Case &c : cases) {
        const int got = DeriveCategory(c.name, "001.sy1");
        if (got != c.want) OXO_EXPECT_EQ(std::string(c.name) + " -> " + CategoryName(got), std::string(CategoryName(c.want)));
    }
}

// The patch name wins; the file name is only a fallback (slot numbers ignored).
OXO_TEST(CategoryFallsBackToFileName) {
    OXO_EXPECT_EQ(DeriveCategory("", "012_Lead.sy1"), kCatLead);
    OXO_EXPECT_EQ(DeriveCategory("Soundtrack", "003_Pad.sy1"), kCatPad);
    OXO_EXPECT_EQ(DeriveCategory("", "001.sy1"), kCatUnclassified);
}

OXO_TEST(Sha256MatchesFipsVectors) {
    OXO_EXPECT(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    OXO_EXPECT(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    OXO_EXPECT(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string big;
    for (int i = 0; i < 1000; ++i) big += char(i % 251);
    OXO_EXPECT(Sha256Hex(big) == "4e4c294b331f7a2099a379bec34b9f9fc03dc46ab465d998f4d683da53487e6d");
    // Streaming in awkward chunks must equal the one-shot digest.
    Sha256 sha;
    for (size_t at = 0; at < big.size(); at += 7) sha.update(big.data() + at, std::min<size_t>(7, big.size() - at));
    OXO_EXPECT(sha.hex() == Sha256Hex(big));
}

OXO_TEST(Sha256FileHashesBytes) {
    const fs::path root = FreshDir("oxo_cat_sha");
    const std::string text = std::string(9000, 'x') + "\r\n012:Name";
    WriteFile(root / "001.sy1", text);
    std::string hex;
    OXO_EXPECT(Sha256File(root / "001.sy1", hex));
    OXO_EXPECT(hex == Sha256Hex(text));
    OXO_EXPECT(!Sha256File(root / "missing.sy1", hex));
}

OXO_TEST(TagsAreNormalisedAndBounded) {
    OXO_EXPECT(NormalizeTag("  Dark   Pad ") == "dark pad");
    OXO_EXPECT(NormalizeTag("\t WARM \n") == "warm");
    OXO_EXPECT(NormalizeTag("").empty());
    OXO_EXPECT(NormalizeTag("   ").empty());

    PatchTags patch;
    OXO_EXPECT(AddTag(patch, "Warm"));
    OXO_EXPECT(!AddTag(patch, "warm")); // duplicate after normalisation
    OXO_EXPECT(!AddTag(patch, " "));
    OXO_EXPECT(AddTag(patch, "bright"));
    OXO_EXPECT_EQ(patch.tags.size(), size_t(2));
    OXO_EXPECT(patch.tags[0] == "bright"); // sorted
    OXO_EXPECT(ContainsTag(patch.tags, "warm"));
    OXO_EXPECT(RemoveTag(patch, "WARM"));
    OXO_EXPECT(!ContainsTag(patch.tags, "warm"));
    OXO_EXPECT(!RemoveTag(patch, "warm"));

    for (int i = 0; i < int(kMaxTags) + 4; ++i) AddTag(patch, "t" + std::to_string(i));
    OXO_EXPECT_EQ(patch.tags.size(), kMaxTags);

    OXO_EXPECT(NormalizeTag(std::string(kMaxTagLength + 8, 'a')).size() == kMaxTagLength);
    // A multi-byte character is never cut in half.
    OXO_EXPECT(NormalizeTag(std::string(kMaxTagLength - 1, 'a') + "\xC3\xA9").size() == kMaxTagLength - 1);
}

// A reset to Unclassified still needs an override, so Empty() must ignore it.
OXO_TEST(EmptyKeepsExplicitUnclassified) {
    PatchTags patch;
    OXO_EXPECT(patch.Empty());
    patch.overridden = true;
    OXO_EXPECT(!patch.Empty());
    patch.category = kCatPad;
    OXO_EXPECT(!patch.Empty());

    TagStore store;
    PatchTags pad;
    pad.category = kCatPad;
    pad.overridden = true;
    store.Set("aaaa", pad);
    OXO_EXPECT_EQ(store.Size(), size_t(1));
    store.Update("aaaa", [](PatchTags &p) {
        p.category = kCatUnclassified; // user says: no category
    });
    OXO_EXPECT_EQ(store.Size(), size_t(1)); // override survives, patch stays listed
    const PatchTags *found = store.Find("aaaa");
    OXO_EXPECT(found != nullptr && found->overridden && found->category == kCatUnclassified);

    store.Update("aaaa", [](PatchTags &p) { p.overridden = false; });
    OXO_EXPECT(store.Empty()); // now genuinely idle: pruned
}

OXO_TEST(TagStoreEncodesAndDecodes) {
    TagStore store;
    PatchTags pad;
    pad.category = kCatPad;
    pad.overridden = true;
    pad.favourite = true;
    AddTag(pad, "warm");
    AddTag(pad, "Juno");
    store.Set("bbb", pad);
    PatchTags plain;
    AddTag(plain, "clicky");
    store.Set("aaa", plain);
    store.SetNameHint("aaa", "ARP \"quoted\" name");

    const std::string text = store.Encode();
    TagStore reread;
    OXO_EXPECT(reread.Decode(text));
    OXO_EXPECT_EQ(reread.Size(), size_t(2));
    const PatchTags *pad2 = reread.Find("bbb");
    OXO_EXPECT(pad2 != nullptr);
    if (pad2) {
        OXO_EXPECT_EQ(pad2->category, int(kCatPad));
        OXO_EXPECT(pad2->overridden);
        OXO_EXPECT(pad2->favourite);
        OXO_EXPECT_EQ(pad2->tags.size(), size_t(2));
        OXO_EXPECT(ContainsTag(pad2->tags, "juno"));
    }
    const PatchTags *plain2 = reread.Find("aaa");
    OXO_EXPECT(plain2 != nullptr && !plain2->overridden && !plain2->favourite);
    // The "name" hint is a comment for humans, never reloaded as data.
    OXO_EXPECT_EQ(reread.Size(), size_t(2));

    // Encoding is deterministic, so a diff only shows real edits.
    OXO_EXPECT_EQ(store.Encode(), text);
    OXO_EXPECT(text.find("\"version\": 1") != std::string::npos);
    // Key order must never change the meaning (the "favourite" cursor bug).
    const std::string reordered =
        "{\"version\": 1, \"patches\": {\"ccc\": {\"favourite\": true, \"tags\": "
        "[\"bright\"], \"category\": \"Lead\"}}}";
    TagStore other;
    OXO_EXPECT(other.Decode(reordered));
    const PatchTags *lead = other.Find("ccc");
    OXO_EXPECT(lead != nullptr);
    if (lead) {
        OXO_EXPECT_EQ(lead->category, int(kCatLead));
        OXO_EXPECT(lead->favourite && lead->overridden);
        OXO_EXPECT_EQ(lead->tags.size(), size_t(1));
    }
}

OXO_TEST(TagStoreDecodeIsTolerant) {
    TagStore store;
    PatchTags pad;
    pad.category = kCatPad;
    pad.overridden = true;
    store.Set("keep", pad);
    OXO_EXPECT(!store.Decode("{ \"version\": 2, \"patches\": {} }"));
    OXO_EXPECT(store.Empty() == false); // higher version refused whole, not cleared
    OXO_EXPECT(store.Find("keep") != nullptr);

    OXO_EXPECT(store.Decode("{ \"patches\": { \"h1\": { \"future\": { \"a\": [1, 2] }, \"tags\": [\"Soft\"], "
                           "\"favourite\": true, \"category\": \"Nope\" } } }"));
    const PatchTags *found = store.Find("h1");
    OXO_EXPECT(found != nullptr);
    if (found) {
        OXO_EXPECT(found->favourite);
        OXO_EXPECT(!found->overridden); // unknown category name dropped
        OXO_EXPECT(ContainsTag(found->tags, "soft"));
    }
    OXO_EXPECT(store.Find("keep") == nullptr); // a decoded file replaces content
    OXO_EXPECT(store.Decode("{ \"version\": 1 }"));
    OXO_EXPECT(store.Empty());
}

OXO_TEST(TagStoreSavesAndLoads) {
    const fs::path root = FreshDir("oxo_cat_store");
    TagStore store;
    PatchTags patch;
    patch.favourite = true;
    AddTag(patch, "metallic");
    store.Set("cafe", patch);
    OXO_EXPECT(store.Save(root));
    OXO_EXPECT(fs::exists(root / TagStore::kFileName));
    OXO_EXPECT(!fs::exists(root / (std::string(TagStore::kFileName) + ".tmp")));

    TagStore loaded;
    loaded.Load(root);
    const PatchTags *found = loaded.Find("cafe");
    OXO_EXPECT(found != nullptr && found->favourite && ContainsTag(found->tags, "metallic"));
    OXO_EXPECT(loaded.Vocabulary().size() == 1 && loaded.Vocabulary()[0] == "metallic");

    // A missing sidecar is simply an empty store.
    TagStore fresh;
    fresh.Load(FreshDir("oxo_cat_store_empty"));
    OXO_EXPECT(fresh.Empty());
}

OXO_TEST(TagStoreMergeIncomingWins) {
    TagStore local;
    PatchTags shared;
    AddTag(shared, "local");
    local.Set("h", shared);
    PatchTags localOnly;
    AddTag(localOnly, "keep");
    local.Set("only", localOnly);

    TagStore incoming;
    PatchTags updated;
    AddTag(updated, "incoming");
    updated.favourite = true;
    incoming.Set("h", updated);
    PatchTags brandNew;
    AddTag(brandNew, "new");
    incoming.Set("fresh", brandNew);

    local.Merge(incoming);
    OXO_EXPECT_EQ(local.Size(), size_t(3));
    const PatchTags *h = local.Find("h");
    OXO_EXPECT(h != nullptr && h->favourite && ContainsTag(h->tags, "incoming") && !ContainsTag(h->tags, "local"));
    OXO_EXPECT(local.Find("only") != nullptr);
    OXO_EXPECT(local.Find("fresh") != nullptr);
    // Vocabulary is sorted and deduped across the store.
    const std::vector<std::string> vocab = local.Vocabulary();
    OXO_EXPECT(std::is_sorted(vocab.begin(), vocab.end()));
}

// The sidecar keys on file bytes, so renaming / dragging a patch keeps its tags.
OXO_TEST(TagsSurviveRenameAndMovedLibrary) {
    const fs::path root = FreshDir("oxo_cat_rename");
    const std::string bytes = "001:Warm Pad\r\n0 1 2 3\r\n";
    WriteFile(root / "007.sy1", bytes);

    std::string hash;
    OXO_EXPECT(Sha256File(root / "007.sy1", hash));
    TagStore store;
    PatchTags patch;
    patch.favourite = true;
    AddTag(patch, "keeper");
    store.Set(hash, patch);
    OXO_EXPECT(store.Save(root));

    // Rename to another slot number, then move the whole library somewhere else.
    WriteFile(root / "033.sy1", bytes);
    fs::remove(root / "007.sy1");
    const fs::path moved = FreshDir("oxo_cat_rename_moved");
    fs::copy_file(root / "033.sy1", moved / "033.sy1");
    fs::copy_file(root / TagStore::kFileName, moved / TagStore::kFileName);

    std::string movedHash;
    OXO_EXPECT(Sha256File(moved / "033.sy1", movedHash));
    OXO_EXPECT(movedHash == hash);
    TagStore reloaded;
    reloaded.Load(moved);
    const PatchTags *found = reloaded.Find(movedHash);
    OXO_EXPECT(found != nullptr && found->favourite && ContainsTag(found->tags, "keeper"));
}
