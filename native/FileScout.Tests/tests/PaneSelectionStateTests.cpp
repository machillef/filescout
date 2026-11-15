#include "pch.h"
#include <gtest/gtest.h>
#include <unordered_set>
#include "Domain/PaneSelectionState.h"

using namespace FileScoutNative::Domain;

// D-02: replacing the selection with an empty id clears it (no unselected focus/anchor left).
TEST(PaneSelectionState, ReplaceWithEmptyIdClears)
{
    PaneSelectionState state;
    state.ReplaceWith(std::wstring{ L"C:\\a.txt" });

    state.ReplaceWith(std::wstring{});

    EXPECT_TRUE(state.SelectedIds().empty());
    EXPECT_FALSE(state.FocusedId().has_value());
    EXPECT_FALSE(state.IsSelected(L""));
}

TEST(PaneSelectionState, ReplaceWithSingleIdSelectsAndFocusesIt)
{
    PaneSelectionState state;
    state.ReplaceWith(std::wstring{ L"C:\\a.txt" });

    EXPECT_EQ(std::vector<std::wstring>{ L"C:\\a.txt" }, state.SelectedIds());
    EXPECT_EQ(std::optional<std::wstring>{ L"C:\\a.txt" }, state.FocusedId());
}

// D-02: without a usable focus hint, focus falls back to the first id given, not to whatever an
// unordered_set iterates first; ids come back in the order they were given.
TEST(PaneSelectionState, FocusFallbackIsTheFirstIdGiven)
{
    std::vector<std::wstring> ids;
    for (int index = 40; index > 0; --index)
    {
        ids.push_back(L"C:\\dir\\item" + std::to_wstring(index) + L".txt");
    }

    PaneSelectionState state;
    state.ReplaceWith(std::span<std::wstring const>{ ids });
    EXPECT_EQ(std::optional<std::wstring>{ ids.front() }, state.FocusedId());
    EXPECT_EQ(ids, state.SelectedIds());

    state.ReplaceWith(std::span<std::wstring const>{ ids }, std::wstring{ L"C:\\not-selected" });
    EXPECT_EQ(std::optional<std::wstring>{ ids.front() }, state.FocusedId());

    state.ReplaceWith(std::span<std::wstring const>{ ids }, ids[7]);
    EXPECT_EQ(std::optional<std::wstring>{ ids[7] }, state.FocusedId());
}

TEST(PaneSelectionState, ReplaceWithSkipsEmptyAndDuplicateIds)
{
    std::vector<std::wstring> const ids{ L"b", L"", L"a", L"b" };

    PaneSelectionState state;
    state.ReplaceWith(std::span<std::wstring const>{ ids });

    EXPECT_EQ((std::vector<std::wstring>{ L"b", L"a" }), state.SelectedIds());
    EXPECT_EQ(std::optional<std::wstring>{ L"b" }, state.FocusedId());
}

TEST(PaneSelectionState, ReplaceWithOnlyEmptyIdsClears)
{
    std::vector<std::wstring> const ids{ L"", L"" };

    PaneSelectionState state;
    state.ReplaceWith(std::wstring{ L"x" });
    state.ReplaceWith(std::span<std::wstring const>{ ids });

    EXPECT_TRUE(state.SelectedIds().empty());
    EXPECT_FALSE(state.FocusedId().has_value());
}
