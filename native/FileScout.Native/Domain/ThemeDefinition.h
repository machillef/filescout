#pragma once

#include <cstdint>
#include <string>

namespace FileScoutNative::Domain
{
    struct ThemeDefinition
    {
        std::wstring id;
        std::wstring display_name;
        std::uint32_t base{ 0 };
        std::uint32_t surface{ 0 };
        std::uint32_t surface_alt{ 0 };
        std::uint32_t accent{ 0 };
        std::uint32_t accent_secondary{ 0 };
        std::uint32_t text_primary{ 0 };
        std::uint32_t text_secondary{ 0 };
        std::uint32_t border{ 0 };
        std::uint32_t border_subtle{ 0 };
        std::uint32_t hover{ 0 };

        [[nodiscard]] static ThemeDefinition DefaultDark();
        [[nodiscard]] static ThemeDefinition TokyoNight();
        [[nodiscard]] static ThemeDefinition CatppuccinMocha();
    };
}
