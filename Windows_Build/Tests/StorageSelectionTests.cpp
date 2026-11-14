#include "Windows_Build/Source/StorageSelection.hpp"
#include "Windows_Build/Source/WindowsManualPaths.hpp"
#include "OxoSynth2VST3/source/VoiceStateProxy.hpp"
#include "Windows_Build/Source/WindowsVoiceActivity.hpp"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace paths = oxo::storage::paths;

int main() {
    const fs::path staging = fs::temp_directory_path() /
        ("oxo_synth2_storage_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path bundleDirectory = staging / "Plugins";
    const fs::path local = bundleDirectory / "Synth2_Data";
    const fs::path moduleDirectory = bundleDirectory / "Synth2.vst3" / "Contents" / "x86_64-win";
    const fs::path moduleLocal = moduleDirectory / "Synth2_Data";
    const fs::path standardData = staging / "Roaming" / "Synth2_Data";
    const fs::path standardLibrary = staging / "Documents" / "Synth2_Data";
    std::error_code ec;
    fs::create_directories(bundleDirectory, ec);
    if (ec) return 1;
    auto select = [&] { return paths::Choose(moduleDirectory, bundleDirectory, standardData, standardLibrary,
                                            [](const fs::path &p) { std::error_code error;
                                                return fs::is_directory(p, error); }); };
    auto fail = [&](const char *message) { std::cerr << "FAIL " << message << '\n'; fs::remove_all(staging, ec); return 1; };
    auto normal = select();
    if (normal.portable || normal.data != standardData || normal.library != standardLibrary)
        return fail("normal mode must not be changed");
    std::cout << "PASS normal folders without portable data\n";

    fs::create_directories(local, ec);
    if (ec) return fail("create temporary local data folder");
    auto portable = select();
    if (!portable.portable || portable.data != local || portable.library != local / "Presets")
        return fail("portable location detection");
    if (portable.standardData != standardData || portable.standardLibrary != standardLibrary)
        return fail("fork-specific standard paths must remain accessible");
    std::cout << "PASS portable selection and configured standard paths retained\n";

    fs::create_directories(moduleLocal, ec);
    if (ec) return fail("create binary-adjacent portable data folder");
    auto nearest = select();
    if (!nearest.portable || nearest.data != moduleLocal || nearest.library != moduleLocal / "Presets")
        return fail("binary-adjacent folder must take precedence over bundle-adjacent folder");
    std::cout << "PASS binary-adjacent portable folder takes precedence\n";

    fs::remove_all(moduleLocal, ec);
    auto fallback = select();
    if (!fallback.portable || fallback.data != local)
        return fail("fall back to bundle-adjacent portable folder");
    std::cout << "PASS falls back to bundle-adjacent portable folder\n";

    fs::remove_all(local, ec);
    auto restored = select();
    if (restored.portable || restored.data != standardData || restored.library != standardLibrary)
        return fail("normal mode after removing portable folder");
    std::cout << "PASS return to normal mode when folder removed\n";
    // Manual lookup never changes storage mode or writes user data.
    const fs::path bundle = bundleDirectory / "Synth2.vst3";
    const fs::path plainManualInBinary = moduleDirectory / "Synth2_Manual.pdf";
    const fs::path manualInBinary = moduleDirectory / "Synth2_Manual_v1.1.5.25.pdf";
    const fs::path manualBesideBundle = bundleDirectory / "Synth2_Manual.pdf";
    const fs::path manualInAppData = standardData / "Synth2_Manual.pdf";
    const std::vector<fs::path> manualSearch = {
        moduleDirectory, bundle / "Contents" / "Resources", bundle,
        bundleDirectory, standardData, standardLibrary
    };
    if (!oxo::winmanual::Find(manualSearch,L"1.1.5.27").empty())
        return fail("missing manual should not resolve");
    std::cout << "PASS missing manual returns no path\n";

    // The stable unversioned filename must work beside the plugin binary.
    { std::ofstream output(plainManualInBinary); output << "test"; }
    if (oxo::winmanual::Find(manualSearch,L"1.1.5.27") != plainManualInBinary)
        return fail("find plain Synth2_Manual.pdf beside binary");
    fs::remove(plainManualInBinary,ec);
    std::cout << "PASS plain Synth2_Manual.pdf beside binary\n";

    // An older versioned PDF must still open after the plugin version bumps.
    { std::ofstream output(manualInBinary); output << "test"; }
    if (oxo::winmanual::Find(manualSearch,L"1.1.5.27") != manualInBinary)
        return fail("find older versioned manual in binary folder");
    std::cout << "PASS find existing versioned manual beside binary\n";

    { std::ofstream output(manualBesideBundle); output << "test"; }
    if (oxo::winmanual::Find(manualSearch,L"1.1.5.27") != manualInBinary)
        return fail("binary folder must take priority");
    fs::remove(manualInBinary,ec);
    if (oxo::winmanual::Find(manualSearch,L"1.1.5.27") != manualBesideBundle)
        return fail("find manual beside VST3 bundle");
    std::cout << "PASS bundled plugin search precedence and fallback\n";

    fs::remove(manualBesideBundle,ec);
    fs::create_directories(standardData,ec);
    if (ec) return fail("create AppData folder for manual test");
    { std::ofstream output(manualInAppData); output << "test"; }
    if (oxo::winmanual::Find(manualSearch,L"1.1.5.27") != manualInAppData)
        return fail("AppData Synth2_Data fallback");
    std::cout << "PASS standard Synth2_Data manual fallback\n";

    // The VST3 hidden grid proxies must transmit Release with a large,
    // unambiguous value, not a tiny fraction easily lost by a host.
    for (int voice = 0; voice < oxo::voiceproxy::kCount; ++voice) {
        for (int state = 0; state <= 2; ++state) {
            const uint64_t original = uint64_t(state) << (2 * voice);
            const double value = oxo::voiceproxy::Normalized(oxo::voiceproxy::State(original, voice));
            if (value != state / 2.0 || oxo::voiceproxy::BitsForVoice(voice, value) != original)
                return fail("voice proxy state roundtrip (Idle, Releasing, Playing)");
        }
    }
    std::cout << "PASS voice grid transport preserves each of 32 voices and Release\n";
    // The GUI must display Release even when host/controller polling skips
    // a short intermediate state. This is ONLY a display treatment.
    {
        oxo::winvoice::ActivityDisplay grid;
        if(grid.Update(2u,1000) != 2u) return fail("voice grid Playing state");
        if(grid.Update(0u,1033) != 1u) return fail("voice grid missed Release fallback");
        if(grid.Update(0u,1120) != 1u) return fail("voice grid Release visible across refreshes");
        if(grid.Update(0u,1153) != 0u) return fail("voice grid Release expires");
        if(grid.Update(1u,2000) != 1u) return fail("voice grid real Release state");
        if(grid.Update(1u,2300) != 1u) return fail("voice grid long actual Release persists");
        if(grid.Update(2u,2350) != 2u) return fail("voice grid retrigger takes precedence");
        if(grid.Update(0u,2370) != 1u) return fail("voice grid retrigger then Release");
        if(grid.Update(0u,2500) != 0u) return fail("voice grid returns to Idle");
        oxo::winvoice::ActivityDisplay high;
        constexpr uint64_t lastVoice = 2ull << 62;
        if(high.Update(lastVoice,1000) != lastVoice ||
           high.Update(0,1030) != (1ull << 62) || high.Update(0,1200) != 0)
            return fail("voice grid 32nd voice transition");
    }
    std::cout << "PASS voice grid Release drawing survives missed/brief transitions\n";
    fs::remove_all(staging, ec);
    std::cout << "12 tests, 0 failed\n";
    return 0;
}
