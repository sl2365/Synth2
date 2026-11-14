#pragma once

// Windows editor visual state only. Audio and voice allocation are untouched.
// The VST3 host and the 30 Hz GUI can coalesce a brief Releasing state into
// a direct Playing->Idle change. Display Release for at least one repaint
// interval when it is observed or that transition is skipped. For longer
// actual releases, the real state remains visible until the voice ends.
#include <array>
#include <cstdint>

namespace oxo::winvoice {
class ActivityDisplay {
public:
    static constexpr uint64_t kMinimumReleaseMs = 120;

    uint64_t Update(uint64_t raw, uint64_t nowMs) {
        uint64_t display = 0;
        for (int i = 0; i < 32; ++i) {
            const int shift = 2 * i;
            const int current = int((raw >> shift) & 3u);
            const int previous = int((lastRaw_ >> shift) & 3u);
            int shown = current <= 2 ? current : 0;
            if (shown == 2) {
                // A retriggered or stolen voice should show Playing now.
                releaseUntil_[i] = 0;
            } else {
                if ((shown == 1 && previous != 1) ||
                    (shown == 0 && previous == 2)) {
                    // The latter case is a Release transition skipped by
                    // host/GUI timing; only the visual state is inferred.
                    releaseUntil_[i] = nowMs + kMinimumReleaseMs;
                }
                if (shown == 0 && nowMs < releaseUntil_[i]) shown = 1;
            }
            display |= uint64_t(shown) << shift;
        }
        lastRaw_ = raw;
        return display;
    }

private:
    uint64_t lastRaw_ = 0;
    std::array<uint64_t, 32> releaseUntil_{};
};
} // namespace oxo::winvoice
