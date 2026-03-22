#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/CommandRegistry.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Domain;

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

// R13c: all file operation commands must exist in the registry.
TEST(CommandRegistry, AllFileOperationCommandsPresent)
{
    CommandRegistry reg;
    std::vector<std::wstring> const required = {
        L"copy", L"cut", L"paste", L"delete", L"rename",
        L"newFolder", L"open", L"openWith", L"properties"
    };
    for (auto const& id : required)
    {
        bool found = false;
        for (auto const& cmd : reg.All())
            if (cmd.id == id) { found = true; break; }
        EXPECT_TRUE(found) << "Missing file operation command id: " << id.c_str();
    }
}
