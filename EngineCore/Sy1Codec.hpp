#pragma once

// Synth1 .sy1 patch text: line 1 = name, `key=value` header lines (color,
// ver), then `index,value` lines of native values (missing lines keep the
// init value). ver 109+ share today's encoding. Older versions are converted
// on load exactly like the original does (measured, tools/s1ref/
// measure_legacy.py → EngineCore/LegacyTables.hpp): no ver line = 100, 100 /
// 105 / 106 / 107 / 108 each remap some params and reset the ones that
// version lacked. Out-of-range values are clamped: the original keeps them
// (file and chunk loaders) but plays every one as the nearest in-range value,
// booleans > 1 as on (measured, measure_out_of_range.py).

#include "LegacyTables.hpp"
#include "Params.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace oxo::s2 {

struct Sy1Patch {
    std::string name;
    std::string color = "default";
    int version = 0;
    PatchValues values = DefaultValues();
    bool legacy() const { return version < 112; }
};

inline bool ParseInt(std::string_view s, long &out) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    if (s.empty()) return false;
    std::string tmp(s);
    char *end = nullptr;
    out = std::strtol(tmp.c_str(), &end, 10);
    return end && *end == '\0';
}

inline int LegacyGroup(int version) {
    switch (version) {
    case 0: // no ver line: the original's loader treats it as 100 (measured)
    case 100: return legacy::kVer100;
    case 105:
    case 106: return legacy::kVer105;
    case 107:
    case 108: return legacy::kVer107;
    default: return 0;
    }
}

// The original's conversion of an old-version patch (values as read).
inline void ConvertLegacy(PatchValues &v, int version) {
    const int group = LegacyGroup(version);
    if (!group) return;
    const PatchValues old = v;
    for (const auto &r : legacy::kRules)
        if (r.groups & group)
            v[r.param] = r.table < 0 ? r.value : legacy::kTables[r.table][std::clamp(old[r.param], 0, 127)];
    // ctrl sens 1 / 2 were "wheel -> lfo1 depth / speed" pulling the target
    // toward 127 by s / 127; now additive routing (exact on the whole grid)
    for (auto [sens, target] : {std::pair{kWheelSpeedSens, kLfo1Depth}, std::pair{kWheelAmountSens, kLfo1Speed}})
        v[sens] = 64 + old[sens] * (127 - old[target]) * 63 / (127 * 127);
    // unison was fixed per play mode before it got its knobs (NL2X: 2 voices
    // poly, 4 mono); 107 / 108 detune = the old knob law, remapped
    const bool mono = old[kPlayMode] != 0;
    v[kUnisonVoices] = mono ? 4 : 2;
    v[kUnisonDetune] = group == legacy::kVer107
                           ? legacy::kTables[mono ? legacy::kUnisonDetuneMono : legacy::kUnisonDetunePoly]
                                            [std::clamp(old[kUnisonDetune], 0, 127)]
                           : mono ? 70 : 48;
}

inline Sy1Patch ParseSy1(std::string_view text) {
    Sy1Patch p;
    bool first = true;
    while (!text.empty()) {
        size_t eol = text.find_first_of("\r\n");
        std::string_view line = text.substr(0, eol);
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        if (first) {
            p.name = std::string(line);
            first = false;
            continue;
        }
        if (size_t eq = line.find('='); eq != std::string_view::npos) {
            std::string_view key = line.substr(0, eq), val = line.substr(eq + 1);
            long v = 0;
            if (key == "ver" && ParseInt(val, v)) p.version = int(v);
            else if (key == "color") p.color = std::string(val);
        } else if (size_t comma = line.find(','); comma != std::string_view::npos) {
            long id = 0, v = 0;
            if (ParseInt(line.substr(0, comma), id) && ParseInt(line.substr(comma + 1), v) && id >= 0 &&
                id < kParamCount)
                p.values[id] = ClampValue(int(id), v);
        }
    }
    ConvertLegacy(p.values, p.version);
    return p;
}

// Written as ver=112 (the newest version the original's text loader is known
// to read; identical to 113) so exported patches load back into Synth1.
inline std::string WriteSy1(const Sy1Patch &p) {
    std::string out = p.name + "\ncolor=" + p.color + "\nver=112\n";
    for (int i = 0; i < kParamCount; ++i) out += std::to_string(i) + "," + std::to_string(p.values[i]) + "\n";
    return out;
}

} // namespace oxo::s2
