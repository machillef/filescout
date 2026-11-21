#pragma once

#include "ListColumnLayout.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace FileScoutNative::Domain
{
    struct WindowBounds
    {
        int32_t x{ 0 };
        int32_t y{ 0 };
        int32_t width{ 0 };
        int32_t height{ 0 };
        bool maximized{ false };
        bool has_bounds{ false };
    };

    struct ScreenWorkArea
    {
        int32_t left{ 0 };
        int32_t top{ 0 };
        int32_t right{ 0 };
        int32_t bottom{ 0 };
    };

    enum class SavedViewMode : int32_t
    {
        Cards = 0,
        List = 1,
    };

    enum class SavedSortColumn : int32_t
    {
        Name = 0,
        Type = 1,
        Size = 2,
        DateModified = 3,
    };

    enum class SavedSortDirection : int32_t
    {
        Ascending = 0,
        Descending = 1,
    };

    struct UserSettings
    {
        WindowBounds window{};
        ListColumnWidths list_column_widths{};
        SavedViewMode view_mode{ SavedViewMode::List };
        bool show_hidden_files{ false };
        bool show_file_extensions{ false };
        bool group_by_sort_column{ false };
        bool dual_pane_visible{ false };
        bool has_dual_pane_setting{ false };
        SavedSortColumn sort_column{ SavedSortColumn::Name };
        SavedSortDirection sort_direction{ SavedSortDirection::Ascending };
    };

    [[nodiscard]] std::string SerializeUserSettings(UserSettings const& settings);

    [[nodiscard]] UserSettings DeserializeUserSettings(std::string_view json);

    [[nodiscard]] WindowBounds ResolveRestoredWindowBounds(
        WindowBounds const& saved,
        std::vector<ScreenWorkArea> const& monitors,
        ScreenWorkArea const& primaryWorkArea,
        int32_t defaultWidth,
        int32_t defaultHeight) noexcept;

    class UserSettingsService
    {
    public:
        [[nodiscard]] static std::filesystem::path GetStoragePath();
        [[nodiscard]] static UserSettings Load();
        static bool Save(UserSettings const& settings);
    };
}
