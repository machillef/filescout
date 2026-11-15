#pragma once

#include <cstdint>

namespace FileScoutNative::Domain
{
    // What a directory load in `loadPaneIndex` may touch besides its own pane (UB-07). The toolbar
    // (breadcrumbs, search placeholder, Back/Forward, address box, status, details) is shared by both
    // panes and shows the active one; selectAfter goes through the active pane's list. A load in the
    // other pane, or one whose pane stopped being active before its rows arrived, touches neither.
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
