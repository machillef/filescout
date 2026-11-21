#include "pch.h"
#include <gtest/gtest.h>

#include "Domain/UserSettings.h"

#include <fstream>
#include <optional>

using namespace FileScoutNative::Domain;

namespace
{
    class ScopedEnvVar
    {
    public:
        ScopedEnvVar(std::wstring name, std::optional<std::wstring> value)
            : m_name(std::move(name))
        {
            auto const required = ::GetEnvironmentVariableW(m_name.c_str(), nullptr, 0);
            if (required > 0)
            {
                std::wstring buffer(required, L'\0');
                auto const written = ::GetEnvironmentVariableW(m_name.c_str(), buffer.data(), required);
                if (written > 0)
                {
                    buffer.resize(written);
                    m_original = std::move(buffer);
                }
            }

            ::SetEnvironmentVariableW(
                m_name.c_str(),
                value.has_value() ? value->c_str() : nullptr);
        }

        ~ScopedEnvVar()
        {
            ::SetEnvironmentVariableW(
                m_name.c_str(),
                m_original.has_value() ? m_original->c_str() : nullptr);
        }

    private:
        std::wstring m_name;
        std::optional<std::wstring> m_original;
    };

    std::filesystem::path MakeTempDir(std::wstring_view const suffix)
    {
        auto base = std::filesystem::temp_directory_path() /
            (L"FileScoutUserSettingsTests-" +
             std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(::GetTickCount64()) + L"-" +
             std::wstring{ suffix });
        std::filesystem::create_directories(base);
        return base;
    }
}

TEST(UserSettingsTests, RoundTripSerializationPreservesAllFieldsIncludingNegativeCoords)
{
    UserSettings original{};
    original.window = WindowBounds{
        .x = -1280,
        .y = 40,
        .width = 1440,
        .height = 860,
        .maximized = true,
        .has_bounds = true,
    };
    original.list_column_widths = ListColumnWidths{
        .name = 320.5,
        .type = 140.0,
        .size = 92.0,
        .date = 188.5,
    };
    original.view_mode = SavedViewMode::Cards;
    original.show_hidden_files = true;
    original.show_file_extensions = true;
    original.group_by_sort_column = true;
    original.dual_pane_visible = true;
    original.has_dual_pane_setting = true;
    original.sort_column = SavedSortColumn::DateModified;
    original.sort_direction = SavedSortDirection::Descending;

    auto const json = SerializeUserSettings(original);
    auto const parsed = DeserializeUserSettings(json);

    EXPECT_TRUE(parsed.window.has_bounds);
    EXPECT_EQ(parsed.window.x, -1280);
    EXPECT_EQ(parsed.window.y, 40);
    EXPECT_EQ(parsed.window.width, 1440);
    EXPECT_EQ(parsed.window.height, 860);
    EXPECT_TRUE(parsed.window.maximized);

    EXPECT_DOUBLE_EQ(parsed.list_column_widths.name, 320.5);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.type, 140.0);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.size, 92.0);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.date, 188.5);

    EXPECT_EQ(parsed.view_mode, SavedViewMode::Cards);
    EXPECT_TRUE(parsed.show_hidden_files);
    EXPECT_TRUE(parsed.show_file_extensions);
    EXPECT_TRUE(parsed.group_by_sort_column);
    EXPECT_TRUE(parsed.has_dual_pane_setting);
    EXPECT_TRUE(parsed.dual_pane_visible);
    EXPECT_EQ(parsed.sort_column, SavedSortColumn::DateModified);
    EXPECT_EQ(parsed.sort_direction, SavedSortDirection::Descending);
}

TEST(UserSettingsTests, DeserializerClampsColumnsIgnoresUnknownKeysAndSurvivesCorruptJson)
{
    auto const parsed = DeserializeUserSettings(R"({
        "unknown_nested": {"a": [1, 2, "x"]},
        "col_name": 10,
        "col_type": 50000,
        "col_size": -5,
        "col_date": 210,
        "view_mode": "unknown_mode",
        "sort_column": "size",
        "sort_direction": "desc",
        "show_hidden_files": true
    })");

    EXPECT_DOUBLE_EQ(parsed.list_column_widths.name, ListColumnWidths::kMinName);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.type, ListColumnWidths::kMaxWidth);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.size, ListColumnWidths::kMinSize);
    EXPECT_DOUBLE_EQ(parsed.list_column_widths.date, 210.0);
    EXPECT_EQ(parsed.view_mode, SavedViewMode::List);
    EXPECT_EQ(parsed.sort_column, SavedSortColumn::Size);
    EXPECT_EQ(parsed.sort_direction, SavedSortDirection::Descending);
    EXPECT_TRUE(parsed.show_hidden_files);
    EXPECT_FALSE(parsed.has_dual_pane_setting);
    EXPECT_FALSE(parsed.window.has_bounds);

    auto const corrupt = DeserializeUserSettings("not json at all {{{");
    EXPECT_FALSE(corrupt.window.has_bounds);
    EXPECT_EQ(corrupt.view_mode, SavedViewMode::List);
    EXPECT_DOUBLE_EQ(corrupt.list_column_widths.name, ListColumnWidths::kDefaultName);
}

