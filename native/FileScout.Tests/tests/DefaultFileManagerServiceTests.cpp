#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/DefaultFileManagerService.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Shell::detail;

// --- BuildFolderCommandValue tests ---

TEST(DefaultFileManagerService, BuildFolderCommandValueQuotesPathAndAppendsPercentOne)
{
    auto const result = BuildFolderCommandValue(L"C:\\Program Files\\FileScout\\FileScout.Native.exe");

    EXPECT_EQ(result, L"\"C:\\Program Files\\FileScout\\FileScout.Native.exe\" \"%1\"");
}

TEST(DefaultFileManagerService, BuildFolderCommandValueHandlesSimplePath)
{
    auto const result = BuildFolderCommandValue(L"C:\\FileScout.exe");

    EXPECT_EQ(result, L"\"C:\\FileScout.exe\" \"%1\"");
}

// --- BuildWinECommandValue tests ---

TEST(DefaultFileManagerService, BuildWinECommandValueQuotesPathWithoutPercentOne)
{
    auto const result = BuildWinECommandValue(L"C:\\Program Files\\FileScout\\FileScout.Native.exe");

    EXPECT_EQ(result, L"\"C:\\Program Files\\FileScout\\FileScout.Native.exe\"");
}

// --- ValuePointsToFileScout tests ---

TEST(DefaultFileManagerService, ValuePointsToFileScoutReturnsTrueForExactMatch)
{
    EXPECT_TRUE(ValuePointsToFileScout(L"\"C:\\path\\FileScout.Native.exe\" \"%1\""));
}

TEST(DefaultFileManagerService, ValuePointsToFileScoutReturnsTrueForCaseInsensitiveMatch)
{
    EXPECT_TRUE(ValuePointsToFileScout(L"\"C:\\PATH\\FILESCOUT.NATIVE.EXE\" \"%1\""));
}

TEST(DefaultFileManagerService, ValuePointsToFileScoutReturnsFalseForExplorer)
{
    EXPECT_FALSE(ValuePointsToFileScout(L"%SystemRoot%\\explorer.exe"));
}

TEST(DefaultFileManagerService, ValuePointsToFileScoutReturnsFalseForEmpty)
{
    EXPECT_FALSE(ValuePointsToFileScout(L""));
}

TEST(DefaultFileManagerService, ValuePointsToFileScoutReturnsFalseForOtherApp)
{
    EXPECT_FALSE(ValuePointsToFileScout(L"\"C:\\Tools\\TotalCommander.exe\" \"%1\""));
}

// --- DeriveStatus tests ---

TEST(DefaultFileManagerService, DeriveStatusReturnsFullyDefaultWhenAllTrue)
{
    EXPECT_EQ(DeriveStatus(true, true, true), DefaultFileManagerStatus::FullyDefault);
}

TEST(DefaultFileManagerService, DeriveStatusReturnsNotDefaultWhenAllFalse)
{
    EXPECT_EQ(DeriveStatus(false, false, false), DefaultFileManagerStatus::NotDefault);
}

TEST(DefaultFileManagerService, DeriveStatusReturnsPartiallyDefaultWhenMixed)
{
    EXPECT_EQ(DeriveStatus(true, false, false), DefaultFileManagerStatus::PartiallyDefault);
    EXPECT_EQ(DeriveStatus(true, true, false), DefaultFileManagerStatus::PartiallyDefault);
    EXPECT_EQ(DeriveStatus(false, true, true), DefaultFileManagerStatus::PartiallyDefault);
}
