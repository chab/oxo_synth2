#pragma once

#include "OxoSynth2Identity.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace oxo {

// Class IDs from Identity.xcconfig (fresh 2026-10-27). Never reuse another
// product's class IDs — hosts resolve duplicates unpredictably.
static const Steinberg::FUID kOxoSynth2ProcessorUID(OXO_VST3_PROCESSOR_UID);
static const Steinberg::FUID kOxoSynth2ControllerUID(OXO_VST3_CONTROLLER_UID);

// Parameter IDs 0..98 = the Synth1 parameter index (oxo::s2::ParamId).
// Hidden MIDI proxies (IMidiMapping) so hosts deliver CC / pitch bend:
inline constexpr Steinberg::Vst::ParamID kPitchBendParamId = 1000;
inline constexpr Steinberg::Vst::ParamID kAfterTouchParamId = 1001; // channel pressure
inline constexpr Steinberg::Vst::ParamID kCcParamIdBase = 1100; // + CC number 0..127
// Hidden read-only OUTPUT parameter: "a CC arrived" token for MIDI learn,
// (counter << 7 | cc) / kLastCcTokenScale (latest wins; the counter makes a
// repeat of the same CC a new token).
inline constexpr Steinberg::Vst::ParamID kLastCcParamId = 1300;
inline constexpr double kLastCcTokenScale = double(1 << 20);
// Hidden read-only OUTPUT parameters: voice activity for the panel's grid,
// s2::Synth::VoiceStates in kVoiceStateParamCount words of 8 voices (16 bits
// / 65535, exact even through a float), sent when they change.
inline constexpr Steinberg::Vst::ParamID kVoiceStateParamIdBase = 1301;
inline constexpr int kVoiceStateParamCount = 4;
inline constexpr double kVoiceStateScale = 65535.0;
// MIDI program change: the root unit's program list (= the current bank's
// 128 slots) and its kIsProgramChange parameter (0..127), which hosts feed
// with MIDI program changes; the processor switches and reports the slot on
// the hidden read-only token (counter << 7 | slot) / kProgramTokenScale, the
// controller follows (name, LCD).
inline constexpr Steinberg::Vst::ParamID kProgramParamId = 1400; // also the program list ID
inline constexpr Steinberg::Vst::ParamID kProgramTokenParamId = 1401;
inline constexpr double kProgramTokenScale = double(1 << 20);

} // namespace oxo
