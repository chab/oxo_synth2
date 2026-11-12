// Categories, favourites and tags wired into the library: the scan fills
// PatchRef from the sidecar, editing a slot rewrites the sidecar, and every
// library mutation that changes or removes bytes keeps the two in step.

#include "TestFramework.hpp"
#include "../EngineCore/PatchLibrary.hpp"
#include <fstream>

using namespace oxo::s2;
namespace fs = std::filesystem;

namespace {

void WriteFile(const fs::path &p, const std::string &text) {
    std::ofstream(p, std::ios::binary) << text;
}

// A minimal but complete .sy1 (version 112, so no legacy conversion runs).
std::string PatchText(const std::string &name, int cutoff = 64) {
    std::string t = name + "\ncolor=default\nver=112\n";
    for (int i = 0; i < kParamCount; ++i) t += std::to_string(i) + "," + std::to_string(DefaultValues()[size_t(i)]) + "\n";
    t += "19," + std::to_string(cutoff) + "\n";
    return t;
}

fs::path FreshDir(const char *name) {
    const fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

fs::path TagFile(const fs::path &root) { return root / TagStore::kFileName; }

// A library with two banks: "Alpha" (2 patches) and "Beta" (1 patch).
fs::path TwoBankLibrary(const char *name) {
    const fs::path root = FreshDir(name);
    fs::create_directories(root / "Alpha");
    fs::create_directories(root / "Beta");
    WriteFile(root / "Alpha" / "001.sy1", PatchText("Alpha Pad"));
    WriteFile(root / "Alpha" / "002.sy1", PatchText("Alpha Bass", 40));
    WriteFile(root / "Beta" / "001.sy1", PatchText("Beta Lead"));
    return root;
}

const PatchRef *SlotOf(const PatchManager &mgr, int bank, int slot) {
    const auto &banks = mgr.Library().Banks();
    if (bank < 0 || size_t(bank) >= banks.size()) return nullptr;
    return &banks[size_t(bank)].slots[size_t(slot)];
}

int BankId(const PatchManager &mgr, const std::string &name) {
    const auto &banks = mgr.Library().Banks();
    for (size_t b = 0; b < banks.size(); ++b)
        if (banks[b].name == name) return int(b);
    return -1;
}

} // namespace

// A store with no records must cost nothing: no hashes computed, no file.
OXO_TEST(UntaggedLibraryComputesNoHashes) {
    const fs::path root = TwoBankLibrary("oxo_tags_fastpath");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const PatchRef *ref = SlotOf(mgr, BankId(mgr, "Alpha"), 0);
    OXO_EXPECT(ref != nullptr);
    OXO_EXPECT(ref->hash.empty()); // no store, no hashing
    OXO_EXPECT(ref->category != int(kCatUnclassified)); // derived from the name
    OXO_EXPECT(!ref->favourite);
    OXO_EXPECT(ref->tags.empty());
    OXO_EXPECT(!fs::exists(TagFile(root)));
}

// The name alone decides the category until the user overrides it.
OXO_TEST(ScanDerivesCategoryFromName) {
    const fs::path root = TwoBankLibrary("oxo_tags_derive");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->category, int(kCatPad));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 1)->category, int(kCatBass));
    OXO_EXPECT_EQ(SlotOf(mgr, BankId(mgr, "Beta"), 0)->category, int(kCatLead));
}

