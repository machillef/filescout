#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ShellItemSort.h"

using namespace FileScoutNative::Domain;

namespace
{
    ShellItemRef MakeItem(
        std::wstring displayName,
        std::wstring kindLabel,
        bool const isFolder = true)
    {
        ShellItemRef item;
        item.display_name = std::move(displayName);
        item.kind_label = std::move(kindLabel);
        item.is_folder = isFolder;
        return item;
    }
}

TEST(ShellItemSort, TypeDescending_UsesNameTieBreakForEqualTypes)
{
    std::vector<ShellItemRef> items{
        MakeItem(L"alpha", L"Folder"),
        MakeItem(L"charlie", L"Folder"),
        MakeItem(L"bravo", L"Folder"),
    };

    SortShellItems(
        items,
        ShellItemSortColumn::Type,
        ShellItemSortDirection::Descending);

    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].display_name, L"charlie");
    EXPECT_EQ(items[1].display_name, L"bravo");
    EXPECT_EQ(items[2].display_name, L"alpha");
}

TEST(ShellItemSort, DescendingComparator_RemainsFalseForEqualItems)
{
    auto const left = MakeItem(L"same", L"Folder");
    auto const right = MakeItem(L"same", L"Folder");

    auto const comparison =
        CompareShellItems(left, right, ShellItemSortColumn::Type);

    EXPECT_EQ(comparison, 0);
    EXPECT_FALSE(comparison > 0);
}
