#pragma once

// Platform-neutral choice of data roots. Nothing here moves or edits a preset.
// The two original user-folder paths remain available in portable mode.
#include <filesystem>

namespace oxo::storage::paths {
namespace fs = std::filesystem;

struct Selection {
    fs::path data;
    fs::path library;
    fs::path standardData;
    fs::path standardLibrary;
    bool portable = false;
};

// moduleDirectory contains the real loaded VST3 binary (sl23's original mode).
// pluginDirectory contains the outer *.vst3 bundle (Chab's existing mode).
// Never create or migrate either folder automatically. The first existing
// folder takes precedence; standard user paths remain available in Selection.
template <typename IsDirectory>
Selection Choose(const fs::path &moduleDirectory, const fs::path &pluginDirectory,
                 const fs::path &standardData, const fs::path &standardLibrary,
                 IsDirectory isDirectory) {
    Selection out{standardData, standardLibrary, standardData, standardLibrary, false};
    for (const fs::path &parent : {moduleDirectory, pluginDirectory}) {
        if (parent.empty()) continue;
        const fs::path local = parent / "Synth2_Data";
        if (isDirectory(local)) {
            out.data = local;
            out.library = local / "Presets";
            out.portable = true;
            break;
        }
    }
    return out;
}

// Preserve the original helper's interface for existing callers/tests.
template <typename IsDirectory>
Selection Choose(const fs::path &pluginDirectory, const fs::path &standardData,
                 const fs::path &standardLibrary, IsDirectory isDirectory) {
    return Choose(fs::path{}, pluginDirectory, standardData, standardLibrary, isDirectory);
}
} // namespace oxo::storage::paths
