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

TEST(NavigationHistory, TrailingSeparatorOnTheSameFolder_ChangesNeitherStack)
{
    std::vector<std::filesystem::path> back{ L"C:\\A" };
    std::vector<std::filesystem::path> forward{ L"C:\\F" };

    FileScoutNative::Domain::RecordNavigation(back, forward, L"C:\\Users\\Docs", L"C:\\Users\\Docs\\");

    EXPECT_EQ(back, (std::vector<std::filesystem::path>{ L"C:\\A" }));
    EXPECT_EQ(forward, (std::vector<std::filesystem::path>{ L"C:\\F" }));
}

TEST(NavigationHistory, RootIsNotTrimmed)
{
    EXPECT_EQ(FileScoutNative::Domain::NormalizeFolderForCompare(L"C:\\"), std::filesystem::path{ L"C:\\" });
    EXPECT_EQ(FileScoutNative::Domain::NormalizeFolderForCompare(L"C:\\Users\\"), std::filesystem::path{ L"C:\\Users" });
}

// Recycle Bin opened from A, then folder B chosen before the bin's listing arrived: Back from B
// must return to the Recycle Bin, not to A.
TEST(NavigationHistory, NavigationBeforeThePreviousLoadFinishes_RecordsThePendingTarget)
{
    std::vector<std::filesystem::path> back;
    std::vector<std::filesystem::path> forward;
    std::filesystem::path current{ L"C:\\A" };
    std::filesystem::path const recycleBin{ L"shell:RecycleBinFolder" };

    FileScoutNative::Domain::BeginNavigation(back, forward, current, recycleBin, false);
    FileScoutNative::Domain::BeginNavigation(back, forward, current, L"C:\\B", false);

    EXPECT_EQ(back, (std::vector<std::filesystem::path>{ L"C:\\A", recycleBin }));
    EXPECT_EQ(current, std::filesystem::path{ L"C:\\B" });
}

TEST(NavigationHistory, HistoryStep_MovesWithoutRecording)
{
    std::vector<std::filesystem::path> back{ L"C:\\A" };
    std::vector<std::filesystem::path> forward;
    std::filesystem::path current{ L"C:\\B" };

    FileScoutNative::Domain::BeginNavigation(back, forward, current, L"C:\\A", true);

    EXPECT_EQ(back.size(), 1u);
    EXPECT_EQ(current, std::filesystem::path{ L"C:\\A" });
}

// Recents record the folder being left (Back/Forward included), not the destination.
TEST(NavigationHistory, RecentsRecordTheFolderLeft_ToAnotherFolderAndToTheRecycleBin)
{
    using FileScoutNative::Domain::FolderToRecordOnLeave;
    std::filesystem::path const recycleBin{ L"shell:RecycleBinFolder" };

    EXPECT_EQ(FolderToRecordOnLeave(L"C:\\A", L"C:\\B"), std::filesystem::path{ L"C:\\A" });
    EXPECT_EQ(FolderToRecordOnLeave(L"C:\\A", recycleBin), std::filesystem::path{ L"C:\\A" });
    EXPECT_FALSE(FolderToRecordOnLeave(recycleBin, L"C:\\B").has_value());
    EXPECT_FALSE(FolderToRecordOnLeave(L"C:\\A", L"C:\\A\\").has_value());
    EXPECT_FALSE(FolderToRecordOnLeave(L"", L"C:\\B").has_value());
    // To This PC (no path): the folder left is recorded.
    EXPECT_EQ(FolderToRecordOnLeave(L"C:\\A", std::filesystem::path{}), std::filesystem::path{ L"C:\\A" });
}
