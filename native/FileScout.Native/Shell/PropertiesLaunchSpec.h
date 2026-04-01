#pragma once

#include <filesystem>
#include <string>
#include <windows.h>
#include <shlobj_core.h>

namespace FileScoutNative::Shell::detail
{
    struct PropertiesLaunchSpec
    {
        DWORD object_type;
        std::wstring object_name;
        std::wstring property_page;
    };

    [[nodiscard]] inline PropertiesLaunchSpec BuildPropertiesLaunchSpec(
        std::filesystem::path const& path)
    {
        return PropertiesLaunchSpec{
            SHOP_FILEPATH,
            path.wstring(),
            {},
        };
    }
}
