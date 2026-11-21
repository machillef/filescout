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

    inline constexpr int32_t kRowAnchorInset = 24;

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
