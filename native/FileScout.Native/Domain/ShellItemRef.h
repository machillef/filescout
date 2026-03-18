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
        // Date modified (populated by ShellInteropService::EnumerateDirectory).
        bool date_modified_available{ false };
        std::filesystem::file_time_type date_modified{};
        std::wstring date_modified_str;  // Locale-formatted for display, e.g. "3/18/2026 10:30 AM"
        // Date created (populated by ShellInteropService::EnumerateDirectory via GetFileAttributesExW).
        bool date_created_available{ false };
        std::wstring date_created_str;
        // File attributes (FILE_ATTRIBUTE_* bitmask from GetFileAttributesExW, 0 if unavailable).
        bool file_attributes_available{ false };
        uint32_t file_attributes{ 0 };
        // True when the file extension belongs to a common image format (jpg/png/gif/bmp/etc.).
        bool is_image{ false };
    };
}
