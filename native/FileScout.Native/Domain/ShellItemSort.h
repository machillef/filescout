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

        inline int CompareByName(ShellItemRef const& left, ShellItemRef const& right)
        {
            return CompareCaseInsensitive(left.display_name, right.display_name);
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
        std::sort(
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
