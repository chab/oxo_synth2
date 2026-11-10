#include "OxoSynth2Controller.h"

#include "EngineCore/ParamFormat.hpp"
#include "EngineCore/Params.hpp"
#include "OxoSynth2CIDs.h"
#include "EditorCore/State.hpp"
#include "EditorCore/Storage.hpp"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#if SMTG_OS_MACOS
#include "OxoSynth2EditorView.h"
#elif SMTG_OS_WINDOWS
#include "WindowsEditor.hpp"
#endif

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Steinberg;
namespace fs = std::filesystem;

namespace oxo {

namespace {
// UTF-8 -> a host string, cut to fit.
void ToString128(const std::string &utf8, Vst::String128 out) {
    const std::u16string s = Vst::StringConvert::convert(utf8);
    const size_t n = std::min<size_t>(s.size(), 127);
    std::copy_n(s.begin(), n, out);
    out[n] = 0;
}
} // namespace

tresult PLUGIN_API OxoSynth2Controller::initialize(FUnknown *context) {
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) return result;

    for (int i = 0; i < s2::kParamCount; ++i) {
        const s2::ParamInfo &p = s2::kParams[i];
        Vst::String128 title;
        UString(title, str16BufferSize(Vst::String128)).fromAscii(p.name);
        int32 flags = p.automatable ? Vst::ParameterInfo::kCanAutomate : Vst::ParameterInfo::kIsReadOnly;
        if (p.max - p.min > 4096) flags |= Vst::ParameterInfo::kIsHidden;
        auto *param = new Vst::RangeParameter(title, Vst::ParamID(i), nullptr, p.min, p.max, p.def, p.max - p.min, flags);
        param->setPrecision(0);
        parameters.addParameter(param);
    }

    {
        const s2::ParamInfo &p = s2::kVolumeParam;
        auto *param = new Vst::RangeParameter(STR16("Volume"), Vst::ParamID(s2::kVolumeParamId), nullptr, p.min, p.max,
                                              p.def, p.max - p.min, Vst::ParameterInfo::kCanAutomate);
        param->setPrecision(0);
        parameters.addParameter(param);
    }

    auto addProxy = [this](Vst::ParamID id, const char *name, double def) {
        Vst::ParameterInfo info{};
        info.id = id;
        UString(info.title, str16BufferSize(Vst::String128)).fromAscii(name);
        info.defaultNormalizedValue = def;
        info.flags = Vst::ParameterInfo::kIsHidden | Vst::ParameterInfo::kCanAutomate;
        parameters.addParameter(info);
    };
    addProxy(kPitchBendParamId, "Pitch Bend", 0.5);
    addProxy(kAfterTouchParamId, "Channel Pressure", 0.0);
    for (int cc = 0; cc < 128; ++cc) {
        char name[16];
        snprintf(name, sizeof name, "CC %d", cc);
        addProxy(kCcParamIdBase + cc, name, 0.0);
    }
    Vst::ParameterInfo token{};
    token.id = kLastCcParamId;
    UString(token.title, str16BufferSize(Vst::String128)).fromAscii("Last CC");
    token.flags = Vst::ParameterInfo::kIsHidden | Vst::ParameterInfo::kIsReadOnly;
    parameters.addParameter(token);
    for (int w = 0; w < kVoiceStateParamCount; ++w) {
        char name[16];
        snprintf(name, sizeof name, "Voices %d", w + 1);
        token.id = kVoiceStateParamIdBase + Vst::ParamID(w);
        UString(token.title, str16BufferSize(Vst::String128)).fromAscii(name);
        parameters.addParameter(token);
    }
    token.id = kProgramTokenParamId;
    UString(token.title, str16BufferSize(Vst::String128)).fromAscii("Program Changed");
    parameters.addParameter(token);
    // MIDI program change (see OxoSynth2CIDs.h): names from ProgramBankChanged.
    auto *programs = new Vst::ProgramList(STR16("Program"), kProgramParamId, Vst::kRootUnitId);
    const s2::ProgramBankPatches empty;
    for (int s = 0; s < s2::kBankSlots; ++s) {
        Vst::String128 name;
        ToString128(s2::ProgramLabel(empty, s), name);
        programs->addProgram(name);
    }
    addProgramList(programs);
    Vst::Parameter *program = programs->getParameter();
    program->getInfo().flags &= ~Vst::ParameterInfo::kCanAutomate; // like the SDK's programchange sample
    parameters.addParameter(program);
    Vst::UnitInfo unit{};
    unit.id = Vst::kRootUnitId;
    unit.parentUnitId = Vst::kNoParentUnitId;
    unit.programListId = kProgramParamId;
    UString(unit.name, str16BufferSize(Vst::String128)).fromAscii("Root");
    addUnit(new Vst::Unit(unit));
    learnCc_ = storage::LoadMidiLearn();
    programChangeOn_ = storage::LoadProgramChange(); // the processor reads it too
    ConfigureLibrary(storage::LibraryRoot(), storage::FactoryRoot(), OXO_BUILD_NUMBER);
    // A fresh instance starts on the library's first patch, like the
    // processor (hosts usually sync us with its state right after anyway).
    s2::Sy1Patch patch;
    s2::LibraryProgram where;
    if (StartupProgram(patch, where)) {
        for (int i = 0; i < s2::kParamCount; ++i) setParamNormalized(Vst::ParamID(i), s2::ToNormalized(i, patch.values[i]));
        RestoredPatch(patch.name, patch.color, where);
    }
    return kResultOk;
}

