#include "Synth2Bridge.h"

#define OXO_TRASH_IMPL // oxo::MoveToTrash for this binary (EditorCore/Trash.hpp)
#include "Synth2Instance.hpp"

#include <algorithm>
#include <cstring>
#include <string>

using oxo::Synth2Instance;

namespace {
Synth2Instance *I(void *p) { return static_cast<Synth2Instance *>(p); }
// A patch parameter or the volume (ID 99).
int32_t Clamp(int32_t id) { return id == oxo::s2::kVolumeParamId ? id : std::clamp<int32_t>(id, 0, oxo::s2::kParamCount - 1); }

// One MIDI 1.0 / 2.0 Universal MIDI Packet word stream (AURenderEventMIDIEventList).
void HandleUmp(Synth2Instance *inst, const MIDIEventList &list) {
    const MIDIEventPacket *packet = &list.packet[0];
    for (UInt32 n = 0; n < list.numPackets; ++n) {
        for (UInt32 w = 0; w < packet->wordCount;) {
            const uint32_t word = packet->words[w];
            const uint32_t type = word >> 28;
            if (type == 0x2) { // MIDI 1.0 channel voice
                inst->Midi(uint8_t(word >> 16), uint8_t((word >> 8) & 0x7F), uint8_t(word & 0x7F));
                w += 1;
            } else if (type == 0x4 && w + 1 < packet->wordCount) { // MIDI 2.0 channel voice -> 7 bit
                const uint8_t status = uint8_t(word >> 16), index = uint8_t((word >> 8) & 0x7F);
                const uint32_t data = packet->words[w + 1];
                switch (status & 0xF0) {
                case 0x90:
                case 0x80: inst->Midi(status, index, (status & 0xF0) == 0x90 ? uint8_t(std::max<uint32_t>(1, data >> 25)) : 0); break;
                case 0xB0: inst->Midi(status, index, uint8_t(data >> 25)); break;
                case 0xD0: inst->Midi(status, uint8_t(data >> 25), 0); break;
                case 0xC0: inst->Midi(status, uint8_t((data >> 24) & 0x7F), 0); break; // bank (if flagged) ignored
                case 0xE0: {
                    const uint32_t v14 = data >> 18;
                    inst->Midi(status, uint8_t(v14 & 0x7F), uint8_t(v14 >> 7));
                    break;
                }
                default: break;
                }
                w += 2;
            } else {
                // other message types: 1 word (0, 1, 2), 2 (3, 4), 4 (5, F)...
                w += (type <= 2 || (type >= 6 && type <= 7)) ? 1 : (type <= 4 || (type >= 8 && type <= 0xA)) ? 2 : 4;
            }
        }
        packet = MIDIEventPacketNext(packet);
    }
}
} // namespace

