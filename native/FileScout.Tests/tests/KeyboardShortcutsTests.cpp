#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/KeyboardShortcuts.h"

#include <set>
#include <utility>

using namespace FileScoutNative::Domain;

// UB-04: F2 renamed the selected file while the user typed in the search box.
TEST(KeyboardShortcuts, FileCommands_YieldToFocusedTextInput)
{
    for (auto const* id : { L"rename", L"delete", L"selectAll", L"copy", L"cut", L"paste", L"properties" })
    {
        EXPECT_TRUE(ShouldTextInputOwnShortcut(id, /*focusInEditableControl=*/true)) << id;
        EXPECT_FALSE(ShouldTextInputOwnShortcut(id, /*focusInEditableControl=*/false)) << id;
    }
}

TEST(KeyboardShortcuts, WindowCommands_StillWorkFromTextInput)
{
    for (auto const* id : { L"refresh", L"back", L"forward", L"newTab", L"closeTab", L"switchPane", L"newFolder" })
    {
        EXPECT_FALSE(ShouldTextInputOwnShortcut(id, /*focusInEditableControl=*/true)) << id;
    }
}

TEST(KeyboardShortcuts, F2_IsBoundToRename)
{
    auto const* shortcut = FindFileShortcut(VK_F2, ShortcutModifiers::None);
    ASSERT_NE(shortcut, nullptr);
    EXPECT_EQ(shortcut->command_id, L"rename");
}

// UB-05: Ctrl+A was bound twice (accelerator and the list's PreviewKeyDown).
TEST(KeyboardShortcuts, EveryChordIsBoundOnce)
{
    std::set<std::pair<std::uint32_t, ShortcutModifiers>> seen;
    for (auto const& shortcut : kFileShortcuts)
    {
        EXPECT_TRUE(seen.emplace(shortcut.key, shortcut.modifiers).second)
            << "duplicate chord for " << std::string(shortcut.command_id.begin(), shortcut.command_id.end());
    }
    auto const* selectAll = FindFileShortcut('A', ShortcutModifiers::Control);
    ASSERT_NE(selectAll, nullptr);
    EXPECT_EQ(selectAll->command_id, L"selectAll");
}

TEST(KeyboardShortcuts, ListKeyHandler_LeavesCtrlAToTheAccelerator)
{
    EXPECT_EQ(ResolveListKeyAction('A', /*hasModifierChord=*/true), ListKeyAction::None);
    EXPECT_EQ(ResolveListKeyAction(VK_RETURN, /*hasModifierChord=*/false), ListKeyAction::ActivateSelection);
    EXPECT_EQ(ResolveListKeyAction(VK_RETURN, /*hasModifierChord=*/true), ListKeyAction::None);
    EXPECT_EQ(ResolveListKeyAction('A', /*hasModifierChord=*/false), ListKeyAction::TypeAhead);
}
