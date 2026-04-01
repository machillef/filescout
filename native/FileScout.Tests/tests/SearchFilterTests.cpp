#include "pch.h"
#include <gtest/gtest.h>
#include "Search/SearchFilter.h"

using namespace FileScoutNative::Domain;
using namespace FileScoutNative::Search;

namespace
{
    ShellItemRef MakeSearchItem(std::wstring_view const name)
    {
        ShellItemRef item;
        item.display_name = std::wstring{ name };
        item.path = std::filesystem::path{ item.display_name };
        item.parsing_name = item.path.wstring();
        return item;
    }
}

TEST(SearchFilter, ShortQueryReturnsSnapshotOrderUnfiltered)
{
    std::vector<ShellItemRef> snapshot{
        MakeSearchItem(L"gamma.txt"),
        MakeSearchItem(L"alpha.txt"),
        MakeSearchItem(L"beta.txt"),
    };

    auto const results = BuildFilterResults(L"a", snapshot);

    ASSERT_EQ(results.size(), snapshot.size());
    EXPECT_EQ(results[0]->display_name, L"gamma.txt");
    EXPECT_EQ(results[1]->display_name, L"alpha.txt");
    EXPECT_EQ(results[2]->display_name, L"beta.txt");
}

TEST(SearchFilter, RanksHigherScoresAheadOfLowerScores)
{
    std::vector<ShellItemRef> snapshot{
        MakeSearchItem(L"cat.txt"),
        MakeSearchItem(L"bobcat.txt"),
        MakeSearchItem(L"cxaxt.txt"),
    };

    auto const results = BuildFilterResults(L"cat", snapshot);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0]->display_name, L"cat.txt");
    EXPECT_EQ(results[1]->display_name, L"bobcat.txt");
    EXPECT_EQ(results[2]->display_name, L"cxaxt.txt");
}

TEST(SearchFilter, DropsNonMatchingEntries)
{
    std::vector<ShellItemRef> snapshot{
        MakeSearchItem(L"report.txt"),
        MakeSearchItem(L"notes.md"),
        MakeSearchItem(L"summary.docx"),
    };

    auto const results = BuildFilterResults(L"rt", snapshot);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->display_name, L"report.txt");
}

TEST(SearchFilter, PreservesSnapshotOrderForScoreTies)
{
    std::vector<ShellItemRef> snapshot{
        MakeSearchItem(L"alpha-one.txt"),
        MakeSearchItem(L"alpha-two.txt"),
        MakeSearchItem(L"alphabet.txt"),
    };

    auto const results = BuildFilterResults(L"alp", snapshot);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0]->display_name, L"alpha-one.txt");
    EXPECT_EQ(results[1]->display_name, L"alpha-two.txt");
    EXPECT_EQ(results[2]->display_name, L"alphabet.txt");
}

TEST(SearchFilter, LimitsLongResultSetsToTopTwoHundred)
{
    std::vector<ShellItemRef> snapshot;
    snapshot.reserve(250);

    for (int index = 0; index < 250; ++index)
    {
        snapshot.push_back(MakeSearchItem(L"alpha-" + std::to_wstring(index) + L".txt"));
    }

    auto const results = BuildFilterResults(L"alp", snapshot);

    ASSERT_EQ(results.size(), 200u);
    EXPECT_EQ(results.front()->display_name, L"alpha-0.txt");
    EXPECT_EQ(results.back()->display_name, L"alpha-199.txt");
}
