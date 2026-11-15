#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    // Returns the fraction of drive capacity that is used (0.0–1.0).
    // Returns 0.0 if totalBytes is zero (unknown / unavailable capacity).
    // Clamps the result to [0.0, 1.0] in case freeBytes exceeds totalBytes.
    [[nodiscard]] double DriveUsedRatio(std::uintmax_t totalBytes, std::uintmax_t freeBytes);

    // One This PC row: the drive, its icon, and its capacity when the volume reports one.
    struct SidebarDriveUsage
    {
        std::filesystem::path path;
        std::wstring display_name;
        std::uint32_t drive_type{ 0 };
        bool has_usage{ false };
        std::uintmax_t free_bytes{ 0 };
        std::uintmax_t total_bytes{ 0 };
        double used_ratio{ 0.0 };
        IconBitmap icon;
    };
}
