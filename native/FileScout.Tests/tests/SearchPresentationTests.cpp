#include "pch.h"
#include <gtest/gtest.h>
#include "Search/SearchPresentation.h"

using namespace FileScoutNative::Search;

TEST(SearchPresentation, InProgressStatusUsesMatchPluralization)
{
    EXPECT_EQ(BuildSearchProgressStatus(0), L"0 matches");
    EXPECT_EQ(BuildSearchProgressStatus(1), L"1 match");
    EXPECT_EQ(BuildSearchProgressStatus(12), L"12 matches");
}

TEST(SearchPresentation, CompleteStatusShowsRefineMessageWhenCapReached)
{
    EXPECT_EQ(
        BuildSearchCompleteStatus(200, LR"(C:\Work)", true),
        L"Showing top 200 matches - refine your query to narrow results");
}

TEST(SearchPresentation, CompleteStatusIncludesRootWhenCapNotReached)
{
    EXPECT_EQ(
        BuildSearchCompleteStatus(3, LR"(C:\Work\Docs)", false),
        LR"(3 matches in C:\Work\Docs)");
}

TEST(SearchPresentation, BuildSearchResultItemsCopiesFilenameAndPath)
{
    std::vector<SearchResult> results{
        SearchResult{ LR"(C:\Work\Docs\report.txt)", L"report.txt", 42 },
        SearchResult{ LR"(C:\Work\notes.md)", L"notes.md", 31 },
    };

    auto const items = BuildSearchResultItems(results, LR"(C:\Work)");

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].display_name, L"report.txt");
    EXPECT_EQ(items[0].path, std::filesystem::path{ LR"(C:\Work\Docs\report.txt)" });
    EXPECT_EQ(items[0].parsing_name, LR"(C:\Work\Docs\report.txt)");
    EXPECT_EQ(items[0].kind_label, LR"(Docs\)");
    EXPECT_FALSE(items[0].is_folder);
    EXPECT_EQ(items[1].display_name, L"notes.md");
    EXPECT_TRUE(items[1].kind_label.empty());
}

TEST(SearchPresentation, HighlightPositionsFollowFuzzySubsequenceOrder)
{
    auto const positions = FindSearchHighlightPositions(L"abc", L"axbxcx.txt");

    ASSERT_EQ(positions.size(), 3u);
    EXPECT_EQ(positions[0], static_cast<std::size_t>(0));
    EXPECT_EQ(positions[1], static_cast<std::size_t>(2));
    EXPECT_EQ(positions[2], static_cast<std::size_t>(4));
}

TEST(SearchPresentation, HighlightPositionsAreEmptyWhenQueryDoesNotMatch)
{
    EXPECT_TRUE(FindSearchHighlightPositions(L"zzz", L"document.txt").empty());
}

TEST(SearchPresentation, BuildOpenTargetUsesParentFolderAndSelectedPath)
{
    auto const target = TryBuildSearchOpenTarget(LR"(C:\Work\Docs\report.txt)");
    auto const expectedFolder = std::filesystem::path{ LR"(C:\Work\Docs)" };
    auto const expectedSelect = std::filesystem::path{ LR"(C:\Work\Docs\report.txt)" };

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->folder_path, expectedFolder);
    EXPECT_EQ(target->select_path, expectedSelect);
}

TEST(SearchPresentation, CompleteStatusNamesTheSharedResultLimit)
{
    EXPECT_EQ(
        L"Showing top " + std::to_wstring(k_max_search_results) +
            L" matches - refine your query to narrow results",
        BuildSearchCompleteStatus(5, L"C:\\Root", true));
}