// Editors: category, star and tags land in the sidecar and on the ref at once.
OXO_TEST(EditingASlotSavesTheSidecar) {
    const fs::path root = TwoBankLibrary("oxo_tags_edit");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");

    OXO_EXPECT(mgr.SetCategory(alpha, 0, int(kCatTexture)));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->category, int(kCatTexture));
    OXO_EXPECT(mgr.SetFavourite(alpha, 0, true));
    OXO_EXPECT(SlotOf(mgr, alpha, 0)->favourite);
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "warm"));
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "Slow"));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->tags.size(), size_t(2));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->tags[0], std::string("slow")); // normalised + sorted

    OXO_EXPECT(fs::exists(TagFile(root)));
    TagStore reread;
    reread.Load(root);
    OXO_EXPECT_EQ(reread.Size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->hash.size(), size_t(64));
    const PatchTags *rec = reread.Find(SlotOf(mgr, alpha, 0)->hash);
    OXO_EXPECT(rec != nullptr);
    OXO_EXPECT_EQ(rec->category, int(kCatTexture));
    OXO_EXPECT(rec->overridden);
    OXO_EXPECT(rec->favourite);
    OXO_EXPECT_EQ(rec->tags.size(), size_t(2));

    // A duplicate tag and removing one that is not there change nothing.
    OXO_EXPECT(!mgr.AddPatchTag(alpha, 0, "WARM"));
    OXO_EXPECT(!mgr.RemovePatchTag(alpha, 0, "brittle"));
    OXO_EXPECT(mgr.RemovePatchTag(alpha, 0, "slow"));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->tags.size(), size_t(1));

    // Un-starring the last record leaves no file behind.
    OXO_EXPECT(mgr.SetFavourite(alpha, 0, false));
    OXO_EXPECT(mgr.SetCategory(alpha, 0, -1)); // back to the derived category
    OXO_EXPECT(mgr.RemovePatchTag(alpha, 0, "warm"));
    OXO_EXPECT(!fs::exists(TagFile(root)));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->category, int(kCatPad)); // derived again
}

// Reopening the library shows what was tagged last time.
OXO_TEST(TagsSurviveAFreshManager) {
    const fs::path root = TwoBankLibrary("oxo_tags_reload");
    {
        PatchManager mgr;
        mgr.Configure(root);
    mgr.Rescan();
        const int alpha = BankId(mgr, "Alpha");
        OXO_EXPECT(mgr.SetFavourite(alpha, 1, true));
        OXO_EXPECT(mgr.AddPatchTag(alpha, 1, "sub"));
    }
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(SlotOf(mgr, alpha, 1)->favourite);
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 1)->tags.size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 1)->tags[0], std::string("sub"));
    // The other slot was never touched: it keeps its derived state.
    OXO_EXPECT(!SlotOf(mgr, alpha, 0)->favourite);
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->category, int(kCatPad));
}

// A patch that is copied inherits the tags of the bytes it was copied from.
OXO_TEST(CopyCarriesTagsAndStar) {
    const fs::path root = TwoBankLibrary("oxo_tags_copy");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "keeper"));
    OXO_EXPECT(mgr.SetFavourite(alpha, 0, true));

    const int made = mgr.Copy(alpha, 0, alpha); // a second copy inside the bank
    OXO_EXPECT_EQ(made, 2);                     // 0 and 1 are taken
    const PatchRef *dup = SlotOf(mgr, alpha, made);
    OXO_EXPECT(dup != nullptr);
    if (dup == nullptr) return;
    OXO_EXPECT(dup->favourite);
    OXO_EXPECT_EQ(dup->tags.size(), size_t(1));
    OXO_EXPECT_EQ(dup->tags[0], std::string("keeper"));
    OXO_EXPECT_EQ(dup->hash, SlotOf(mgr, alpha, 0)->hash); // same bytes, same hash
}

// Overwriting a slot with different bytes MOVES the star and tags along, and a
// duplicate of the old bytes keeps them.
OXO_TEST(WriteMigratesTagsToTheNewBytes) {
    const fs::path root = TwoBankLibrary("oxo_tags_write");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "moves"));
    OXO_EXPECT(mgr.SetFavourite(alpha, 0, true));
    const std::string oldHash = SlotOf(mgr, alpha, 0)->hash;

    Sy1Patch replacement = ParseSy1(PatchText("Bright Pad", 90));
    OXO_EXPECT(mgr.Write(alpha, 0, replacement));
    OXO_EXPECT(SlotOf(mgr, alpha, 0)->hash != oldHash);
    OXO_EXPECT(SlotOf(mgr, alpha, 0)->favourite);
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 0)->tags.size(), size_t(1));

    TagStore store;
    store.Load(root);
    OXO_EXPECT(store.Find(oldHash) == nullptr); // nothing holds the old bytes now
    OXO_EXPECT(store.Find(SlotOf(mgr, alpha, 0)->hash) != nullptr);
    // Also in the fresh process that matters: the old record is gone from disk.
    TagStore reread;
    reread.Load(root);
    OXO_EXPECT(reread.Find(oldHash) == nullptr);
}

