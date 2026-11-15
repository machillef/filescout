#pragma once

#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    enum class ShellItemSortColumn
    {
        Name,
        Type,
        Size,
        DateModified,
    };

    enum class ShellItemSortDirection
    {
        Ascending,
        Descending,
    };

    namespace detail
    {
        inline int CompareCaseInsensitive(std::wstring const& left, std::wstring const& right)
        {
            return _wcsicmp(left.c_str(), right.c_str());
        }

        inline int CompareOrdinal(std::wstring const& left, std::wstring const& right)
        {
            auto const comparison = left.compare(right);
            return comparison < 0 ? -1 : (comparison > 0 ? 1 : 0);
        }

        // Case-insensitive name first; names equal ignoring case (search hits from different
        // folders, "a" vs "A") fall back to the ordinal name and then the full path, so the
        // order never depends on the input order.
        inline int CompareByName(ShellItemRef const& left, ShellItemRef const& right)
        {
            if (auto const comparison = CompareCaseInsensitive(left.display_name, right.display_name);
                comparison != 0)
            {
                return comparison;
            }

            if (auto const comparison = CompareOrdinal(left.display_name, right.display_name);
                comparison != 0)
            {
                return comparison;
            }

            return CompareOrdinal(left.path.native(), right.path.native());
        }
    }

    inline int CompareShellItems(
        ShellItemRef const& left,
        ShellItemRef const& right,
        ShellItemSortColumn const column)
    {
        if (left.is_folder != right.is_folder)
        {
            return left.is_folder ? -1 : 1;
        }

        switch (column)
        {
        case ShellItemSortColumn::Name:
            return detail::CompareByName(left, right);

        case ShellItemSortColumn::Type:
        {
            auto const typeComparison =
                detail::CompareCaseInsensitive(left.kind_label, right.kind_label);
            return typeComparison != 0
                ? typeComparison
                : detail::CompareByName(left, right);
        }

        case ShellItemSortColumn::Size:
            if (left.size_available != right.size_available)
            {
                return left.size_available ? -1 : 1;
            }
            if (left.size_bytes != right.size_bytes)
            {
                return left.size_bytes < right.size_bytes ? -1 : 1;
            }
            return detail::CompareByName(left, right);

        case ShellItemSortColumn::DateModified:
            if (left.date_modified_available != right.date_modified_available)
            {
                return left.date_modified_available ? -1 : 1;
            }
            if (left.date_modified != right.date_modified)
            {
                return left.date_modified < right.date_modified ? -1 : 1;
            }
            return detail::CompareByName(left, right);
        }

        return detail::CompareByName(left, right);
    }

    inline void SortShellItems(
        std::vector<ShellItemRef>& items,
        ShellItemSortColumn const column,
        ShellItemSortDirection const direction)
    {
        std::stable_sort(
            items.begin(),
            items.end(),
            [column, direction](ShellItemRef const& left, ShellItemRef const& right)
            {
                auto const comparison = CompareShellItems(left, right, column);
                return direction == ShellItemSortDirection::Ascending
                    ? comparison < 0
                    : comparison > 0;
            });
    }
}
