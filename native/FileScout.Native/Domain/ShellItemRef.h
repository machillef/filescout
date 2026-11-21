#pragma once

namespace FileScoutNative::Domain
{
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
        bool date_modified_available{ false };
        std::filesystem::file_time_type date_modified{};
        std::wstring date_modified_str;
        bool date_created_available{ false };
        std::wstring date_created_str;
        bool file_attributes_available{ false };
        uint32_t file_attributes{ 0 };
        bool is_image{ false };
        bool is_archive_candidate{ false };
        bool is_supported_archive{ false };
    };
}
