#pragma once

#include <filesystem>
#include <string>

namespace FileScoutNative::Domain
{
    struct FavoriteEntry
    {
        std::wstring display_name;
        std::filesystem::path path;
        bool is_drives_view{ false };
        bool is_recycle_bin{ false };
        bool is_pinned{ false };
    };
}
