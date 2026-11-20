#include "pch.h"
#include "ThemeService.h"
#include "TextFold.h"

#include <cwctype>
#include <fstream>
#include <optional>

namespace FileScoutNative::Domain
{
    namespace
    {
        constexpr wchar_t const* k_default_theme_id = L"default-dark";

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

        // Reads an environment variable of any length: a fixed MAX_PATH buffer gets only the
        // required size back (nothing written) when the value is 260+ characters.
        std::optional<std::wstring> ReadEnvironmentVariable(wchar_t const* name)
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                auto const length = GetEnvironmentVariableW(
                    name, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::nullopt;
                }

                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return buffer;
                }

                buffer.resize(length); // the required size, terminator included
            }

            return std::nullopt;
        }

        // Paths of MAX_PATH or more only work through the \\?\ prefix (no long-path manifest).
        std::filesystem::path WithLongPathPrefix(std::filesystem::path path)
        {
            auto const& native = path.native();
            if (native.size() < 248 || native.starts_with(L"\\\\?\\"))
            {
                return path;
            }

            if (native.starts_with(L"\\\\"))
            {
                return std::filesystem::path{ L"\\\\?\\UNC\\" + native.substr(2) };
            }

            return std::filesystem::path{ L"\\\\?\\" + native };
        }

        std::filesystem::path GetThemeStoragePath()
        {
            if (auto const localAppData = ReadEnvironmentVariable(L"LOCALAPPDATA");
                localAppData.has_value())
            {
                std::filesystem::path const base{ *localAppData };
                if (base.is_absolute())
                {
                    return WithLongPathPrefix(
                        (base / L"FileScoutNative" / L"theme.txt").lexically_normal());
                }
            }

            std::error_code ec;
            return WithLongPathPrefix(std::filesystem::current_path(ec) / L"theme.txt");
        }

        // create_directories without exceptions, one CreateDirectoryW per missing level so a
        // \\?\-prefixed path never has its prefix split into components.
        bool EnsureDirectory(std::filesystem::path const& directory)
        {
            std::error_code ec;
            if (std::filesystem::is_directory(directory, ec))
            {
                return true;
            }

            auto const parent = directory.parent_path();
            if (parent.empty() || parent == directory || !EnsureDirectory(parent))
            {
                return false;
            }

            if (CreateDirectoryW(directory.c_str(), nullptr) != FALSE)
            {
                return true;
            }

            return GetLastError() == ERROR_ALREADY_EXISTS &&
                std::filesystem::is_directory(directory, ec);
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

    std::array<ThemeResourceEntry, 19> BuildThemeResourceEntries(
        ThemeDefinition const& theme)
    {
        return {{
            { L"ApplicationPageBackgroundThemeBrush", theme.base },
            { L"CardBackgroundFillColorDefaultBrush", theme.surface },
            { L"CardBackgroundFillColorSecondaryBrush", theme.surface_alt },
            { L"LayerFillColorDefaultBrush", theme.surface_alt },
            { L"LayerFillColorAltBrush", theme.surface_alt },
            { L"ControlFillColorDefaultBrush", theme.surface },
            { L"ControlStrokeColorDefaultBrush", theme.border },
            { L"ExpanderHeaderBackground", theme.surface_alt },
            { L"ExpanderHeaderForeground", theme.text_primary },
            { L"ExpanderContentBackground", theme.surface_alt },
            { L"ExpanderHeaderBorderBrush", theme.border_subtle },
            { L"ExpanderContentBorderBrush", theme.border_subtle },
            { L"AccentFillColorDefaultBrush", theme.accent },
            { L"AccentFillColorSecondaryBrush", theme.accent_secondary },
            { L"TextFillColorPrimaryBrush", theme.text_primary },
            { L"TextFillColorSecondaryBrush", theme.text_secondary },
            { L"CardStrokeColorDefaultBrush", theme.border },
            { L"DividerStrokeColorDefaultBrush", theme.border_subtle },
            { L"SubtleFillColorSecondaryBrush", theme.hover },
        }};
    }

    namespace
    {
        // Pre-blend `fg` over opaque `bg` at `alphaByte / 255` so AppWindowTitleBar receives
        // an exact opaque RGB color (avoiding DWM/backdrop dark-overlay blending quirks).
        [[nodiscard]] std::uint32_t BlendOpaque(
            std::uint32_t const bg,
            std::uint32_t const fg,
            std::uint32_t const alphaByte) noexcept
        {
            auto const blendChannel = [alphaByte](std::uint32_t const b, std::uint32_t const f) noexcept
            {
                return (f * alphaByte + b * (255u - alphaByte) + 127u) / 255u;
            };
            auto const r = blendChannel((bg >> 16) & 0xFFu, (fg >> 16) & 0xFFu);
            auto const g = blendChannel((bg >> 8) & 0xFFu, (fg >> 8) & 0xFFu);
            auto const b = blendChannel(bg & 0xFFu, fg & 0xFFu);
            return 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    CaptionButtonPalette BuildCaptionButtonPalette(ThemeDefinition const& theme) noexcept
    {
        return CaptionButtonPalette{
            .foreground = theme.text_primary,
            .inactive_foreground = theme.text_secondary,
            // ~16% foreground lift over base for crisp Windows Terminal-style hover feedback.
            .hover_background = BlendOpaque(theme.base, theme.text_primary, 41u),
            // ~10% foreground lift for subtle recessed pressed feedback.
            .pressed_background = BlendOpaque(theme.base, theme.text_primary, 26u),
        };
    }

    std::wstring ThemeService::LoadSavedThemeId()
    {
        auto const storagePath = GetThemeStoragePath();
        std::error_code ec;
        if (!std::filesystem::exists(storagePath, ec))
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

    bool ThemeService::SaveThemeId(std::wstring_view const id) noexcept
    {
        try
        {
            auto const storagePath = GetThemeStoragePath();
            if (!EnsureDirectory(storagePath.parent_path()))
            {
                return false;
            }

            auto const canonicalId = GetTheme(id).id;
            auto const utf8 = ToUtf8(canonicalId);

            std::ofstream output(storagePath, std::ios::binary | std::ios::trunc);
            output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            output.close();
            return !output.fail();
        }
        catch (...)
        {
            return false;
        }
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
        auto const normalized = FoldCase(id);
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
