#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/NavigationHistory.h"

using namespace FileScoutNative::Domain;
using std::filesystem::path;

TEST(NavigationHistory, NavigatingAway_PushesThePreviousFolder)
{
    std::vector<path> back;
    std::vector<path> forward{ L"C:\\Later" };
    RecordNavigation(back, forward, L"C:\\Users", L"C:\\Users\\Docs");
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back.back(), path{ L"C:\\Users" });
    EXPECT_TRUE(forward.empty());
}

TEST(NavigationHistory, RecycleBin_BackReturnsToThePreviousFolder)
{
    // UB-12: the Recycle Bin click pushed the NEW path, so Back stayed in the Recycle Bin.
    std::vector<path> back;
    std::vector<path> forward;
    RecordNavigation(back, forward, L"C:\\Users\\Docs", path{ kRecycleBinLocation });
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back.back(), path{ L"C:\\Users\\Docs" });
}

TEST(NavigationHistory, EmptyCurrent_PushesNothing)
{
    std::vector<path> back;
    std::vector<path> forward;
    RecordNavigation(back, forward, path{}, L"C:\\");
    EXPECT_TRUE(back.empty());
}

TEST(NavigationHistory, ReloadingTheSameFolder_PushesNothing)
{
    std::vector<path> back;
    std::vector<path> forward{ L"C:\\Later" };
    RecordNavigation(back, forward, L"C:\\Users\\Docs", L"c:\\users\\docs");
    EXPECT_TRUE(back.empty());
    EXPECT_EQ(forward.size(), 1u);
}

TEST(NavigationHistory, RecycleBinLocation_IsRecognisedRawOrAbsolutised)
{
    EXPECT_TRUE(IsRecycleBinLocation(path{ kRecycleBinLocation }));
    EXPECT_TRUE(IsRecycleBinLocation(L"C:\\Program Files\\FileScout\\shell:RecycleBinFolder"));
    EXPECT_TRUE(IsRecycleBinLocation(L"SHELL:recyclebinfolder"));
    EXPECT_FALSE(IsRecycleBinLocation(L"C:\\Users\\Docs"));
    EXPECT_FALSE(IsRecycleBinLocation(path{}));
}
