#pragma once

#include <cstdint>

namespace FileScoutNative::Domain
{
    struct LoadChromeUpdates
    {
        bool toolbar{ false };
        bool select_after{ false };
    };

    [[nodiscard]] constexpr LoadChromeUpdates ChromeUpdatesForLoad(
        int32_t const loadPaneIndex,
        int32_t const activePaneIndex) noexcept
    {
        bool const ownsChrome = loadPaneIndex == activePaneIndex;
        return LoadChromeUpdates{ ownsChrome, ownsChrome };
    }
}
