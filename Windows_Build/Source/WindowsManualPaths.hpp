#pragma once

// Locate the optional user manual without depending on the host DAW's
// working directory. Supplied directories are in search-priority order.
// This helper does not create, modify, or migrate any files.
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace oxo::winmanual {
namespace fs = std::filesystem;

inline fs::path Find(const std::vector<fs::path>& directories,
                     const std::wstring& version) {
    const auto regular = [](const fs::path& file) {
        std::error_code error;
        return fs::is_regular_file(file, error) && !error;
    };
    // The stable filename is preferred; the versioned name is also accepted.
    // Older versioned PDFs remain discoverable when the fork's build number
    // changes without requiring the user to rename their existing manual.
    const std::wstring current = L"Synth2_Manual_v" + version + L".pdf";
    for (const auto& dir : directories) {
        if (dir.empty()) continue;
        for (const auto& name : {L"Synth2_Manual.pdf", current.c_str()}) {
            const fs::path file = dir / name;
            if (regular(file)) return file;
        }
        std::error_code error;
        std::vector<fs::path> versioned;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error),
                                    end;
             !error && it != end; it.increment(error)) {
            const fs::path file = it->path();
            if (!regular(file)) continue;
            std::wstring name = file.filename().wstring();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t c) { return std::towlower(c); });
            if (name.rfind(L"synth2_manual_v", 0) == 0 &&
                name.size() > 18 && name.compare(name.size()-4, 4, L".pdf") == 0)
                versioned.push_back(file);
        }
        if (!versioned.empty()) {
            // Deterministic fallback if more than one older manual is present.
            std::sort(versioned.begin(), versioned.end());
            return versioned.back();
        }
    }
    return {};
}
} // namespace oxo::winmanual