OXO_TEST(WriteKeepsTagsOfAnIdenticalDuplicate) {
    const fs::path root = TwoBankLibrary("oxo_tags_write_dup");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "shared"));
    OXO_EXPECT_EQ(mgr.Copy(alpha, 0, alpha), 2); // same bytes in slot 2 as well
    const std::string hash = SlotOf(mgr, alpha, 0)->hash;

    // Rewriting slot 0 with something else must not drop slot 2's tags.
    OXO_EXPECT(mgr.Write(alpha, 0, ParseSy1(PatchText("Other Pad", 30))));
    TagStore store;
    store.Load(root);
    OXO_EXPECT(store.Find(hash) != nullptr);
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 2)->tags.size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 2)->tags[0], std::string("shared"));
    OXO_EXPECT(!SlotOf(mgr, alpha, 2)->favourite);
}

// Slot moves rename files; bytes never change, so the tags must follow.
OXO_TEST(MoveKeepsTags) {
    const fs::path root = TwoBankLibrary("oxo_tags_move");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(mgr.AddPatchTag(alpha, 0, "traveller"));
    OXO_EXPECT(mgr.SetFavourite(alpha, 0, true));
    const std::string hash = SlotOf(mgr, alpha, 0)->hash;

    OXO_EXPECT(mgr.Move(alpha, 0, 7));
    OXO_EXPECT(SlotOf(mgr, alpha, 7)->favourite);
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 7)->tags.size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(mgr, alpha, 7)->hash, hash);
    OXO_EXPECT(!SlotOf(mgr, alpha, 0)->favourite);
}

// Deleting a bank drops its records: the hashes can never come back, and a
// stale record would give a star to a file someone re-copies later.
OXO_TEST(DeleteBankForgetsItsRecords) {
    const fs::path root = TwoBankLibrary("oxo_tags_delete");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int beta = BankId(mgr, "Beta");
    OXO_EXPECT(mgr.SetFavourite(beta, 0, true));
    OXO_EXPECT_EQ(mgr.Tags().Size(), size_t(1));

    OXO_EXPECT(mgr.DeleteBank(beta));
    OXO_EXPECT_EQ(mgr.Tags().Size(), size_t(0));
    OXO_EXPECT(!fs::exists(TagFile(root)));
}

// Export / import: a tagged library carried to another machine keeps its stars.
OXO_TEST(ExportAndImportMergeTags) {
    const fs::path sourceRoot = TwoBankLibrary("oxo_tags_export_src");
    PatchManager source;
    source.Configure(sourceRoot);
    source.Rescan();
    const int alpha = BankId(source, "Alpha");
    OXO_EXPECT(source.SetFavourite(alpha, 0, true));
    OXO_EXPECT(source.AddPatchTag(alpha, 0, "portable"));

    const fs::path carried = fs::temp_directory_path() / "oxo_tags_carried.json";
    fs::remove(carried);
    OXO_EXPECT(source.ExportTags(carried));
    OXO_EXPECT(fs::exists(carried));

    // The same patches in a new library: the file restores their tags.
    const fs::path destRoot = TwoBankLibrary("oxo_tags_export_dst");
    PatchManager dest;
    dest.Configure(destRoot);
    dest.Rescan();
    const int destAlpha = BankId(dest, "Alpha");
    OXO_EXPECT(!SlotOf(dest, destAlpha, 0)->favourite);
    OXO_EXPECT_EQ(dest.ImportTags(carried), 1);
    OXO_EXPECT(SlotOf(dest, destAlpha, 0)->favourite);
    OXO_EXPECT_EQ(SlotOf(dest, destAlpha, 0)->tags.size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(dest, destAlpha, 0)->tags[0], std::string("portable"));

    // Importing twice adds nothing (and must not undo local edits).
    OXO_EXPECT(dest.RemovePatchTag(destAlpha, 0, "portable"));
    OXO_EXPECT_EQ(dest.ImportTags(carried), 0);
    OXO_EXPECT_EQ(SlotOf(dest, destAlpha, 0)->tags.size(), size_t(0));

    // A file that is not a store is refused, not crash.
    OXO_EXPECT_EQ(dest.ImportTags(carried.parent_path() / "nope.json"), 0);
    fs::remove(carried);
}

