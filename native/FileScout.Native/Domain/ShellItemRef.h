#pragma once

namespace FileScoutNative::Domain
{
    struct IconBitmap
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        std::vector<uint8_t> rgba;
    };

    struct ShellItemRef
    {
        std::filesystem::path path;
        std::wstring display_name;
        std::wstring parsing_name;
        std::wstring kind_label;
        IconBitmap icon;
        bool is_folder{ false };
        bool size_available{ false };
        std::uintmax_t size_bytes{ 0 };
    };
}