tresult PLUGIN_API OxoSynth2Controller::setComponentState(IBStream *state) {
    if (!state) return kResultFalse;
    std::vector<uint8_t> blob;
    uint8_t chunk[4096];
    for (int32 read = 0; state->read(chunk, sizeof chunk, &read) == kResultOk && read > 0;)
        blob.insert(blob.end(), chunk, chunk + read);
    vst3::PluginState st;
    if (!vst3::DecodeState(blob.data(), blob.size(), st)) return kResultFalse;
    const s2::Sy1Patch &patch = st.patch;
    for (int i = 0; i < s2::kParamCount; ++i) setParamNormalized(Vst::ParamID(i), s2::ToNormalized(i, patch.values[i]));
    setParamNormalized(Vst::ParamID(s2::kVolumeParamId), s2::ToNormalized(s2::kVolumeParamId, st.volume));
    RestoredTheme(st.theme);
    RestoredPatch(patch.name, patch.color, st.program);
    return kResultOk;
}

IPlugView *PLUGIN_API OxoSynth2Controller::createView(FIDString name) {
#if SMTG_OS_MACOS
    if (name && strcmp(name, Vst::ViewType::kEditor) == 0) return new OxoSynth2EditorView(this);
#elif SMTG_OS_WINDOWS
    if (name && strcmp(name, Vst::ViewType::kEditor) == 0) return new WindowsEditor(this);
#else
    (void)name;
#endif
    return nullptr;
}

tresult PLUGIN_API OxoSynth2Controller::getMidiControllerAssignment(int32 busIndex, int16, Vst::CtrlNumber number,
                                                                     Vst::ParamID &id) {
    if (busIndex != 0) return kResultFalse;
    if (number == Vst::kPitchBend) {
        id = kPitchBendParamId;
        return kResultTrue;
    }
    if (number == Vst::kAfterTouch) {
        id = kAfterTouchParamId;
        return kResultTrue;
    }
    if (number >= 0 && number < 128) {
        id = kCcParamIdBase + Vst::ParamID(number);
        return kResultTrue;
    }
    return kResultFalse;
}

// Program changes on every channel of the MIDI input (omni, like the notes)
// go to the root unit's program parameter.
tresult PLUGIN_API OxoSynth2Controller::getUnitByBus(Vst::MediaType type, Vst::BusDirection dir, int32 busIndex,
                                                      int32 channel, Vst::UnitID &unitId) {
    if (type != Vst::MediaTypes::kEvent || dir != Vst::BusDirections::kInput || busIndex != 0 || channel < 0 ||
        channel > 15)
        return kResultFalse;
    unitId = Vst::kRootUnitId;
    return kResultTrue;
}

// The processor can receive messages from now on: hand it the current bank.
tresult PLUGIN_API OxoSynth2Controller::connect(Vst::IConnectionPoint *other) {
    const tresult result = EditControllerEx1::connect(other);
    if (result == kResultOk) ResendProgramBank();
    return result;
}

// The processor switched program (output token): follow it.
tresult PLUGIN_API OxoSynth2Controller::setParamNormalized(Vst::ParamID tag, Vst::ParamValue value) {
    const bool switched = tag == kProgramTokenParamId && value != getParamNormalized(tag);
    const tresult result = EditControllerEx1::setParamNormalized(tag, value);
    if (switched) AdoptProgramChange(int(std::lround(value * kProgramTokenScale)) & 127);
    return result;
}

