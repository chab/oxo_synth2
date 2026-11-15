#include "TestFramework.hpp"

#include "../EngineCore/InitPatch.hpp"
#include "../EngineCore/ParamFormat.hpp"
#include "../EngineCore/Params.hpp"
#include "../EngineCore/Sy1Codec.hpp"

using namespace oxo::s2;

OXO_TEST(ParamsDefaultsInRange) {
    for (int i = 0; i < kParamCount; ++i) {
        OXO_EXPECT(kParams[i].min <= kParams[i].def);
        OXO_EXPECT(kParams[i].def <= kParams[i].max);
    }
}

// Synth2's INIT (the user's INIT.sy1): every value in range, and it survives
// a .sy1 round trip unchanged.
OXO_TEST(InitPatchInRangeAndRoundTrips) {
    for (int i = 0; i < kParamCount; ++i) OXO_EXPECT_EQ(ClampValue(i, kInitPatchValues[i]), kInitPatchValues[i]);
    const Sy1Patch p = ParseSy1(WriteSy1(InitPatch()));
    OXO_EXPECT(p.values == kInitPatchValues);
    OXO_EXPECT_EQ(p.name, std::string("INIT"));
    OXO_EXPECT_EQ(p.color, std::string("blue"));
}

// The factory bank's 001 is the same INIT (shipped file vs code copy).
OXO_TEST(FactoryInitFileMatchesInitPatch) {
    const std::string path = std::string(OXO_TESTS_DIR) + "/../Shared/Resources/Synth2 Factory/001.sy1";
    std::FILE *f = std::fopen(path.c_str(), "r");
    OXO_EXPECT(f != nullptr);
    if (!f) return;
    std::string text;
    for (int c; (c = std::fgetc(f)) != EOF;) text += char(c);
    std::fclose(f);
    const Sy1Patch p = ParseSy1(text);
    for (int i = 0; i < kParamCount; ++i) OXO_EXPECT_EQ(p.values[i], kInitPatchValues[i]);
    OXO_EXPECT_EQ(p.name, std::string("INIT"));
}

// Chorus type 3 (= 2) is never offered: steps skip it, a loaded 3 acts as 2.
OXO_TEST(OfferedStepSkipsChorusType3) {
    using namespace oxo::s2;
    OXO_EXPECT(!Offered(kChorusType, 3) && Offered(kChorusType, 2) && Offered(kEffectType, 3));
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 2, 1), 4);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 4, -1), 2);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 3, 0), 2);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 3, 1), 4);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 3, -1), 1);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 4, 5), 4);
    OXO_EXPECT_EQ(OfferedStep(kChorusType, 1, -1), 1);
    OXO_EXPECT_EQ(OfferedStep(kOscKeyShift, 10, 3), 13);
    OXO_EXPECT_EQ(OfferedStep(kOscKeyShift, 127, 3), Info(kOscKeyShift).max);
}

OXO_TEST(ParamsNormalizedRoundTrip) {
    for (int i = 0; i < kParamCount; ++i)
        for (int32_t v = kParams[i].min; v <= kParams[i].max; v += std::max(1, (kParams[i].max - kParams[i].min) / 300))
            OXO_EXPECT_EQ(FromNormalized(i, ToNormalized(i, v)), v);
}

OXO_TEST(Sy1ParsesCurrentFormat) {
    Sy1Patch p = ParseSy1("Lead A\r\ncolor=red\r\nver=112\r\n0,3\r\n9,-12\r\n19,127\r\n94,32\r\n21,128\r\n");
    OXO_EXPECT_EQ(p.name, std::string("Lead A"));
    OXO_EXPECT_EQ(p.color, std::string("red"));
    OXO_EXPECT_EQ(p.version, 112);
    OXO_EXPECT(!p.legacy());
    OXO_EXPECT_EQ(p.values[kOsc1Shape], 3);
    OXO_EXPECT_EQ(p.values[kOscKeyShift], -12);
    OXO_EXPECT_EQ(p.values[kFilterFreq], 127);
    OXO_EXPECT_EQ(p.values[kPolyphony], 32);
    OXO_EXPECT_EQ(p.values[kFilterAmount], 127); // out-of-range value clamped
    OXO_EXPECT_EQ(p.values[kOsc2Pitch], kParams[kOsc2Pitch].def); // missing line keeps init
}