// Export bank… carries the bank's records; Import folder… on another machine
// brings them back (that is the whole point of the sidecar).
OXO_TEST(ExportingABankCarriesItsTags) {
    const fs::path sourceRoot = TwoBankLibrary("oxo_tags_bankexport_src");
    PatchManager source;
    source.Configure(sourceRoot);
    source.Rescan();
    const int alpha = BankId(source, "Alpha");
    OXO_EXPECT(source.SetFavourite(alpha, 0, true));
    OXO_EXPECT(source.AddPatchTag(alpha, 0, "travelled"));

    const fs::path drop = FreshDir("oxo_tags_bankexport_drop");
    const std::string exported = source.ExportBank(alpha, drop);
    OXO_EXPECT(!exported.empty());
    OXO_EXPECT(fs::exists(fs::path(exported) / TagStore::kFileName));

    const fs::path destRoot = FreshDir("oxo_tags_bankexport_dst");
    PatchManager dest;
    dest.Configure(destRoot);
    dest.Rescan();
    OXO_EXPECT(!dest.Import(exported).empty());
    const int imported = BankId(dest, "Alpha");
    OXO_EXPECT(imported >= 0);
    if (imported < 0) return;
    OXO_EXPECT(SlotOf(dest, imported, 0)->favourite);
    OXO_EXPECT_EQ(SlotOf(dest, imported, 0)->tags.size(), size_t(1));
    OXO_EXPECT_EQ(SlotOf(dest, imported, 0)->tags[0], std::string("travelled"));
    // A plain folder with no sidecar of its own imports without tags.
    const fs::path plain = FreshDir("oxo_tags_plain_drop");
    fs::create_directories(plain / "Plain");
    WriteFile(plain / "Plain" / "001.sy1", PatchText("Plain Pad"));
    OXO_EXPECT(!dest.Import((plain / "Plain").string()).empty());
}

// The list the UI reads: parallel arrays, same order as "slots".
OXO_TEST(JsonCarriesCategoryFavouriteAndTags) {
    const fs::path root = TwoBankLibrary("oxo_tags_json");
    PatchManager mgr;
    mgr.Configure(root);
    mgr.Rescan();
    const int alpha = BankId(mgr, "Alpha");
    OXO_EXPECT(mgr.SetFavourite(alpha, 1, true));
    OXO_EXPECT(mgr.AddPatchTag(alpha, 1, "growl"));

    const std::string json = mgr.Json();
    OXO_EXPECT(json.find("\"catNames\":[") != std::string::npos);
    OXO_EXPECT(json.find("\"tagVocab\":[\"growl\"]") != std::string::npos);
    OXO_EXPECT(json.find("\"cats\":[") != std::string::npos);
    OXO_EXPECT(json.find("\"favs\":[") != std::string::npos);
    // JSON booleans, not 0/1: the Swift browser decodes these as [Bool],
    // and a number there failed the WHOLE document (an empty-looking
    // library) instead of just the flags.
    OXO_EXPECT(json.find("\"favs\":[false,true") != std::string::npos);
    OXO_EXPECT(json.find("\"tags\":[[") != std::string::npos);
    // The name list is still there, unchanged, so old readers keep working.
    OXO_EXPECT(json.find("\"slots\":[\"Alpha Pad\"") != std::string::npos);
}
