#pragma once

// Identity strings the code needs (C, C++ and Swift through the bridging
// headers). Source of truth: Identity.xcconfig. The mac VST3 build generates
// OxoSynth2Identity.h from it (CMake). Other builds (iPad, tools) take the
// fallback: there the per-user folder sits inside the app's own sandbox, so
// it cannot collide with another product and forks need not touch it.

#if __has_include("OxoSynth2Identity.h")
#include "OxoSynth2Identity.h"
#else
#define OXO_DATA_FOLDER "Oxo Music/Oxo Synth2"
// No update check (the App Store updates the iPad app): empty product =
// no menu entry, the site / email are then unused.
#define OXO_UPDATE_PRODUCT ""
#define OXO_VENDOR_URL ""
#define OXO_VENDOR_EMAIL ""
#endif
