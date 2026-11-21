#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace FileScoutNative::Domain
{
    inline constexpr std::array<std::wstring_view, 11> kThemeBrushKeys{
        L"AccentFillColorDefaultBrush",
        L"AccentTextFillColorPrimaryBrush",
        L"CardBackgroundFillColorDefaultBrush",
        L"CardBackgroundFillColorSecondaryBrush",
        L"CardStrokeColorDefaultBrush",
        L"DividerStrokeColorDefaultBrush",
        L"LayerFillColorAltBrush",
        L"SubtleFillColorSecondaryBrush",
        L"SystemFillColorCriticalBrush",
        L"TextFillColorPrimaryBrush",
        L"TextFillColorSecondaryBrush",
    };

    [[nodiscard]] constexpr bool IsKnownThemeBrushKey(std::wstring_view const key) noexcept
    {
        return std::find(kThemeBrushKeys.begin(), kThemeBrushKeys.end(), key) != kThemeBrushKeys.end();
    }
}
