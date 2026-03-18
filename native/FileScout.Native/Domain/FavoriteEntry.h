#pragma once

#include <filesystem>
#include <string>

namespace FileScoutNative::Domain
{
    // A pinned location shown in the sidebar navigation panel.
    struct FavoriteEntry
    {
        std::wstring display_name;
        std::filesystem::path path;     // Empty for virtual nodes.
        bool is_drives_view{ false };   // True for the "This PC" virtual node.
        bool is_recycle_bin{ false };   // True for the Recycle Bin shortcut.
    };
}
