#pragma once

// Windows saved themes: only individual Themes/<name>.json files.
// Current unsaved colours/preferences are stored in theme_current.json;
// this code never reads, writes, imports or deletes themes.json.
// File-safety helpers are cross-platform for standalone regression tests.

#include "EditorCore/Themes.hpp"
#include "WindowsVoiceGrid.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace oxo::winthemefiles {
namespace fs = std::filesystem;

struct Entry {
    themes::Named theme;
    bool file = true; // all user themes are individual files
};

inline bool SameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Saved files are independent of factory presets. Never discard a file
// just because it has the same name and base palette as a factory theme:
// its six Windows-specific colours may still be different.
inline std::vector<Entry> List(const fs::path &folder) {
    std::vector<Entry> out;
    for (auto &named : themes::LoadFolder(folder))
        out.push_back({std::move(named), true});
    return out;
}

// Windows filename rules; reject invalid names instead of silently changing
// them (which could overwrite an unrelated existing theme file).
inline std::string SafeName(std::string name) {
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = name.find_last_not_of(" \t");
    name = name.substr(first, last - first + 1);
    if (name.empty() || name.size() > 180 || name.front() == '.' || name.back() == '.' || name.back() == ' ') return {};
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*') return {};
    std::string base = name.substr(0, name.find('.'));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") &&
         base[3] >= '1' && base[3] <= '9')) return {};
    return name;
}

// Resolves an existing spelling, including .JSON and ASCII case differences.
// Do not take an arbitrary first match if a case-sensitive filesystem has
// two conflicting files; refuse to replace or delete either one.
inline fs::path ExistingFile(const fs::path &folder, const std::string &name, bool &ambiguous) {
    ambiguous = false;
    fs::path found;
    std::error_code ec;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto path = it->path();
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".json") continue;
        const auto stem = path.stem().u8string();
        if (!SameName(std::string(stem.begin(), stem.end()), name)) continue;
        if (!found.empty()) { ambiguous = true; return {}; }
        found = path;
    }
    if (ec) ambiguous = true;
    return found;
}

inline fs::path UnusedBackup(const fs::path &file, std::string_view suffix) {
    for (int i = 0; i < 10000; ++i) {
        fs::path candidate = file;
        candidate += std::string(suffix) + (i ? "." + std::to_string(i) : "");
        std::error_code ec;
        if (!fs::exists(candidate, ec) && !ec) return candidate;
    }
    return {};
}

// Save always writes a complete 19-colour flat map. On replacing an existing
// file, make a uniquely named backup first. Keep the old file if any step
// fails.
inline bool Save(const fs::path &folder, std::string name, const themes::Palette &palette,
                 const winvoice::Palette &voice) {
    name = SafeName(std::move(name));
    if (name.empty()) return false;
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) return false;
    bool ambiguous = false;
    fs::path target = ExistingFile(folder, name, ambiguous);
    if (ambiguous) return false;
    const bool replacing = !target.empty();
    if (!replacing) {
        const std::u8string utf8(name.begin(), name.end());
        target = folder / fs::path(utf8 + u8".json");
    }
    themes::ExtraColors extras;
    if (replacing) {
        if (fs::is_symlink(target, ec) || ec || !fs::is_regular_file(target, ec) || ec ||
            fs::file_size(target, ec) > 64 * 1024 || ec) return false;
        // An existing file must be readable before it can be replaced.
        std::ifstream in(target, std::ios::binary);
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        json::Value existing;
        if (in.bad() || !json::Parse(old, existing) || !existing.IsObject()) return false;
        extras = themes::ExtraHexColors(existing);
    }
    for (size_t i = 0; i < winvoice::Keys.size(); ++i) extras[winvoice::Keys[i]] = themes::Hex(voice[i]);
    const std::string next = themes::HexMapWithExtras(palette, extras) + "\n";
    if (replacing) {
        std::ifstream in(target, std::ios::binary);
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in.bad() && old == next) return true; // idempotent Save, no unnecessary backups
        const fs::path backup = UnusedBackup(target, ".bak");
        if (backup.empty() || !fs::copy_file(target, backup, fs::copy_options::none, ec) || ec) return false;
    }
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << next) || !out.flush()) {
            fs::remove(tmp, ec);
            return false;
        }
    }
#if defined(_WIN32)
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(tmp, ec);
        return false;
    }
#else
    fs::rename(tmp, target, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
#endif
    return true;
}

// Delete archives the original file rather than destroying it.
inline bool Archive(const fs::path &folder, const std::string &name) {
    if (SafeName(name) != name) return false;
    bool ambiguous = false;
    const fs::path source = ExistingFile(folder, name, ambiguous);
    if (ambiguous || source.empty()) return false;
    std::error_code ec;
    if (fs::is_symlink(source, ec) || ec || !fs::is_regular_file(source, ec) || ec) return false;
    const fs::path backup = UnusedBackup(source, ".deleted.bak");
    if (backup.empty()) return false;
    fs::rename(source, backup, ec);
    return !ec;
}


// Load the extra Windows-specific colours by the SELECTED FILE NAME.
// Matching only the 13 base colours is insufficient: two themes can have
// identical base colours but different voice-grid/browser colours.
inline winvoice::Palette VoiceForFile(const fs::path &folder, const std::string &name,
                                      const themes::Palette &expected) {
    winvoice::Palette palette = winvoice::Default(expected);
    bool ambiguous = false;
    const fs::path file = ExistingFile(folder, name, ambiguous);
    if (ambiguous || file.empty()) return palette;
    json::Value source;
    if (winvoice::ReadJson(file, source) && themes::FromHexMap(source) == expected)
        winvoice::ReadExtras(source, palette);
    return palette;
}

} // namespace oxo::winthemefiles
