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

namespace
{
    std::vector<ShellItemRef> MakeTiedFiles()
    {
        std::vector<ShellItemRef> items;
        std::wstring const names[] = { L"Same.txt", L"same.txt", L"SAME.txt" };
        for (int index = 0; index < 120; ++index)
        {
            ShellItemRef item;
            item.display_name = names[index % 3];
            item.path = std::filesystem::path{ L"C:\\d" + std::to_wstring(index) } / item.display_name;
            item.parsing_name = item.path.wstring();
            item.kind_label = L"Text Document";
            item.size_available = true;
            item.size_bytes = 4096;
            items.push_back(std::move(item));
        }
        return items;
    }

    std::vector<std::wstring> SortedPaths(
        std::vector<ShellItemRef> items,
        ShellItemSortColumn const column,
        ShellItemSortDirection const direction)
    {
        SortShellItems(items, column, direction);
        std::vector<std::wstring> paths;
        for (auto const& item : items)
        {
            paths.push_back(item.path.wstring());
        }
        return paths;
    }
}

TEST(ShellItemSort, TiedKeys_GiveTheSameOrderWhateverTheInputOrder)
{
    auto const forward = MakeTiedFiles();
    auto reversed = forward;
    std::reverse(reversed.begin(), reversed.end());

    for (auto const column : { ShellItemSortColumn::Name, ShellItemSortColumn::Type,
                               ShellItemSortColumn::Size, ShellItemSortColumn::DateModified })
    {
        for (auto const direction : { ShellItemSortDirection::Ascending, ShellItemSortDirection::Descending })
        {
            EXPECT_EQ(SortedPaths(forward, column, direction), SortedPaths(reversed, column, direction))
                << "column " << static_cast<int>(column) << " direction " << static_cast<int>(direction);
        }
    }
}

TEST(ShellItemSort, TiedCaseInsensitiveNames_BreakTiesOrdinally)
{
    std::vector<ShellItemRef> items;
    for (auto const* name : { L"same.txt", L"SAME.txt", L"Same.txt" })
    {
        ShellItemRef item;
        item.display_name = name;
        item.path = std::filesystem::path{ L"C:\\x" } / name;
        items.push_back(std::move(item));
    }

    SortShellItems(items, ShellItemSortColumn::Name, ShellItemSortDirection::Ascending);

    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].display_name, L"SAME.txt");
    EXPECT_EQ(items[1].display_name, L"Same.txt");
    EXPECT_EQ(items[2].display_name, L"same.txt");
}
