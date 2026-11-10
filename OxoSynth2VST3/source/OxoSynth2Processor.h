#pragma once

#include "EngineCore/PatchLibrary.hpp"
#include "EngineCore/ProgramChange.hpp"
#include "EngineCore/Sy1Codec.hpp"
#include "EngineCore/Synth.hpp"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace oxo {

// Instrument: MIDI event in, stereo out. Parameters 0..98 drive the engine's
// native values; the hidden proxies carry CC / pitch bend.
class OxoSynth2Processor : public Steinberg::Vst::AudioEffect {
public:
    OxoSynth2Processor();

    static Steinberg::FUnknown *createInstance(void *) {
        return (Steinberg::Vst::IAudioProcessor *)new OxoSynth2Processor();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) SMTG_OVERRIDE;

private:
    struct TimedInput {
        Steinberg::int32 offset;
        int kind; // 0 = parameter, 1 = event
        Steinberg::Vst::ParamID id;
        double value;
        Steinberg::Vst::Event event;
    };

    void applyParameter(Steinberg::Vst::ParamID id, double normalized, Steinberg::int32 offset,
                        Steinberg::Vst::IParameterChanges *out);
    void applyProgram(int program, Steinberg::int32 offset, Steinberg::Vst::IParameterChanges *out);

    std::vector<TimedInput> inputs_; // reserved once, reused per block
    // MIDI learn table (param -> CC, 0 = none), plugin-global: loaded from
    // disk, updated by the controller's "OxoMidiLearn" message. A learned CC
    // drives the parameter here and reports it as an output parameter
    // change so the controller / host follow.
    std::array<std::atomic<int>, s2::kParamCount> learnCc_;
    uint32_t ccCounter_ = 0;
    // MIDI program change: the current bank's values ("OxoProgramBank" from
    // the controller); a switch reports its slot on kProgramTokenParamId.
    s2::ProgramMailbox programs_;
    uint32_t programCounter_ = 0;
    std::atomic<bool> programChangeOn_{false}; // menu > Program change ("OxoProgramChangeOn", settings.json)
    uint64_t sentVoices_ = 0; // last voice activity reported (kVoiceStateParamIdBase)
    s2::Synth synth_;
    // Lock-free mirror of the patch for getState/setState (UI thread); the
    // audio thread owns synth_ and picks a restored patch up via pending_.
    std::array<std::atomic<int32_t>, s2::kParamCount> values_;
    std::atomic<int32_t> volume_{s2::kVolumeParam.def}; // master volume, state chunk only (not the patch)
    std::atomic<bool> pending_{false};
    std::atomic<bool> panic_{false}; // menu > All notes off (controller message), done in process()
    std::atomic<bool> clearDelay_{false}; // a patch load ("OxoPatchLoad", setState), done in process()
    std::mutex nameMutex_;
    std::string name_ = "Init";
    std::string color_ = "default";
    s2::LibraryProgram program_; // the LCD's library program, kept in the state chunk (nameMutex_)
    std::string theme_;          // the editor's palette of this instance, ditto
};

} // namespace oxo
