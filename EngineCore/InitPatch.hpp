#pragma once

// Synth2's INIT patch (the user's design, 2026-10-30): menu > Initialize
// patch, and the value a knob / display double-click resets to. NOT the
// parameter defaults of Params.hpp: those stay the ORIGINAL's built-in init
// patch, which .sy1 files fall back to for missing lines (legacy loading).
// osc2 pitch 64 = 0 semitones (was 51 = -12), filter kbd track 64 (was 88)
// (user 2026-10-30), delay spread 64 = 0 (was 0 = -64) (user 2026-11-03).

#include "Sy1Codec.hpp"

namespace oxo::s2 {

// clang-format off
inline constexpr PatchValues kInitPatchValues = {
    1, 2, 64, 66, 1, 0, 0, 0, 112, 0, 0, 64, 0, 0, 0, 0, 48, 21, 49, 127,      //  0..19
    0, 64, 64, 0, 0, 0, 49, 127, 49, 64, 0, 1, 1, 14, 29, 10, 51, 45, 0, 0,    // 20..39
    12, 5, 5, 37, 0, 0, 3, 4, 10, 0, 64, 64, 64, 72, 50, 64, 105, 1, 1, 0,     // 40..59
    64, 64, 64, 64, 2, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 26, 0, 0, 0, 64,         // 60..79
    64, 64, 0, 64, 64, 24, 45057, 44, 45057, 49, 64, 0, 0, 2, 16, 0, 3, 1, 64}; // 80..98
// clang-format on

inline Sy1Patch InitPatch() {
    Sy1Patch p;
    p.name = "INIT";
    p.color = "blue";
    p.version = 112;
    p.values = kInitPatchValues;
    return p;
}

} // namespace oxo::s2