OXO_TEST(Sy1LegacyHasNoVersion) {
    Sy1Patch p = ParseSy1("Rydeen melo A1\n0,1\n45,0\n33,64\n");
    OXO_EXPECT(p.legacy());
    OXO_EXPECT_EQ(p.values[kArpBeat], 18);
}

// legacy_fixture.txt (tools/s1ref/check_legacy.py --fixture): every old
// patch of the reference banks + random patches of each converting version,
// with the values the ORIGINAL keeps after loading them.
OXO_TEST(Sy1LegacyConversionMatchesOriginal) {
    const std::string path = std::string(OXO_TESTS_DIR) + "/legacy_fixture.txt";
    std::FILE *f = std::fopen(path.c_str(), "r");
    OXO_EXPECT(f != nullptr);
    if (!f) return;
    int count = 0, ver = 0;
    while (std::fscanf(f, "%d", &ver) == 1) {
        std::string text = "fixture\n" + (ver ? "ver=" + std::to_string(ver) + "\n" : std::string());
        PatchValues expected{};
        for (int i = 0, v = 0; i < kParamCount && std::fscanf(f, "%d", &v) == 1; ++i)
            text += std::to_string(i) + "," + std::to_string(v) + "\n";
        OXO_EXPECT(std::fscanf(f, " |") == 0);
        for (int i = 0; i < kParamCount; ++i) OXO_EXPECT(std::fscanf(f, "%d", &expected[i]) == 1);
        const Sy1Patch p = ParseSy1(text);
        for (int i = 0; i < kParamCount; ++i)
            if (p.values[i] != expected[i]) {
                OXO_EXPECT_EQ(p.values[i], expected[i]);
                std::printf("  patch %d ver %d param %d\n", count, ver, i);
            }
        ++count;
    }
    std::fclose(f);
    OXO_EXPECT(count >= 200);
}

OXO_TEST(Sy1LegacySpotValues) {
    // ver 105, poly: filter amount was unipolar, decay remapped, unison
    // fixed at 2 voices / detune 48, routing reset to the init wheel routing
    Sy1Patch p = ParseSy1("old\nver=105\n21,0\n26,20\n38,0\n44,19\n50,127\n87,10\n93,8\n");
    OXO_EXPECT_EQ(p.values[kFilterAmount], 63);
    OXO_EXPECT_EQ(p.values[kAmpDecay], 8);
    OXO_EXPECT_EQ(p.values[kUnisonVoices], 2);
    OXO_EXPECT_EQ(p.values[kUnisonDetune], 48);
    OXO_EXPECT_EQ(p.values[kWheelSpeedSens], 117);
    OXO_EXPECT_EQ(p.values[kCtrlAssign1], 44);
    // mono: 4 voices / detune 70
    p = ParseSy1("old\nver=106\n38,1\n");
    OXO_EXPECT_EQ(p.values[kUnisonVoices], 4);
    OXO_EXPECT_EQ(p.values[kUnisonDetune], 70);
    // current versions are untouched
    p = ParseSy1("new\nver=109\n21,0\n26,20\n93,8\n");
    OXO_EXPECT_EQ(p.values[kFilterAmount], 0);
    OXO_EXPECT_EQ(p.values[kAmpDecay], 20);
    OXO_EXPECT_EQ(p.values[kUnisonVoices], 8);
    // ctrl assign -1 = none (kept, like the original; was clamped to 0)
    p = ParseSy1("new\nver=112\n89,-1\n");
    OXO_EXPECT_EQ(p.values[kCtrlAssign2], -1);
    OXO_EXPECT(WriteSy1(p).find("\n89,-1\n") != std::string::npos);
    // out-of-range values play as the nearest in-range one in the original
    // (measure_out_of_range.py): lfo dest 0 = dest 1, LFO stays on
    p = ParseSy1("odd\nver=113\n46,0\n58,1\n21,128\n31,0\n57,2\n85,85\n");
    OXO_EXPECT_EQ(p.values[kLfo2Dest], 1);
    OXO_EXPECT_EQ(p.values[kLfo2On], 1);
    OXO_EXPECT_EQ(p.values[kFilterAmount], 127);
    OXO_EXPECT_EQ(p.values[kArpType], 1);
    OXO_EXPECT_EQ(p.values[kLfo1On], 1);
    OXO_EXPECT_EQ(p.values[kUnisonPitch], 48);
}

