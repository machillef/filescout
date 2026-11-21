#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/PaneLoadTarget.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace FileScoutNative::Domain;

TEST(PaneLoadTarget, LoadingTheActivePane_UpdatesTheSharedToolbar)
{
    auto const updates = ChromeUpdatesForLoad(0, 0);
    EXPECT_TRUE(updates.toolbar);
    EXPECT_TRUE(updates.select_after);
}

TEST(PaneLoadTarget, LoadingTheOtherPane_LeavesTheSharedToolbarAlone)
{
    auto const updates = ChromeUpdatesForLoad(1, 0);
    EXPECT_FALSE(updates.toolbar);
    EXPECT_FALSE(updates.select_after);
}

TEST(PaneLoadTarget, PaneSwitchedDuringTheLoad_DropsSelectAfter)
{
    EXPECT_FALSE(ChromeUpdatesForLoad(0, 1).select_after);
}

namespace
{
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
