#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace FileScoutNative::Domain
{
    struct ScreenPoint
    {
        int32_t x{ 0 };
        int32_t y{ 0 };
    };

    struct ScreenRect
    {
        int32_t left{ 0 };
        int32_t top{ 0 };
        int32_t right{ 0 };
        int32_t bottom{ 0 };

        [[nodiscard]] constexpr bool IsEmpty() const noexcept { return right <= left || bottom <= top; }
    };

    // Horizontal offset from the row's left edge, so the menu does not cover the row's icon edge.
    inline constexpr int32_t kRowAnchorInset = 24;

    // Where a shell menu opens, in screen pixels (UB-15). A mouse click wins; from the keyboard or
    // the command palette the menu opens just below the focused row (clamped into the window if the
    // row is scrolled out of view); with no row it opens at the window centre. It never falls back to
    // a fixed screen position.
    [[nodiscard]] constexpr ScreenPoint ChooseContextMenuAnchor(
        std::optional<ScreenPoint> const clickPoint,
        std::optional<ScreenRect> const focusedRow,
        ScreenRect const window) noexcept
    {
        if (clickPoint.has_value())
        {
            return *clickPoint;
        }

        if (focusedRow.has_value() && !focusedRow->IsEmpty())
        {
            auto const x = focusedRow->left + (std::min)(kRowAnchorInset, (focusedRow->right - focusedRow->left) / 2);
            return ScreenPoint{
                std::clamp(x, window.left, (std::max)(window.left, window.right)),
                std::clamp(focusedRow->bottom, window.top, (std::max)(window.top, window.bottom)),
            };
        }

        return ScreenPoint{ window.left + (window.right - window.left) / 2, window.top + (window.bottom - window.top) / 2 };
    }
}
