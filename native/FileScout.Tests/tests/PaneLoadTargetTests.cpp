#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/PaneLoadTarget.h"

#include <filesystem>
#include <fstream>
#include <regex>
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

namespace
{
    // Text of one MainWindow member function, from its signature to its closing brace at member
    // indentation ("\n    }\n").
    std::string FunctionBody(std::string const& source, std::string const& signature)
    {
        auto const begin = source.find(signature);
        if (begin == std::string::npos)
        {
            return {};
        }
        auto const end = source.find("\n    }\n", begin);
        return source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }
}

// Source scan (no XAML runtime in gtest): the two functions that loaded the other pane by swapping
// the active index now name the pane they load and never assign the active index, whatever the
// saved variable would be called.
TEST(PaneLoadTarget, MainWindowNoLongerSwapsTheActivePaneToLoad)
{
    auto const path = std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path() /
        "FileScout.Native" / "MainWindow.xaml.cpp";
    std::ifstream stream{ path, std::ios::binary };
    ASSERT_TRUE(stream.is_open()) << path.string();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    // CI checks out with CRLF line endings; drop the CRs so the body delimiter matches either way.
    auto source = buffer.str();
    std::erase(source, '\r');

    std::regex const assignsActiveIndex{ R"(m_activePaneIndex\s*=[^=])" };
    for (auto const* signature : { "void MainWindow::InitPane(", "void MainWindow::ToggleDualPane(" })
    {
        auto const body = FunctionBody(source, signature);
        ASSERT_FALSE(body.empty()) << signature << " not found";
        EXPECT_NE(body.find("LoadDirectoryInPane("), std::string::npos) << signature;
        EXPECT_FALSE(std::regex_search(body, assignsActiveIndex)) << signature << " assigns m_activePaneIndex";
    }
}
