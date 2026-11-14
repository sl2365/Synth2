#pragma once

// Legacy patch / bank names -> UTF-8. Synth1 banks predate UTF-8 on its
// platforms: names are Shift-JIS (CP932, Japanese banks) or Windows-1252.
// Valid UTF-8 passes through; else a strict CP932 decode (Apple:
// CoreFoundation), else Windows-1252 (never fails).

#include <string>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace oxo::s2 {

inline bool IsUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + size_t(len) > s.size()) return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + size_t(k)]) & 0xC0) != 0x80) return false;
        i += size_t(len);
    }
    return true;
}

inline void AppendUtf8(std::string &o, unsigned cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) o += char(0xC0 | cp >> 6), o += char(0x80 | (cp & 0x3F));
    else o += char(0xE0 | cp >> 12), o += char(0x80 | (cp >> 6 & 0x3F)), o += char(0x80 | (cp & 0x3F));
}

inline std::string Cp1252ToUtf8(const std::string &s) {
    // 0x80..0x9F: Windows-1252's printable additions (0 = undefined -> '?')
    static constexpr unsigned short k80[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    std::string o;
    for (const char ch : s) {
        const unsigned c = static_cast<unsigned char>(ch);
        AppendUtf8(o, c >= 0x80 && c < 0xA0 ? (k80[c - 0x80] ? k80[c - 0x80] : '?') : c);
    }
    return o;
}

#if defined(__APPLE__)
inline bool ShiftJisToUtf8(const std::string &s, std::string &out) {
    CFStringRef str = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(s.data()), CFIndex(s.size()),
                                              kCFStringEncodingDOSJapanese, false);
    if (!str) return false;
    const CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(str), kCFStringEncodingUTF8) + 1;
    std::string buf(size_t(max), '\0');
    const bool ok = CFStringGetCString(str, buf.data(), max, kCFStringEncodingUTF8);
    CFRelease(str);
    if (ok) out = buf.c_str();
    return ok;
}
#endif

// Japanese-looking bytes: a CP932 pair led by 0x81..0x84 (symbols, hiragana,
// katakana; rare in Windows-1252), two other pairs (a lone 0x85..0x9F lead is
// more likely 1252 punctuation: Don't), or 2+ half-width katakana in a row (a
// lone 0xA1..0xDF is more likely a Latin-1 sign: degree, (c)...). A 0xE0..
// 0xFC lead counts only with a high trail (a Latin-1 accented letter is
// followed by ASCII).
inline bool LooksShiftJis(const std::string &s) {
    int kana = 0, pairs = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned c = static_cast<unsigned char>(s[i]);
        const unsigned t = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
        const bool trail = t >= 0x40 && t <= 0xFC && t != 0x7F;
        if (c >= 0x81 && c <= 0x84 && trail) return true;
        if ((c >= 0x85 && c <= 0x9F && trail) || (c >= 0xE0 && c <= 0xFC && t >= 0x80 && t <= 0xFC)) {
            if (++pairs >= 2) return true;
            ++i;
            kana = 0;
            continue;
        }
        kana = c >= 0xA1 && c <= 0xDF ? kana + 1 : 0;
        if (kana >= 2) return true;
    }
    return false;
}

inline std::string LegacyToUtf8(const std::string &s) {
    if (IsUtf8(s)) return s;
#if defined(__APPLE__)
    if (std::string u; LooksShiftJis(s) && ShiftJisToUtf8(s, u)) return u;
#endif
    return Cp1252ToUtf8(s);
}

} // namespace oxo::s2
