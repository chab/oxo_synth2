#pragma once

// MIDI learn (Oxo Simple's model): a PLUGIN-GLOBAL table parameter -> CC
// number (0 = unassigned; CC 0 / Bank Select and the channel mode messages
// 120..127 can't be learned, `IsLearnableCC`). Not part of
// the patch, shared by every instance, nothing mapped by default (hosts replay
// controller state on session load, a factory mapping would silently reset
// parameters). One CC may drive several parameters. CC values are absolute
// and scale linearly onto the parameter's full native range.
// Persisted as a flat JSON object {"<param key>": cc, ...} (keys =
// ParamInfo::key, stable).

#include "Params.hpp"

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace oxo::s2 {

using MidiLearnTable = std::array<int, kParamCount>;

// Every editor-facing parameter; the hidden ctrl src / assign words (-1..65535)
// are not.
inline constexpr bool IsLearnable(int id) {
    return id >= 0 && id < kParamCount && kParams[id].max - kParams[id].min <= 4096;
}

// CC 1..119. 120..127 are channel mode messages (All Sound Off, Reset All
// Controllers, All Notes Off...) that hosts send by themselves on stop /
// panic / loop jumps: learned, the armed control took the transport's
// All Notes Off (CC 123) when Live stopped.
inline constexpr bool IsLearnableCC(int cc) { return cc >= 1 && cc <= 119; }

// Absolute pickup: 0..127 -> [min..max], linear, rounded to nearest.
inline int32_t ScaledValueForCC(int id, int cc7) {
    const ParamInfo &p = kParams[id];
    const long v = cc7 < 0 ? 0 : cc7 > 127 ? 127 : cc7;
    return p.min + int32_t((v * (long(p.max) - p.min) + 63) / 127);
}

inline std::string EncodeMidiLearn(const MidiLearnTable &cc) {
    std::string out = "{";
    bool first = true;
    for (int i = 0; i < kParamCount; ++i) {
        if (!IsLearnable(i) || !IsLearnableCC(cc[i])) continue;
        out += first ? "\n" : ",\n";
        out += "  \"" + std::string(kParams[i].key) + "\": " + std::to_string(cc[i]);
        first = false;
    }
    out += first ? "}\n" : "\n}\n";
    return out;
}

// Tolerant: unknown keys / bad values are skipped.
inline MidiLearnTable DecodeMidiLearn(std::string_view text) {
    MidiLearnTable cc{};
    size_t pos = 0;
    while ((pos = text.find('"', pos)) != std::string_view::npos) {
        const size_t end = text.find('"', pos + 1);
        if (end == std::string_view::npos) break;
        const std::string_view key = text.substr(pos + 1, end - pos - 1);
        size_t q = text.find(':', end);
        pos = end + 1;
        if (q == std::string_view::npos) break;
        ++q;
        while (q < text.size() && std::isspace(static_cast<unsigned char>(text[q]))) ++q;
        int value = 0, digits = 0;
        while (q < text.size() && std::isdigit(static_cast<unsigned char>(text[q])) && digits < 4)
            value = value * 10 + (text[q++] - '0'), ++digits;
        if (!digits || !IsLearnableCC(value)) continue;
        for (int i = 0; i < kParamCount; ++i)
            if (IsLearnable(i) && key == kParams[i].key) cc[i] = value;
    }
    return cc;
}

} // namespace oxo::s2
