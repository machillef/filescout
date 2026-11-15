#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/PaneLoadTarget.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace FileScoutNative::Domain;

// UB-07: InitPane and ToggleDualPane switched m_activePaneIndex to load the other pane, so the shared
// toolbar and async continuations (selectAfter) acted on the wrong pane.
TEST(PaneLoadTarget, LoadingTheActivePane_UpdatesTheSharedToolbar)
{
    auto const updates = ChromeUpdatesForLoad(/*loadPaneIndex=*/0, /*activePaneIndex=*/0);
    EXPECT_TRUE(updates.toolbar);
    EXPECT_TRUE(updates.select_after);
}

TEST(PaneLoadTarget, LoadingTheOtherPane_LeavesTheSharedToolbarAlone)
{
    auto const updates = ChromeUpdatesForLoad(/*loadPaneIndex=*/1, /*activePaneIndex=*/0);
    EXPECT_FALSE(updates.toolbar);
    EXPECT_FALSE(updates.select_after);
}

TEST(PaneLoadTarget, PaneSwitchedDuringTheLoad_DropsSelectAfter)
{
    // Started in pane 0, the user switched to pane 1 before the rows arrived.
    EXPECT_FALSE(ChromeUpdatesForLoad(/*loadPaneIndex=*/0, /*activePaneIndex=*/1).select_after);
}

// Source scan (no XAML runtime in gtest): no code saves and overwrites the active pane index to load
// another pane any more; loads name their pane (LoadDirectoryInPane).
TEST(PaneLoadTarget, MainWindowNoLongerSwapsTheActivePaneToLoad)
{
    auto const path = std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path() /
        "FileScout.Native" / "MainWindow.xaml.cpp";
    std::ifstream stream{ path, std::ios::binary };
    ASSERT_TRUE(stream.is_open()) << path.string();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    auto const source = buffer.str();
    EXPECT_EQ(source.find("savedActivePane"), std::string::npos);
    EXPECT_NE(source.find("LoadDirectoryInPane(1,"), std::string::npos);
}
