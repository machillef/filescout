#pragma once

namespace FileScoutNative::Domain
{
    // Pixels are shared: a 100k-item folder holds one bitmap per icon cache entry, not one per row.
    // Copying a ShellItemRef (the list keeps several copies) copies a pointer, not 1-4 KB of pixels.
    struct IconBitmap
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        std::shared_ptr<std::vector<uint8_t> const> rgba;

        [[nodiscard]] bool IsEmpty() const noexcept { return !rgba || rgba->empty() || width == 0 || height == 0; }
    };

    struct ShellItemRef
    {
        std::filesystem::path path;
        std::wstring display_name;
        std::wstring parsing_name;
        std::wstring kind_label;
        std::uint32_t drive_type{ 0 };
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
        // True when the filename matches a known archive-like extension (zip/tar/7z/etc.).
        bool is_archive_candidate{ false };
        // True when Windows shell namespace enumeration can browse this archive directly.
        bool is_supported_archive{ false };
    };
}
