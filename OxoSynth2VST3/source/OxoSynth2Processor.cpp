#include "OxoSynth2Processor.h"

#include "OxoSynth2CIDs.h"
#include "EditorCore/State.hpp"
#include "EditorCore/Storage.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;

namespace oxo {

OxoSynth2Processor::OxoSynth2Processor() {
    setControllerClass(kOxoSynth2ControllerUID);
    const s2::MidiLearnTable learned = storage::LoadMidiLearn();
    programChangeOn_ = storage::LoadProgramChange();
    // A fresh instance plays the library's first patch (Synth1: program 001
    // of the first bank); a session's setState replaces it.
    s2::Sy1Patch patch;
    patch.name = name_;
    s2::FirstProgram(storage::LibraryRoot(), storage::FactoryRoot(), patch, program_);
    name_ = patch.name;
    color_ = patch.color;
    for (int i = 0; i < s2::kParamCount; ++i) {
        values_[i] = patch.values[i];
        learnCc_[i] = learned[i];
    }
    pending_ = true;
    inputs_.reserve(4096);
}

tresult PLUGIN_API OxoSynth2Processor::initialize(FUnknown *context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioOutput(STR16("Stereo Out"), Vst::SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 1);
    return kResultOk;
}

tresult PLUGIN_API OxoSynth2Processor::setActive(TBool state) {
    if (!state) synth_.Reset();
    return AudioEffect::setActive(state);
}

// The editor renamed / loaded a patch: keep the name for the state chunk.
tresult PLUGIN_API OxoSynth2Processor::notify(Vst::IMessage *message) {
    if (message && strcmp(message->getMessageID(), "OxoPatchName") == 0) {
        const void *data = nullptr;
        uint32 size = 0;
        if (message->getAttributes() && message->getAttributes()->getBinary("name", data, size) == kResultOk) {
            std::lock_guard<std::mutex> lock(nameMutex_);
            name_.assign(static_cast<const char *>(data), size);
        }
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoAllNotesOff") == 0) {
        panic_ = true; // the audio thread owns synth_
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoPatchLoad") == 0) {
        clearDelay_ = true; // sent before the load's parameter edits
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoProgram") == 0) {
        const void *data = nullptr;
        uint32 size = 0;
        int64 slot = -1;
        if (Vst::IAttributeList *attributes = message->getAttributes()) {
            if (attributes->getBinary("bank", data, size) != kResultOk) size = 0;
            attributes->getInt("slot", slot);
        }
        std::lock_guard<std::mutex> lock(nameMutex_);
        program_ = {size ? std::string(static_cast<const char *>(data), size) : std::string(), int(slot)};
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoTheme") == 0) {
        const void *data = nullptr;
        uint32 size = 0;
        if (!message->getAttributes() || message->getAttributes()->getBinary("theme", data, size) != kResultOk) size = 0;
        std::lock_guard<std::mutex> lock(nameMutex_);
        theme_ = size ? std::string(static_cast<const char *>(data), size) : std::string();
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoProgramBank") == 0) {
        const void *data = nullptr;
        uint32 size = 0;
        if (message->getAttributes() && message->getAttributes()->getBinary("values", data, size) == kResultOk &&
            size == sizeof(s2::ProgramBankValues)) {
            auto bank = std::make_unique<s2::ProgramBankValues>();
            std::memcpy(bank.get(), data, size);
            programs_.Publish(std::move(bank));
        }
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoProgramChangeOn") == 0) {
        int64 on = 0;
        if (message->getAttributes() && message->getAttributes()->getInt("on", on) == kResultOk) programChangeOn_ = on != 0;
        return kResultOk;
    }
    if (message && strcmp(message->getMessageID(), "OxoMidiLearn") == 0) {
        const void *data = nullptr;
        uint32 size = 0;
        if (message->getAttributes() && message->getAttributes()->getBinary("cc", data, size) == kResultOk &&
            size == sizeof(int32_t) * s2::kParamCount) {
            const auto *cc = static_cast<const int32_t *>(data);
            for (int i = 0; i < s2::kParamCount; ++i) learnCc_[i] = cc[i];
        }
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

tresult PLUGIN_API OxoSynth2Processor::getState(IBStream *state) {
    if (!state) return kResultFalse;
    vst3::PluginState st;
    {
        std::lock_guard<std::mutex> lock(nameMutex_);
        st.patch.name = name_;
        st.patch.color = color_;
        st.program = program_;
        st.theme = theme_;
    }
    for (int i = 0; i < s2::kParamCount; ++i) st.patch.values[i] = values_[i].load();
    st.volume = volume_.load();
    std::vector<uint8_t> blob = vst3::EncodeState(st);
    int32 written = 0;
    return state->write(blob.data(), int32(blob.size()), &written);
}

tresult PLUGIN_API OxoSynth2Processor::setState(IBStream *state) {
    if (!state) return kResultFalse;
    std::vector<uint8_t> blob;
    uint8_t chunk[4096];
    for (int32 read = 0; state->read(chunk, sizeof chunk, &read) == kResultOk && read > 0;)
        blob.insert(blob.end(), chunk, chunk + read);
    vst3::PluginState st;
    if (!vst3::DecodeState(blob.data(), blob.size(), st)) return kResultFalse;
    volume_ = st.volume;
    {
        std::lock_guard<std::mutex> lock(nameMutex_);
        name_ = st.patch.name;
        color_ = st.patch.color;
        program_ = st.program;
        theme_ = st.theme;
    }
    for (int i = 0; i < s2::kParamCount; ++i) values_[i] = st.patch.values[i];
    clearDelay_ = true;
    pending_ = true;
    return kResultOk;
}

tresult PLUGIN_API OxoSynth2Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API OxoSynth2Processor::setupProcessing(Vst::ProcessSetup &setup) {
    synth_.SetSampleRate(setup.sampleRate);
    return AudioEffect::setupProcessing(setup);
}

namespace {
void AddOutputPoint(Vst::IParameterChanges *out, Vst::ParamID id, int32 offset, double value) {
    if (!out) return;
    int32 index = 0;
    if (Vst::IParamValueQueue *queue = out->addParameterData(id, index)) queue->addPoint(offset, value, index);
}
} // namespace

void OxoSynth2Processor::applyParameter(Vst::ParamID id, double normalized, int32 offset,
                                        Vst::IParameterChanges *out) {
    if (id < s2::kParamCount) {
        const int32_t v = s2::FromNormalized(int(id), normalized);
        synth_.SetParam(int(id), v);
        values_[id] = v;
    } else if (id == Vst::ParamID(s2::kVolumeParamId)) {
        volume_ = s2::FromNormalized(s2::kVolumeParamId, normalized);
        synth_.SetVolume(volume_.load());
    } else if (id == kPitchBendParamId) {
        synth_.PitchBend(std::clamp(int(std::lround(normalized * 16383.0)), 0, 16383));
    } else if (id == kProgramParamId) {
        if (programChangeOn_.load(std::memory_order_relaxed))
            applyProgram(std::clamp(int(std::lround(normalized * 127.0)), 0, 127), offset, out);
    } else if (id == kAfterTouchParamId) {
        synth_.ChannelPressure(std::clamp(int(std::lround(normalized * 127.0)), 0, 127));
    } else if (id >= kCcParamIdBase && id < kCcParamIdBase + 128) {
        const int cc = int(id - kCcParamIdBase);
        const int v = std::clamp(int(std::lround(normalized * 127.0)), 0, 127);
        synth_.ControlChange(cc, v);
        if (cc == 0) return;
        for (int p = 0; p < s2::kParamCount; ++p) {
            if (learnCc_[p].load(std::memory_order_relaxed) != cc) continue;
            const int32_t native = s2::ScaledValueForCC(p, v);
            if (native == values_[p].load(std::memory_order_relaxed)) continue;
            synth_.SetParam(p, native);
            values_[p] = native;
            AddOutputPoint(out, Vst::ParamID(p), offset, s2::ToNormalized(p, native));
        }
        ccCounter_ = (ccCounter_ + 1) & 0x1FFF;
        AddOutputPoint(out, kLastCcParamId, offset, double((ccCounter_ << 7) | uint32_t(cc)) / kLastCcTokenScale);
    }
}

// A MIDI program change: the patch of that slot of the current bank (an
// empty slot = nothing); the host / controller hear of every changed value
// and of the slot (token).
void OxoSynth2Processor::applyProgram(int program, int32 offset, Vst::IParameterChanges *out) {
    const s2::PatchValues *v = s2::SwitchProgram(synth_, programs_.Current(), program);
    if (!v) return;
    for (int p = 0; p < s2::kParamCount; ++p) {
        const int32_t native = (*v)[size_t(p)];
        if (native == values_[size_t(p)].exchange(native, std::memory_order_relaxed)) continue;
        AddOutputPoint(out, Vst::ParamID(p), offset, s2::ToNormalized(p, native));
    }
    programCounter_ = (programCounter_ + 1) & 0x1FFF;
    AddOutputPoint(out, kProgramTokenParamId, offset, double((programCounter_ << 7) | uint32_t(program)) / kProgramTokenScale);
}

tresult PLUGIN_API OxoSynth2Processor::process(Vst::ProcessData &data) {
    using Point = TimedInput;
    if (panic_.exchange(false)) synth_.Panic();
    if (clearDelay_.exchange(false)) { // a patch load, before its parameter edits
        synth_.ClearDelay();
        synth_.BeginPatchChange();
    }
    if (pending_.exchange(false)) {
        for (int i = 0; i < s2::kParamCount; ++i) synth_.SetParam(i, values_[i].load());
        synth_.SetVolume(volume_.load());
    }
    if (const Vst::ProcessContext *ctx = data.processContext) {
        const bool playing = (ctx->state & Vst::ProcessContext::kPlaying) != 0;
        synth_.SetTransport((ctx->state & Vst::ProcessContext::kTempoValid) ? ctx->tempo : 120.0,
                            (ctx->state & Vst::ProcessContext::kProjectTimeMusicValid) ? ctx->projectTimeMusic : 0.0,
                            playing);
    }

    // Sample-accurate: parameter points and MIDI events merged by offset.
    std::vector<Point> &points = inputs_;
    points.clear();
    if (Vst::IParameterChanges *changes = data.inputParameterChanges) {
        for (int32 i = 0; i < changes->getParameterCount(); ++i) {
            Vst::IParamValueQueue *queue = changes->getParameterData(i);
            if (!queue) continue;
            for (int32 p = 0; p < queue->getPointCount(); ++p) {
                int32 offset = 0;
                Vst::ParamValue value = 0;
                if (queue->getPoint(p, offset, value) == kResultOk)
                    points.push_back({offset, 0, queue->getParameterId(), value, {}});
            }
        }
    }
    if (Vst::IEventList *events = data.inputEvents) {
        for (int32 i = 0; i < events->getEventCount(); ++i) {
            Vst::Event e{};
            if (events->getEvent(i, e) == kResultOk) points.push_back({e.sampleOffset, 1, 0, 0, e});
        }
    }
    std::stable_sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.offset < b.offset; });

    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2) {
        for (const Point &p : points)
            if (p.kind == 0) applyParameter(p.id, p.value, p.offset, data.outputParameterChanges);
        return kResultOk;
    }
    synth_.BeginBlock(data.numSamples);
    float *left = data.outputs[0].channelBuffers32[0];
    float *right = data.outputs[0].channelBuffers32[1];
    int32 pos = 0;
    for (const Point &p : points) {
        const bool note = p.kind == 1 && (p.event.type == Vst::Event::kNoteOnEvent || p.event.type == Vst::Event::kNoteOffEvent);
        const int32 at = std::clamp(note ? int32(s2::NoteEventOffset(p.offset)) : p.offset, pos, data.numSamples);
        if (at > pos) {
            synth_.Render(left + pos, right + pos, at - pos);
            pos = at;
        }
        if (p.kind == 0) {
            applyParameter(p.id, p.value, at, data.outputParameterChanges);
        } else if (p.event.type == Vst::Event::kNoteOnEvent) {
            synth_.NoteOn(p.event.noteOn.pitch, std::clamp(int(std::lround(p.event.noteOn.velocity * 127.f)), 1, 127));
        } else if (p.event.type == Vst::Event::kNoteOffEvent) {
            synth_.NoteOff(p.event.noteOff.pitch);
        }
    }
    if (pos < data.numSamples) synth_.Render(left + pos, right + pos, data.numSamples - pos);
    const uint64_t voices = synth_.VoiceStates();
    for (int w = 0; w < kVoiceStateParamCount; ++w) {
        const uint64_t word = voices >> (16 * w) & 0xFFFF;
        if (word != (sentVoices_ >> (16 * w) & 0xFFFF))
            AddOutputPoint(data.outputParameterChanges, kVoiceStateParamIdBase + Vst::ParamID(w), 0,
                           double(word) / kVoiceStateScale);
    }
    sentVoices_ = voices;
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

} // namespace oxo
