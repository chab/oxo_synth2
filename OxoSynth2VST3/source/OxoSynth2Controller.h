#pragma once

#include "EditorCore/SynthEditorCore.hpp"
#include "EngineCore/MidiLearn.hpp"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <string>

namespace oxo {

// The 99 Synth1 parameters (ids = Synth1 indices) + hidden MIDI proxies; the
// editor services (patch files, bank library, hooks) come from
// SynthEditorCore.
class OxoSynth2Controller : public Steinberg::Vst::EditControllerEx1,
                            public Steinberg::Vst::IMidiMapping,
                            public SynthEditorCore {
public:
    static Steinberg::FUnknown *createInstance(void *) {
        return (Steinberg::Vst::IEditController *)new OxoSynth2Controller();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state) SMTG_OVERRIDE;
    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID &id) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint *other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getUnitByBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
                                               Steinberg::int32 busIndex, Steinberg::int32 channel,
                                               Steinberg::Vst::UnitID &unitId) SMTG_OVERRIDE;

    // ---- SynthEditorCore ----
    int32_t NativeValue(int id) override;
    void BeginEdit(int id) override { beginEdit(Steinberg::Vst::ParamID(id)); }
    void PerformEdit(int id, int32_t value) override;
    void EndEdit(int id) override { endEdit(Steinberg::Vst::ParamID(id)); }
    void PatchLoading() override;
    void GroupEdit(bool begin) override { begin ? startGroupEdit() : finishGroupEdit(); }
    void PatchNameChanged(const std::string &name) override;
    void ProgramChanged(const s2::LibraryProgram &program) override;
    void ThemeChanged(const std::string &theme) override;
    int32_t MidiCC(int id) override { return id >= 0 && id < s2::kParamCount ? learnCc_[size_t(id)] : 0; }
    void SetMidiCC(int id, int32_t cc) override;
    int32_t LastCC() override;
    uint64_t VoiceStates() override;
    void AllNotesOff() override;
    bool ProgramBankChanged(const s2::ProgramBankValues &values) override;
    bool ProgramChangeOn() override { return programChangeOn_; }
    void SetProgramChangeOn(bool on) override;

    OBJ_METHODS(OxoSynth2Controller, EditControllerEx1)
    DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)

private:
    s2::MidiLearnTable learnCc_{};
    bool programChangeOn_ = false; // menu > Program change (per-user preference)
};

} // namespace oxo
