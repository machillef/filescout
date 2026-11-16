#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/SidebarRecents.h"

using namespace FileScoutNative::Domain;

TEST(SidebarRecents, EmptyBackStack_ReturnsEmpty)
{
    std::vector<std::filesystem::path> const empty;
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(empty, noExclusions, 5);
    EXPECT_TRUE(result.empty());
}

TEST(SidebarRecents, AllCandidatesExcluded_ReturnsEmpty)
{
    std::vector<std::filesystem::path> const candidates{
        L"C:\\Users\\test\\Documents",
        L"C:\\Users\\test\\Desktop",
    };
    std::unordered_set<std::wstring> const excluded{
        SidebarPathKey(L"C:\\Users\\test\\Documents"),
        SidebarPathKey(L"C:\\Users\\test\\Desktop"),
    };
    auto const result = SelectRecentPaths(candidates, excluded, 5);
    EXPECT_TRUE(result.empty());
}

TEST(SidebarRecents, CapAtMax)
{
    std::vector<std::filesystem::path> candidates;
    for (int i = 0; i < 8; ++i)
        candidates.push_back(L"C:\\folder" + std::to_wstring(i));
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(candidates, noExclusions, 5);
    EXPECT_EQ(result.size(), 5u);
}

TEST(SidebarRecents, DedupsDuplicatePaths)
{
    // back_stack contains repeated paths — each unique path returned once
    std::vector<std::filesystem::path> const candidates{
        L"C:\\Work", L"C:\\Work", L"C:\\Projects", L"C:\\Work",
    };
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(candidates, noExclusions, 5);
    EXPECT_EQ(result.size(), 2u);
}

TEST(SidebarRecents, MostRecentFirst)
{
    // back_stack is oldest-first, newest-last — results must be newest-first
    std::vector<std::filesystem::path> const candidates{
        L"C:\\Oldest", L"C:\\Middle", L"C:\\Newest",
    };
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(candidates, noExclusions, 5);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], std::filesystem::path(L"C:\\Newest"));
    EXPECT_EQ(result[1], std::filesystem::path(L"C:\\Middle"));
    EXPECT_EQ(result[2], std::filesystem::path(L"C:\\Oldest"));
}

TEST(SidebarRecents, CaseInsensitiveExclusion)
{
    // excluded key generated from uppercase path; candidate uses lowercase — must be excluded
    std::vector<std::filesystem::path> const candidates{ L"C:\\users\\test\\desktop" };
    std::unordered_set<std::wstring> const excluded{
        SidebarPathKey(L"C:\\Users\\Test\\Desktop"),
    };
    auto const result = SelectRecentPaths(candidates, excluded, 5);
    EXPECT_TRUE(result.empty());
}

TEST(SidebarRecents, EmptyPathsInCandidates_Skipped)
{
    std::vector<std::filesystem::path> const candidates{
        L"", L"C:\\Folder1", L"", L"C:\\Folder2",
    };
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(candidates, noExclusions, 5);
    EXPECT_EQ(result.size(), 2u);
}

// UB-11: recents read only the left pane's back stack. They now come from one window-wide list of
// folders the user left, in either pane.
TEST(SidebarRecents, RecordRecentFolder_MergesBothPanesNewestLast)
{
    std::vector<std::filesystem::path> recents;
    RecordRecentFolder(recents, L"C:\\LeftPane\\A", 16);   // left pane navigates away from A
    RecordRecentFolder(recents, L"D:\\RightPane\\B", 16);  // right pane navigates away from B
    std::unordered_set<std::wstring> const noExclusions;
    auto const result = SelectRecentPaths(recents, noExclusions, 5);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], std::filesystem::path(L"D:\\RightPane\\B"));
    EXPECT_EQ(result[1], std::filesystem::path(L"C:\\LeftPane\\A"));
}

TEST(SidebarRecents, RecordRecentFolder_RevisitMovesToNewestWithoutDuplicate)
{
    std::vector<std::filesystem::path> recents;
    RecordRecentFolder(recents, L"C:\\A", 16);
    RecordRecentFolder(recents, L"C:\\B", 16);
    RecordRecentFolder(recents, L"c:\\a", 16);
    ASSERT_EQ(recents.size(), 2u);
    EXPECT_EQ(recents.back(), std::filesystem::path(L"c:\\a"));
}

TEST(SidebarRecents, RecordRecentFolder_DropsOldestAtCapacityAndIgnoresEmpty)
{
    std::vector<std::filesystem::path> recents;
    RecordRecentFolder(recents, L"C:\\1", 2);
    RecordRecentFolder(recents, L"C:\\2", 2);
    RecordRecentFolder(recents, L"", 2);
    RecordRecentFolder(recents, L"C:\\3", 2);
    ASSERT_EQ(recents.size(), 2u);
    EXPECT_EQ(recents.front(), std::filesystem::path(L"C:\\2"));
    EXPECT_EQ(recents.back(), std::filesystem::path(L"C:\\3"));
}

// A recursive search rooted at a folder shows results from its subfolders, so a file operation
// in a subfolder must refresh that search (S-03). Path keys are SidebarPathKey values.
TEST(SidebarRecents, DescendantPathKeyMatchesOnlyWholeSegmentsUnderTheRoot)
{
    using FileScoutNative::Domain::IsSameOrDescendantPathKey;
    using FileScoutNative::Domain::SidebarPathKey;
    auto const root = SidebarPathKey(L"C:\\Root");
    EXPECT_TRUE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\Root"), root));
    EXPECT_TRUE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\root\\Child"), root));
    EXPECT_TRUE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\Root\\Child\\Deep"), root));
    EXPECT_FALSE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\RootOther"), root));
    EXPECT_FALSE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\"), root));
    EXPECT_TRUE(IsSameOrDescendantPathKey(SidebarPathKey(L"C:\\Root"), SidebarPathKey(L"C:\\")));
}
