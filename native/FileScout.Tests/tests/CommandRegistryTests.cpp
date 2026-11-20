#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/CommandRegistry.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Domain;

namespace
{
    bool HasCommandId(
        std::vector<FileScoutNative::Domain::CommandEntry> const& commands,
        std::wstring_view const id)
    {
        return std::any_of(commands.begin(), commands.end(),
            [id](auto const& cmd) { return cmd.id == id; });
    }
}

TEST(CommandRegistry, AllReturnsNonEmpty)
{
    CommandRegistry reg;
    EXPECT_FALSE(reg.All().empty());
}

TEST(CommandRegistry, EmptyQueryReturnsAll)
{
    CommandRegistry reg;
    auto results = reg.Filter(L"");
    EXPECT_EQ(results.size(), reg.All().size());
}

TEST(CommandRegistry, FilterMatchesByDisplayName)
{
    CommandRegistry reg;
    // "new" should match "New Tab" — verify at least one result with id "newTab"
    auto results = reg.Filter(L"ne");
    ASSERT_FALSE(results.empty());
    bool found = false;
    for (auto const& cmd : results)
        if (cmd.id == L"newTab") found = true;
    EXPECT_TRUE(found);
}

TEST(CommandRegistry, FilterReturnsEmptyForNoMatch)
{
    CommandRegistry reg;
    auto results = reg.Filter(L"zzzzzz");
    EXPECT_TRUE(results.empty());
}

TEST(CommandRegistry, FilterIsCaseInsensitive)
{
    CommandRegistry reg;
    auto lower = reg.Filter(L"rename");
    auto upper = reg.Filter(L"RENAME");
    ASSERT_FALSE(lower.empty());
    ASSERT_FALSE(upper.empty());
    EXPECT_EQ(lower.front().id, upper.front().id);
}

TEST(CommandRegistry, FilterResultsAreSubsetOfAll)
{
    CommandRegistry reg;
    auto results = reg.Filter(L"re");
    for (auto const& r : results)
    {
        bool found = false;
        for (auto const& a : reg.All())
            if (a.id == r.id) { found = true; break; }
        EXPECT_TRUE(found) << "Filtered result id not in All(): " << r.id.c_str();
    }
}

TEST(CommandRegistry, EachCommandHasNonEmptyIdAndDisplayName)
{
    CommandRegistry reg;
    for (auto const& cmd : reg.All())
    {
        EXPECT_FALSE(cmd.id.empty())           << "Command has empty id";
        EXPECT_FALSE(cmd.display_name.empty()) << "Command has empty display_name";
    }
}

// R13b: all navigation commands wired in this slice must exist in the registry.
TEST(CommandRegistry, AllNavigationCommandsPresent)
{
    CommandRegistry reg;
    std::vector<std::wstring> const required = {
        L"newTab", L"closeTab", L"switchPane",
        L"refresh", L"back", L"forward", L"goToPath"
    };
    for (auto const& id : required)
    {
        bool found = false;
        for (auto const& cmd : reg.All())
            if (cmd.id == id) { found = true; break; }
        EXPECT_TRUE(found) << "Missing navigation command id: " << id.c_str();
    }
}

// R13d: all view and sort commands must exist in the registry.
TEST(CommandRegistry, AllViewAndSortCommandsPresent)
{
    CommandRegistry reg;
    std::vector<std::wstring> const required = {
        L"toggleView", L"toggleGroup",
        L"sortByName", L"sortBySize", L"sortByDateModified"
    };
    for (auto const& id : required)
    {
        bool found = false;
        for (auto const& cmd : reg.All())
            if (cmd.id == id) { found = true; break; }
        EXPECT_TRUE(found) << "Missing view/sort command id: " << id.c_str();
    }
}

// R13c: all file operation commands must exist in the registry.
TEST(CommandRegistry, AllFileOperationCommandsPresent)
{
    CommandRegistry reg;
    std::vector<std::wstring> const required = {
        L"copy", L"cut", L"paste", L"delete", L"permanentDelete", L"rename",
        L"newFolder", L"open", L"openWith", L"properties", L"selectAll"
    };
    for (auto const& id : required)
    {
        bool found = false;
        for (auto const& cmd : reg.All())
            if (cmd.id == id) { found = true; break; }
        EXPECT_TRUE(found) << "Missing file operation command id: " << id.c_str();
    }
}

TEST(CommandRegistry, ExplorerStyleShortcutsAreExposed)
{
    CommandRegistry reg;
    std::vector<std::pair<std::wstring, std::wstring>> const required = {
        { L"selectAll", L"Ctrl+A" },
        { L"permanentDelete", L"Shift+Del" },
        { L"newFolder", L"Ctrl+Shift+N" },
        { L"properties", L"Alt+Enter" },
        { L"back", L"Alt+Left" },
        { L"forward", L"Alt+Right" }
    };

    for (auto const& [id, shortcut] : required)
    {
        auto const it = std::find_if(reg.All().begin(), reg.All().end(),
            [&id](auto const& cmd) { return cmd.id == id; });
        ASSERT_NE(it, reg.All().end()) << "Missing command id: " << id.c_str();
        EXPECT_EQ(it->shortcut_text, shortcut) << "Unexpected shortcut text for " << id.c_str();
    }
}

TEST(CommandRegistry, ClosedBetaDoesNotAdvertiseGatedFeatures)
{
    CommandRegistry reg;

    EXPECT_FALSE(HasCommandId(reg.All(), L"commandPalette"));
    EXPECT_FALSE(HasCommandId(reg.All(), L"batchRename"));
    EXPECT_FALSE(HasCommandId(reg.All(), L"browseArchive"));
    EXPECT_FALSE(HasCommandId(reg.Filter(L"palette"), L"commandPalette"));
    EXPECT_FALSE(HasCommandId(reg.Filter(L"batch"), L"batchRename"));
    EXPECT_FALSE(HasCommandId(reg.Filter(L"archive"), L"browseArchive"));
}

// R13e: panel toggle commands must exist in the registry.
TEST(CommandRegistry, AllPanelToggleCommandsPresent)
{
    CommandRegistry reg;
    std::vector<std::wstring> const required = {
        L"togglePreview", L"toggleDualPane"
    };
    for (auto const& id : required)
    {
        bool found = false;
        for (auto const& cmd : reg.All())
            if (cmd.id == id) { found = true; break; }
        EXPECT_TRUE(found) << "Missing panel toggle command id: " << id.c_str();
    }
}

// D-08: commands with the same fuzzy score keep their registration order.
TEST(CommandRegistry, RankByScore_KeepsRegistrationOrderForTies)
{
    std::vector<std::pair<int, CommandEntry>> scored;
    for (int index = 0; index < 100; ++index)
    {
        CommandEntry entry;
        entry.id = L"cmd" + std::to_wstring(index);
        scored.emplace_back(index % 3, std::move(entry));
    }

    auto const ranked = RankCommandsByScore(std::move(scored));

    ASSERT_EQ(ranked.size(), 100u);
    std::vector<std::wstring> expected;
    for (int score = 2; score >= 0; --score)
    {
        for (int index = 0; index < 100; ++index)
        {
            if (index % 3 == score)
            {
                expected.push_back(L"cmd" + std::to_wstring(index));
            }
        }
    }
    std::vector<std::wstring> actual;
    for (auto const& entry : ranked)
    {
        actual.push_back(entry.id);
    }
    EXPECT_EQ(expected, actual);
}
