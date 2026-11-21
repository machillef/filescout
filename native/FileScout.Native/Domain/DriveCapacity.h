#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    [[nodiscard]] double DriveUsedRatio(std::uintmax_t totalBytes, std::uintmax_t freeBytes);

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
