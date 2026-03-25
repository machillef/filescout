#pragma once
#include <cstdint>

namespace FileScoutNative::Domain
{
    // Returns the fraction of drive capacity that is used (0.0–1.0).
    // Returns 0.0 if totalBytes is zero (unknown / unavailable capacity).
    // Clamps the result to [0.0, 1.0] in case freeBytes exceeds totalBytes.
    [[nodiscard]] double DriveUsedRatio(std::uintmax_t totalBytes, std::uintmax_t freeBytes);
}
