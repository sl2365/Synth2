#pragma once

// Windows VST3-only voice-grid transport, not part of the audio engine.
// Report one voice per hidden read-only parameter. The previous 8-voices-in-
// 16-bits format sent differences as small as 1/65535 of a normalized range;
// some hosts can discard those tiny changes, particularly the Release state.
// With 32 proxies, Idle=0.0, Releasing=0.5, Playing=1.0.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oxo::voiceproxy {
inline constexpr int kCount = 32;

inline constexpr int State(uint64_t states, int voice) {
    return int((states >> (2 * voice)) & 3u);
}
inline constexpr double Normalized(int state) {
    return state == 2 ? 1.0 : state == 1 ? 0.5 : 0.0;
}
inline int FromNormalized(double value) {
    return std::clamp(int(std::lround(value * 2.0)), 0, 2);
}
inline uint64_t BitsForVoice(int voice, double normalized) {
    return uint64_t(FromNormalized(normalized)) << (2 * voice);
}
} // namespace oxo::voiceproxy