OXO_TEST(Sy1WriteRoundTrip) {
    Sy1Patch p;
    p.name = "Round";
    p.values[kOscKeyShift] = -24;
    p.values[kCtrlSrc1] = 57344;
    Sy1Patch q = ParseSy1(WriteSy1(p));
    OXO_EXPECT_EQ(q.name, p.name);
    OXO_EXPECT_EQ(q.version, 112);
    OXO_EXPECT(q.values == p.values);
}

OXO_TEST(LfoTempoSyncSteps) {
    // (measured) knob ranges of Synth1's 30 sync steps
    OXO_EXPECT_EQ(Lfo::SyncBeats(0), 64.0);
    OXO_EXPECT_EQ(Lfo::SyncBeats(4), 64.0);
    OXO_EXPECT_EQ(Lfo::SyncBeats(5), 48.0);
    OXO_EXPECT_EQ(Lfo::SyncBeats(63), 2.0);
    OXO_EXPECT_EQ(Lfo::SyncBeats(64), 1.75);
    OXO_EXPECT_EQ(Lfo::SyncBeats(68), 1.75);
    OXO_EXPECT_EQ(Lfo::SyncBeats(123), 1.0 / 12);
    OXO_EXPECT_EQ(Lfo::SyncBeats(127), 1.0 / 24);
    const char *labels[Lfo::kSyncSteps] = {"16 bars", "12 bars", "9 bars", "8 bars", "6 bars", "4.5 bars", "4 bars",
                                           "3 bars", "2.25 bars", "2 bars", "1/1.", "1 bar", "1/2..", "1/2.", "1/2",
                                           "1/4..", "1/4.", "1/2T", "1/4", "1/8..", "1/8.", "1/4T", "1/8", "1/16.",
                                           "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64T"};
    for (int s = 0; s < Lfo::kSyncSteps; ++s)
        OXO_EXPECT_EQ(DivisionLabel(Lfo::kSyncBeats[s]), std::string(labels[s]));
    OXO_EXPECT_EQ(FormatParam(kLfo1Speed, 60, true), std::string("1/2"));
    OXO_EXPECT_EQ(FormatParam(kLfo1Speed, 60), FormatParam(kLfo1Speed, 60, false));
    OXO_EXPECT_EQ(TempoSyncSwitch(kLfo2Speed), int(kLfo2TempoSync));
    // Effect level: phasers = feedback %, others the plain value.
    OXO_EXPECT_EQ(FormatParam(kEffectLevel, 64, false, 6), std::string("0%"));
    OXO_EXPECT_EQ(FormatParam(kEffectLevel, 127, false, 9), std::string("+100%"));
    OXO_EXPECT_EQ(FormatParam(kEffectLevel, 0, false, 7), std::string("-100%"));
    OXO_EXPECT_EQ(FormatParam(kEffectLevel, 96, false, 0), std::string("96"));
    OXO_EXPECT_EQ(std::string(DisplayName(kEffectCtrl1, 3)), std::string("sample rate"));
    OXO_EXPECT_EQ(std::string(DisplayName(kEffectLevel, 8)), std::string("feedback"));
}
