#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace FileScoutNative::Domain
{
    enum class ListColumnIndex : int
    {
        Name = 0,
        Type = 1,
        Size = 2,
        DateModified = 3,
    };

    struct ListColumnWidths
    {
        static constexpr double kDefaultName = 240.0;
        static constexpr double kDefaultType = 100.0;
        static constexpr double kDefaultSize = 76.0;
        static constexpr double kDefaultDate = 136.0;

        static constexpr double kMinName = 100.0;
        static constexpr double kMinType = 64.0;
        static constexpr double kMinSize = 56.0;
        static constexpr double kMinDate = 100.0;

        static constexpr double kMaxWidth = 1200.0;

        double name{ kDefaultName };
        double type{ kDefaultType };
        double size{ kDefaultSize };
        double date{ kDefaultDate };
    };

    [[nodiscard]] constexpr double MinWidthForListColumn(ListColumnIndex const col) noexcept
    {
        switch (col)
        {
        case ListColumnIndex::Name: return ListColumnWidths::kMinName;
        case ListColumnIndex::Type: return ListColumnWidths::kMinType;
        case ListColumnIndex::Size: return ListColumnWidths::kMinSize;
        case ListColumnIndex::DateModified: return ListColumnWidths::kMinDate;
        }
        return ListColumnWidths::kMinType;
    }

    [[nodiscard]] constexpr double DefaultWidthForListColumn(ListColumnIndex const col) noexcept
    {
        switch (col)
        {
        case ListColumnIndex::Name: return ListColumnWidths::kDefaultName;
        case ListColumnIndex::Type: return ListColumnWidths::kDefaultType;
        case ListColumnIndex::Size: return ListColumnWidths::kDefaultSize;
        case ListColumnIndex::DateModified: return ListColumnWidths::kDefaultDate;
        }
        return ListColumnWidths::kDefaultType;
    }

    [[nodiscard]] constexpr double GetListColumnWidth(
        ListColumnWidths const& widths,
        ListColumnIndex const col) noexcept
    {
        switch (col)
        {
        case ListColumnIndex::Name: return widths.name;
        case ListColumnIndex::Type: return widths.type;
        case ListColumnIndex::Size: return widths.size;
        case ListColumnIndex::DateModified: return widths.date;
        }
        return widths.type;
    }

    [[nodiscard]] inline ListColumnWidths SetListColumnWidth(
        ListColumnWidths widths,
        ListColumnIndex const col,
        double const widthPx) noexcept
    {
        double const clamped = std::isfinite(widthPx)
            ? std::clamp(widthPx, MinWidthForListColumn(col), ListColumnWidths::kMaxWidth)
            : DefaultWidthForListColumn(col);
        switch (col)
        {
        case ListColumnIndex::Name: widths.name = clamped; break;
        case ListColumnIndex::Type: widths.type = clamped; break;
        case ListColumnIndex::Size: widths.size = clamped; break;
        case ListColumnIndex::DateModified: widths.date = clamped; break;
        }
        return widths;
    }

    [[nodiscard]] inline ListColumnWidths ResizeListColumn(
        ListColumnWidths const& widths,
        ListColumnIndex const col,
        double const deltaPx) noexcept
    {
        if (!std::isfinite(deltaPx))
        {
            return widths;
        }
        return SetListColumnWidth(widths, col, GetListColumnWidth(widths, col) + deltaPx);
    }

    [[nodiscard]] inline ListColumnWidths ResetListColumn(
        ListColumnWidths const& widths,
        ListColumnIndex const col) noexcept
    {
        return SetListColumnWidth(widths, col, DefaultWidthForListColumn(col));
    }
}
