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

    [[nodiscard]] std::array<ThemeResourceEntry, 12> BuildThemeResourceEntries(
        ThemeDefinition const& theme);

    class ThemeService
    {
    public:
        [[nodiscard]] static std::wstring LoadSavedThemeId();
        static void SaveThemeId(std::wstring_view id);
        [[nodiscard]] static std::vector<ThemeDefinition> GetAllThemes();
        [[nodiscard]] static ThemeDefinition GetTheme(std::wstring_view id);
    };
}