// ---- SynthEditorCore ----------------------------------------------------------

int32_t OxoSynth2Controller::NativeValue(int id) {
    if (id < 0 || id > s2::kVolumeParamId) return 0;
    return s2::FromNormalized(id, getParamNormalized(Vst::ParamID(id)));
}

void OxoSynth2Controller::PerformEdit(int id, int32_t value) {
    if (id < 0 || id > s2::kVolumeParamId) return;
    const double n = s2::ToNormalized(id, s2::ClampValue(id, value));
    setParamNormalized(Vst::ParamID(id), n);
    performEdit(Vst::ParamID(id), n);
}

void OxoSynth2Controller::PatchNameChanged(const std::string &name) {
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoPatchName");
        if (Vst::IAttributeList *attributes = message->getAttributes())
            attributes->setBinary("name", name.data(), uint32(name.size()));
        sendMessage(message);
        message->release();
    }
}

void OxoSynth2Controller::PatchLoading() {
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoPatchLoad");
        sendMessage(message);
        message->release();
    }
}

void OxoSynth2Controller::AllNotesOff() {
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoAllNotesOff");
        sendMessage(message);
        message->release();
    }
}

void OxoSynth2Controller::ProgramChanged(const s2::LibraryProgram &program) {
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoProgram");
        if (Vst::IAttributeList *attributes = message->getAttributes()) {
            attributes->setBinary("bank", program.bank.data(), uint32(program.bank.size()));
            attributes->setInt("slot", program.slot);
        }
        sendMessage(message);
        message->release();
    }
    // The program parameter shows the slot (the bank becomes the current one).
    if (program.slot >= 0) EditControllerEx1::setParamNormalized(kProgramParamId, program.slot / 127.0);
}

bool OxoSynth2Controller::ProgramBankChanged(const s2::ProgramBankValues &values) {
    if (Vst::ProgramList *list = getProgramList(kProgramParamId)) {
        for (int s = 0; s < s2::kBankSlots; ++s) {
            Vst::String128 name;
            ToString128(s2::ProgramLabel(CurrentProgramBank(), s), name);
            list->setProgramName(s, name);
        }
        notifyProgramListChange(kProgramParamId);
    }
    bool sent = false;
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoProgramBank");
        if (Vst::IAttributeList *attributes = message->getAttributes())
            attributes->setBinary("values", &values, uint32(sizeof values));
        sent = sendMessage(message) == kResultOk;
        message->release();
    }
    return sent;
}

void OxoSynth2Controller::ThemeChanged(const std::string &theme) {
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoTheme");
        if (Vst::IAttributeList *attributes = message->getAttributes())
            attributes->setBinary("theme", theme.data(), uint32(theme.size()));
        sendMessage(message);
        message->release();
    }
}

void OxoSynth2Controller::SetMidiCC(int id, int32_t cc) {
    if (!s2::IsLearnable(id) || cc < 0 || cc > 127 || learnCc_[size_t(id)] == cc) return;
    learnCc_[size_t(id)] = cc;
    storage::SaveMidiLearn(learnCc_);
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoMidiLearn");
        int32_t table[s2::kParamCount];
        for (int i = 0; i < s2::kParamCount; ++i) table[i] = learnCc_[size_t(i)];
        if (Vst::IAttributeList *attributes = message->getAttributes())
            attributes->setBinary("cc", table, uint32(sizeof table));
        sendMessage(message);
        message->release();
    }
}

void OxoSynth2Controller::SetProgramChangeOn(bool on) {
    if (on == programChangeOn_) return;
    programChangeOn_ = on;
    storage::SaveProgramChange(on);
    if (Vst::IMessage *message = allocateMessage()) {
        message->setMessageID("OxoProgramChangeOn");
        if (Vst::IAttributeList *attributes = message->getAttributes()) attributes->setInt("on", on ? 1 : 0);
        sendMessage(message);
        message->release();
    }
}

int32_t OxoSynth2Controller::LastCC() {
    return int32_t(std::lround(getParamNormalized(kLastCcParamId) * kLastCcTokenScale));
}

uint64_t OxoSynth2Controller::VoiceStates() {
    uint64_t s = 0;
    for (int w = 0; w < kVoiceStateParamCount; ++w)
        s |= uint64_t(std::lround(getParamNormalized(kVoiceStateParamIdBase + Vst::ParamID(w)) * kVoiceStateScale))
             << (16 * w);
    return s;
}

} // namespace oxo
