#include "pch.h"
#include "ThemeService.h"

#include <cwctype>
#include <fstream>

namespace FileScoutNative::Domain
{
    namespace
    {
        constexpr wchar_t const* k_default_theme_id = L"default-dark";

        std::wstring ToLowerInvariant(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t const ch) { return static_cast<wchar_t>(::towlower(ch)); });
            return value;
        }

        std::string ToUtf8(std::wstring_view const value)
        {
            if (value.empty())
            {
                return {};
            }

            auto const size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            std::string utf8(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                utf8.data(),
                size,
                nullptr,
                nullptr);
            return utf8;
        }

        std::wstring FromUtf8(std::string_view const value)
        {
            if (value.empty())
            {
                return {};
            }

            auto const size = MultiByteToWideChar(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            std::wstring wide(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                wide.data(),
                size);
            return wide;
        }

        std::wstring TrimWhitespace(std::wstring value)
        {
            auto const notSpace = [](wchar_t const ch) { return !std::iswspace(ch); };
            auto const start = std::find_if(value.begin(), value.end(), notSpace);
            if (start == value.end())
            {
                return {};
            }

            auto const end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
            return std::wstring(start, end);
        }

        std::filesystem::path GetThemeStoragePath()
        {
            wchar_t localAppDataBuf[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppDataBuf, MAX_PATH) > 0)
            {
                return std::filesystem::path{ localAppDataBuf } /
                    L"FileScoutNative" /
                    L"theme.txt";
            }

            return std::filesystem::current_path() / L"theme.txt";
        }
    }

    ThemeDefinition ThemeDefinition::DefaultDark()
    {
        return ThemeDefinition{
            .id = L"default-dark",
            .display_name = L"Default Dark",
            .base = 0xFF1C1C1C,
            .surface = 0xFF2D2D2D,
            .surface_alt = 0xFF252525,
            .accent = 0xFF0078D4,
            .accent_secondary = 0xFF005A9E,
            .text_primary = 0xFFFFFFFF,
            .text_secondary = 0xFF9A9A9A,
            .border = 0xFF383838,
            .border_subtle = 0xFF2D2D2D,
            .hover = 0x14FFFFFF,
        };
    }

    ThemeDefinition ThemeDefinition::TokyoNight()
    {
        return ThemeDefinition{
            .id = L"tokyo-night",
            .display_name = L"Tokyo Night",
            .base = 0xFF1A1B26,
            .surface = 0xFF24283B,
            .surface_alt = 0xFF1F2335,
            .accent = 0xFF7AA2F7,
            .accent_secondary = 0xFFBB9AF7,
            .text_primary = 0xFFC0CAF5,
            .text_secondary = 0xFF565F89,
            .border = 0xFF29293F,
            .border_subtle = 0xFF222436,
            .hover = 0x14C0CAF5,
        };
    }

    ThemeDefinition ThemeDefinition::CatppuccinMocha()
    {
        return ThemeDefinition{
            .id = L"catppuccin-mocha",
            .display_name = L"Catppuccin Mocha",
            .base = 0xFF1E1E2E,
            .surface = 0xFF313244,
            .surface_alt = 0xFF181825,
            .accent = 0xFFCBA6F7,
            .accent_secondary = 0xFF94E2D5,
            .text_primary = 0xFFCDD6F4,
            .text_secondary = 0xFF6C7086,
            .border = 0xFF45475A,
            .border_subtle = 0xFF313244,
            .hover = 0x14CDD6F4,
        };
    }

    std::array<ThemeResourceEntry, 12> BuildThemeResourceEntries(
        ThemeDefinition const& theme)
    {
        return {{
            { L"ApplicationPageBackgroundThemeBrush", theme.base },
            { L"CardBackgroundFillColorDefaultBrush", theme.surface },
            { L"CardBackgroundFillColorSecondaryBrush", theme.surface_alt },
            { L"LayerFillColorDefaultBrush", theme.surface_alt },
            { L"LayerFillColorAltBrush", theme.surface_alt },
            { L"AccentFillColorDefaultBrush", theme.accent },
            { L"AccentFillColorSecondaryBrush", theme.accent_secondary },
            { L"TextFillColorPrimaryBrush", theme.text_primary },
            { L"TextFillColorSecondaryBrush", theme.text_secondary },
            { L"CardStrokeColorDefaultBrush", theme.border },
            { L"DividerStrokeColorDefaultBrush", theme.border_subtle },
            { L"SubtleFillColorSecondaryBrush", theme.hover },
        }};
    }

    std::wstring ThemeService::LoadSavedThemeId()
    {
        auto const storagePath = GetThemeStoragePath();
        if (!std::filesystem::exists(storagePath))
        {
            return std::wstring{ k_default_theme_id };
        }

        std::ifstream input(storagePath, std::ios::binary);
        if (!input)
        {
            return std::wstring{ k_default_theme_id };
        }

        std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        auto const trimmed = TrimWhitespace(FromUtf8(content));
        return GetTheme(trimmed).id;
    }

    void ThemeService::SaveThemeId(std::wstring_view const id)
    {
        auto const storagePath = GetThemeStoragePath();
        std::filesystem::create_directories(storagePath.parent_path());

        auto const canonicalId = GetTheme(id).id;
        auto const utf8 = ToUtf8(canonicalId);

        std::ofstream output(storagePath, std::ios::binary | std::ios::trunc);
        output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }

    std::vector<ThemeDefinition> ThemeService::GetAllThemes()
    {
        return {
            ThemeDefinition::DefaultDark(),
            ThemeDefinition::TokyoNight(),
            ThemeDefinition::CatppuccinMocha(),
        };
    }

    ThemeDefinition ThemeService::GetTheme(std::wstring_view const id)
    {
        auto const normalized = ToLowerInvariant(std::wstring{ id });
        for (auto const& theme : GetAllThemes())
        {
            if (theme.id == normalized)
            {
                return theme;
            }
        }

        return ThemeDefinition::DefaultDark();
    }
}