extern "C" {

void *Synth2Create(const char *libraryRoot, const char *factoryRoot, const char *build) {
    return new Synth2Instance(libraryRoot ? libraryRoot : "", factoryRoot ? factoryRoot : "", build ? build : "");
}
void Synth2Destroy(void *instance) { delete I(instance); }
void Synth2SetEditCallback(void *instance, void *ctx, Synth2EditCallback callback) {
    I(instance)->SetEditCallback(ctx, callback);
}
OxoSynth2Hooks Synth2MakeHooks(void *instance) { return I(instance)->MakeHooks(); }

int32_t Synth2ParamCount(void) { return oxo::s2::kVolumeParamId + 1; }
const char *Synth2ParamKey(int32_t id) { return oxo::s2::Info(id).key; }
const char *Synth2ParamName(int32_t id) { return oxo::s2::Info(id).name; }
void Synth2ParamRange(int32_t id, int32_t *min, int32_t *max, int32_t *def) {
    const auto &p = oxo::s2::Info(id);
    *min = p.min, *max = p.max, *def = p.def;
}
int32_t Synth2ParamAutomatable(int32_t id) { return oxo::s2::Info(id).automatable; }
void Synth2FormatParam(void *instance, int32_t id, int32_t value, char *out, int32_t capacity) {
    if (!out || capacity <= 0) return;
    const int sync = oxo::s2::TempoSyncSwitch(Clamp(id));
    const std::string s = oxo::s2::FormatParam(Clamp(id), value, sync >= 0 && I(instance)->NativeValue(sync),
                                               I(instance)->NativeValue(oxo::s2::kEffectType));
    std::strncpy(out, s.c_str(), size_t(capacity - 1));
    out[capacity - 1] = '\0';
}

int32_t Synth2GetParam(void *instance, int32_t id) { return I(instance)->NativeValue(Clamp(id)); }
void Synth2SetParam(void *instance, int32_t id, int32_t value) { I(instance)->SetFromHost(id, value); }

int32_t Synth2EncodeState(void *instance, uint8_t *out, int32_t capacity) {
    const std::vector<uint8_t> blob = I(instance)->EncodeState();
    if (out && capacity > 0) std::memcpy(out, blob.data(), std::min<size_t>(blob.size(), size_t(capacity)));
    return int32_t(blob.size());
}
int32_t Synth2RestoreState(void *instance, const uint8_t *data, int32_t size) {
    return data && size > 0 && I(instance)->RestoreState(data, size_t(size));
}

void Synth2Prepare(void *instance, double sampleRate, uint32_t maxFrames) { I(instance)->Prepare(sampleRate, maxFrames); }

void Synth2Render(void *instance, uint32_t frames, AudioBufferList *output, const AURenderEvent *events,
                  int64_t now, double tempo, double beat, int32_t playing) {
    Synth2Instance *inst = I(instance);
    frames = std::min(frames, inst->ScratchFrames());
    // Host buffers, or ours when the host passes NULL (in-place rendering).
    float *out[2] = {inst->ScratchL(), inst->ScratchR()};
    for (UInt32 b = 0; b < output->mNumberBuffers && b < 2; ++b) {
        if (!output->mBuffers[b].mData) output->mBuffers[b].mData = b ? inst->ScratchR() : inst->ScratchL();
        out[b] = static_cast<float *>(output->mBuffers[b].mData);
        output->mBuffers[b].mDataByteSize = frames * sizeof(float);
    }
    if (output->mNumberBuffers == 1) out[1] = inst->ScratchR();
    inst->SetTransport(tempo, beat, playing != 0);
    inst->ApplyDirty();
    inst->BeginBlock(frames);

    uint32_t pos = 0;
    // note on / off -> the original's 16-sample grid (NoteEventOffset)
    const auto isNote = [](const AURenderEvent *e) {
        uint32_t status = 0;
        if (e->head.eventType == AURenderEventMIDI && e->MIDI.length >= 1) status = e->MIDI.data[0];
        else if (e->head.eventType == AURenderEventMIDIEventList && e->MIDIEventsList.eventList.numPackets &&
                 e->MIDIEventsList.eventList.packet[0].wordCount) {
            const uint32_t w = e->MIDIEventsList.eventList.packet[0].words[0];
            if ((w >> 28) == 2 || (w >> 28) == 4) status = (w >> 16) & 0xFF;
        }
        return (status & 0xF0) == 0x80 || (status & 0xF0) == 0x90;
    };
    for (const AURenderEvent *e = events; e; e = e->head.next) {
        int64_t offset = e->head.eventSampleTime - now;
        if (isNote(e) && offset > 0) offset = oxo::s2::NoteEventOffset(int(std::min<int64_t>(offset, frames)));
        const int64_t at = std::clamp<int64_t>(offset, pos, frames);
        if (uint32_t(at) > pos) {
            inst->Render(out[0] + pos, out[1] + pos, int(at - pos));
            pos = uint32_t(at);
        }
        switch (e->head.eventType) {
        case AURenderEventParameter:
        case AURenderEventParameterRamp: inst->Automate(int(e->parameter.parameterAddress), e->parameter.value); break;
        case AURenderEventMIDI:
            if (e->MIDI.length >= 2) inst->Midi(e->MIDI.data[0], e->MIDI.data[1], e->MIDI.length > 2 ? e->MIDI.data[2] : 0);
            break;
        case AURenderEventMIDIEventList: HandleUmp(inst, e->MIDIEventsList.eventList); break;
        default: break;
        }
    }
    if (pos < frames) inst->Render(out[0] + pos, out[1] + pos, int(frames - pos));
    if (output->mNumberBuffers == 1) // mono host bus: fold
        for (uint32_t i = 0; i < frames; ++i) out[0][i] = 0.5f * (out[0][i] + out[1][i]);
}

} // extern "C"
