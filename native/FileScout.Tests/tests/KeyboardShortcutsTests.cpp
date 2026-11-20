#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/KeyboardShortcuts.h"

#include <set>
#include <utility>

using namespace FileScoutNative::Domain;

// UB-04: F2 renamed the selected file while the user typed in the search box.
TEST(KeyboardShortcuts, FileCommands_YieldToFocusedTextInput)
{
    for (auto const* id : { L"rename", L"delete", L"permanentDelete", L"navigateUpBackspace", L"copyPath", L"selectAll", L"copy", L"cut", L"paste", L"properties" })
    {
        EXPECT_TRUE(ShouldTextInputOwnShortcut(id, /*focusInEditableControl=*/true)) << id;
        EXPECT_FALSE(ShouldTextInputOwnShortcut(id, /*focusInEditableControl=*/false)) << id;
    }
}

TEST(KeyboardShortcuts, WindowCommands_StillWorkFromTextInput)
{
    for (auto const* id : { L"refresh", L"back", L"forward", L"navigateUp", L"newTab", L"reopenClosedTab", L"closeTab", L"nextTab", L"prevTab", L"toggleHiddenFiles", L"switchPane", L"newFolder" })
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

TEST(KeyboardShortcuts, ShiftDelete_IsBoundToPermanentDelete)
{
    auto const* recycleShortcut = FindFileShortcut(VK_DELETE, ShortcutModifiers::None);
    ASSERT_NE(recycleShortcut, nullptr);
    EXPECT_EQ(recycleShortcut->command_id, L"delete");

    auto const* permanentShortcut = FindFileShortcut(VK_DELETE, ShortcutModifiers::Shift);
    ASSERT_NE(permanentShortcut, nullptr);
    EXPECT_EQ(permanentShortcut->command_id, L"permanentDelete");
}

TEST(KeyboardShortcuts, ExplorerNavigationAndTabShortcutsAreBound)
{
    auto const* altUp = FindFileShortcut(VK_UP, ShortcutModifiers::Menu);
    ASSERT_NE(altUp, nullptr);
    EXPECT_EQ(altUp->command_id, L"navigateUp");

    auto const* backspace = FindFileShortcut(VK_BACK, ShortcutModifiers::None);
    ASSERT_NE(backspace, nullptr);
    EXPECT_EQ(backspace->command_id, L"navigateUpBackspace");

    auto const* copyPath = FindFileShortcut('C', ShortcutModifiers::Control | ShortcutModifiers::Shift);
    ASSERT_NE(copyPath, nullptr);
    EXPECT_EQ(copyPath->command_id, L"copyPath");

    auto const* reopenTab = FindFileShortcut('T', ShortcutModifiers::Control | ShortcutModifiers::Shift);
    ASSERT_NE(reopenTab, nullptr);
    EXPECT_EQ(reopenTab->command_id, L"reopenClosedTab");

    auto const* toggleHidden = FindFileShortcut('H', ShortcutModifiers::Control);
    ASSERT_NE(toggleHidden, nullptr);
    EXPECT_EQ(toggleHidden->command_id, L"toggleHiddenFiles");
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
