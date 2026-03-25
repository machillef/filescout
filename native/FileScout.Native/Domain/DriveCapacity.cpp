#include "pch.h"
#include "DriveCapacity.h"
#include <algorithm>

namespace FileScoutNative::Domain
{
    double DriveUsedRatio(std::uintmax_t totalBytes, std::uintmax_t freeBytes)
    {
        if (totalBytes == 0)
            return 0.0;
        auto const used = totalBytes > freeBytes ? totalBytes - freeBytes : 0;
        return std::clamp(
            static_cast<double>(used) / static_cast<double>(totalBytes),
            0.0,
            1.0);
    }
}
