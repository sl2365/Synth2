#pragma once

// Plugin state chunk (VST3 state, AUv3 fullState["OXS2"]) = 'OXS2' + uint32 LE version, then
//   v1: the patch as .sy1 text (human-readable, and the same codec as patch files)
//   v2: uint32 master volume (s2::kVolumeParamId, not a patch parameter) + text
//   v3: uint32 volume + int32 program slot (-1 = none) + uint32 length + bank folder relative to the library root
//       (s2::LibraryProgram: what the LCD shows, the browser selects) + text
//   v4: v3's header + uint32 length + theme (the editor's palette as JSON, opaque here; "" = never set) + text
// Older chunks restore with the default volume, no program and no theme.

#include "EngineCore/PatchLibrary.hpp"
#include "EngineCore/Sy1Codec.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace oxo::vst3 {

inline constexpr char kStateMagic[4] = {'O', 'X', 'S', '2'};
inline constexpr uint32_t kStateVersion = 4;

struct PluginState {
    s2::Sy1Patch patch;
    int32_t volume = s2::kVolumeParam.def;
    s2::LibraryProgram program;
    std::string theme; // per instance (session only, never in a patch)
};

inline std::vector<uint8_t> EncodeState(const PluginState &state) {
    const std::string text = s2::WriteSy1(state.patch);
    std::vector<uint8_t> out(kStateMagic, kStateMagic + 4);
    const auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
    };
    u32(kStateVersion);
    u32(uint32_t(state.volume));
    u32(uint32_t(state.program.slot));
    u32(uint32_t(state.program.bank.size()));
    out.insert(out.end(), state.program.bank.begin(), state.program.bank.end());
    u32(uint32_t(state.theme.size()));
    out.insert(out.end(), state.theme.begin(), state.theme.end());
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

inline bool DecodeState(const uint8_t *data, size_t size, PluginState &state) {
    size_t at = 0;
    const auto u32 = [&](uint32_t &v) {
        if (size - at < 4) return false;
        v = data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) | (uint32_t(data[at + 3]) << 24);
        at += 4;
        return true;
    };
    uint32_t version = 0, volume = uint32_t(s2::kVolumeParam.def), slot = uint32_t(-1), length = 0;
    if (size < 8 || std::memcmp(data, kStateMagic, 4) != 0) return false;
    at = 4;
    if (!u32(version) || version > kStateVersion) return false;
    if (version >= 2 && !u32(volume)) return false;
    if (version >= 3 && (!u32(slot) || !u32(length) || size - at < length)) return false;
    state.volume = s2::ClampValue(s2::kVolumeParamId, int32_t(volume));
    state.program = {std::string(reinterpret_cast<const char *>(data) + at, length), int32_t(slot) < 0 ? -1 : int(slot)};
    if (state.program.slot >= s2::kBankSlots) state.program = {};
    at += length;
    uint32_t themeLength = 0;
    if (version >= 4 && (!u32(themeLength) || size - at < themeLength)) return false;
    state.theme.assign(reinterpret_cast<const char *>(data) + at, themeLength);
    at += themeLength;
    state.patch = s2::ParseSy1(std::string_view(reinterpret_cast<const char *>(data) + at, size - at));
    state.patch.name = s2::LegacyToUtf8(state.patch.name);
    return true;
}

} // namespace oxo::vst3
