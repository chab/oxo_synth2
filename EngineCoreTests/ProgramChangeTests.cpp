#include "TestFramework.hpp"

#include "../EngineCore/ProgramChange.hpp"
#include "../EngineCore/Synth.hpp"

#include <vector>

using namespace oxo::s2;
namespace fs = std::filesystem;

namespace {
void WriteFile(const fs::path &p, const std::string &text) { std::ofstream(p, std::ios::binary) << text; }

fs::path FreshDir(const char *name) {
    const fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}
} // namespace

// The current bank as the browser shows it: numbered files on their slot,
// the others in the free slots; values clamped; the factory token; a bank
// that is gone = no programs.
OXO_TEST(ProgramBankReadsTheBrowserSlots) {
    const fs::path root = FreshDir("oxo_s2_program_bank_test");
    const fs::path factory = FreshDir("oxo_s2_program_bank_factory");
    fs::create_directories(root / "Sets" / "A");
    WriteFile(root / "Sets" / "A" / "003.sy1", "Third\nver=112\n19,10\n");
    WriteFile(root / "Sets" / "A" / "Named.sy1", "Named\nver=112\n19,999\n");
    WriteFile(root / "Loose.sy1", "Loose\nver=112\n");
    WriteFile(factory / "001.sy1", "INIT\nver=112\n");

    const ProgramBankPatches a = ReadProgramBank(root, factory, "Sets/A");
    OXO_EXPECT(a.present[0] && a.present[2] && !a.present[1] && !a.present[3]);
    OXO_EXPECT(a.patches[0].name == "Named" && a.patches[2].name == "Third");
    OXO_EXPECT_EQ(a.patches[2].values[19], 10);
    OXO_EXPECT_EQ(a.patches[0].values[19], Info(19).max);
    OXO_EXPECT(ProgramLabel(a, 2) == "003:Third" && ProgramLabel(a, 1) == "002:----");
    const ProgramBankValues v = a.Values();
    OXO_EXPECT(v.present[2] && !v.present[1] && v.values[2][19] == 10);

    const ProgramBankPatches r = ReadProgramBank(root, factory, ""); // the root's loose patches
    OXO_EXPECT(r.present[0] && r.patches[0].name == "Loose" && !r.present[1]);
    const ProgramBankPatches f = ReadProgramBank(root, factory, kFactoryProgramBank);
    OXO_EXPECT(f.present[0] && f.patches[0].name == "INIT");
    const ProgramBankPatches gone = ReadProgramBank(root, factory, "Missing");
    OXO_EXPECT(std::none_of(gone.present.begin(), gone.present.end(), [](bool b) { return b; }));
    const ProgramBankPatches noFactory = ReadProgramBank(root, {}, kFactoryProgramBank);
    OXO_EXPECT(!noFactory.present[0]);
}

// The audio side sees the latest object published; older ones are freed by
// the writer, never by the reader.
OXO_TEST(RtMailboxHandsTheLatestObject) {
    RtMailbox<int> box;
    OXO_EXPECT(box.Current() == nullptr);
    box.Publish(std::make_unique<int>(1));
    OXO_EXPECT(box.Current() && *box.Current() == 1);
    box.Publish(std::make_unique<int>(2));
    box.Publish(std::make_unique<int>(3)); // 2 never reached the reader
    OXO_EXPECT(*box.Current() == 3);
    OXO_EXPECT(*box.Current() == 3);
    box.Publish(std::make_unique<int>(4)); // frees 1, which the reader retired
    OXO_EXPECT(*box.Current() == 4);
}

// A program change switches the engine like a browser load (after the
// switch's fade); an empty slot or no bank changes nothing.
OXO_TEST(ProgramChangeSwitchesTheEngine) {
    Synth synth;
    synth.SetSampleRate(48000);
    auto bank = std::make_unique<ProgramBankValues>();
    bank->present[5] = 1;
    bank->values[5] = synth.Values();
    bank->values[5][kFilterFreq] = 17;
    OXO_EXPECT(SwitchProgram(synth, nullptr, 5) == nullptr);
    OXO_EXPECT(SwitchProgram(synth, bank.get(), 4) == nullptr);
    OXO_EXPECT(SwitchProgram(synth, bank.get(), 128) == nullptr);
    OXO_EXPECT(!synth.PatchChanging());
    const PatchValues *v = SwitchProgram(synth, bank.get(), 5);
    OXO_EXPECT(v == &bank->values[5] && synth.PatchChanging());
    std::vector<float> l(48000), r(48000);
    synth.Render(l.data(), r.data(), 48000);
    OXO_EXPECT(!synth.PatchChanging());
    OXO_EXPECT_EQ(synth.Param(kFilterFreq), 17);
}
