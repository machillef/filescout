#pragma once

#include "ThemeDefinition.h"

#include <array>
#include <string_view>
#include <vector>

namespace FileScoutNative::Domain
{
    struct ThemeResourceEntry
    {
        std::wstring_view resource_key;
        std::uint32_t color{ 0 };
    };

    // Opaque caption-button colors derived from a theme so Minimize/Maximize hover states
    // lighten visibly against the titlebar base instead of falling back to dark system overlays.
    struct CaptionButtonPalette
    {
        std::uint32_t foreground{ 0 };
        std::uint32_t inactive_foreground{ 0 };
        std::uint32_t hover_background{ 0 };
        std::uint32_t pressed_background{ 0 };
    };

    [[nodiscard]] std::array<ThemeResourceEntry, 19> BuildThemeResourceEntries(
        ThemeDefinition const& theme);

    [[nodiscard]] CaptionButtonPalette BuildCaptionButtonPalette(
        ThemeDefinition const& theme) noexcept;

    class ThemeService
    {
    public:
        [[nodiscard]] static std::wstring LoadSavedThemeId();
        // Returns false (never throws) when the theme file cannot be written.
        static bool SaveThemeId(std::wstring_view id) noexcept;
        [[nodiscard]] static std::vector<ThemeDefinition> GetAllThemes();
        [[nodiscard]] static ThemeDefinition GetTheme(std::wstring_view id);
    };
}