TEST(UserSettingsTests, ResolveRestoredWindowBoundsHandlesDefaultsMultiMonitorAndDisconnectedDisplay)
{
    ScreenWorkArea const primary{ 0, 0, 1920, 1040 };
    ScreenWorkArea const leftSecondary{ -1920, 0, 0, 1040 };
    std::vector<ScreenWorkArea> const dualMonitors{ primary, leftSecondary };

    // No saved bounds -> centered default capped to 94% of primary work area.
    auto const fallback = ResolveRestoredWindowBounds(
        WindowBounds{},
        dualMonitors,
        primary,
        1520,
        920);
    EXPECT_TRUE(fallback.has_bounds);
    EXPECT_EQ(fallback.width, 1520);
    EXPECT_EQ(fallback.height, 920);
    EXPECT_EQ(fallback.x, (1920 - 1520) / 2);
    EXPECT_EQ(fallback.y, (1040 - 920) / 2);

    // Valid window on left secondary monitor -> stays on secondary monitor.
    WindowBounds const onLeft{
        .x = -1700,
        .y = 80,
        .width = 1200,
        .height = 800,
        .maximized = false,
        .has_bounds = true,
    };
    auto const restoredLeft = ResolveRestoredWindowBounds(onLeft, dualMonitors, primary, 1520, 920);
    EXPECT_EQ(restoredLeft.x, -1700);
    EXPECT_EQ(restoredLeft.y, 80);
    EXPECT_EQ(restoredLeft.width, 1200);
    EXPECT_EQ(restoredLeft.height, 800);

    // Secondary monitor unplugged -> falls back inside primary work area and clamps oversized dimensions.
    WindowBounds const unplugged{
        .x = -1700,
        .y = 80,
        .width = 2400,
        .height = 1400,
        .maximized = true,
        .has_bounds = true,
    };
    auto const restoredPrimary = ResolveRestoredWindowBounds(
        unplugged,
        std::vector<ScreenWorkArea>{ primary },
        primary,
        1520,
        920);
    EXPECT_EQ(restoredPrimary.width, 1920);
    EXPECT_EQ(restoredPrimary.height, 1040);
    EXPECT_EQ(restoredPrimary.x, 0);
    EXPECT_EQ(restoredPrimary.y, 0);
    EXPECT_TRUE(restoredPrimary.maximized);
}

TEST(UserSettingsTests, ServiceSavesAtomicallyAndReloadsFromLocalAppData)
{
    auto const tempRoot = MakeTempDir(L"persist");
    struct Cleanup
    {
        std::filesystem::path dir;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } cleanup{ tempRoot };

    ScopedEnvVar const env{ L"LOCALAPPDATA", tempRoot.wstring() };

    // Missing file returns clean defaults.
    auto const initial = UserSettingsService::Load();
    EXPECT_FALSE(initial.window.has_bounds);
    EXPECT_FALSE(initial.show_hidden_files);

    UserSettings toSave{};
    toSave.window = WindowBounds{ 120, 90, 1280, 760, false, true };
    toSave.list_column_widths.name = 360.0;
    toSave.show_hidden_files = true;
    toSave.show_file_extensions = true;
    toSave.dual_pane_visible = true;
    toSave.has_dual_pane_setting = true;
    toSave.sort_column = SavedSortColumn::Type;
    toSave.sort_direction = SavedSortDirection::Descending;

    ASSERT_TRUE(UserSettingsService::Save(toSave));
    EXPECT_TRUE(std::filesystem::exists(UserSettingsService::GetStoragePath()));

    auto const reloaded = UserSettingsService::Load();
    EXPECT_TRUE(reloaded.window.has_bounds);
    EXPECT_EQ(reloaded.window.x, 120);
    EXPECT_EQ(reloaded.window.width, 1280);
    EXPECT_DOUBLE_EQ(reloaded.list_column_widths.name, 360.0);
    EXPECT_TRUE(reloaded.show_hidden_files);
    EXPECT_TRUE(reloaded.show_file_extensions);
    EXPECT_TRUE(reloaded.has_dual_pane_setting);
    EXPECT_TRUE(reloaded.dual_pane_visible);
    EXPECT_EQ(reloaded.sort_column, SavedSortColumn::Type);
    EXPECT_EQ(reloaded.sort_direction, SavedSortDirection::Descending);
}
