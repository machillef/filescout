#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/SidebarIconInfo.h"

using namespace FileScoutNative::Domain;

TEST(SidebarIconInfo, KnownFolderGlyph_ReturnsExpectedCodepoints)
{
    EXPECT_EQ(KnownFolderGlyph(L"Home"), L"\uE80F");
    EXPECT_EQ(KnownFolderGlyph(L"Desktop"), L"\uEC64");
    EXPECT_EQ(KnownFolderGlyph(L"Documents"), L"\uE8A5");
    EXPECT_EQ(KnownFolderGlyph(L"Downloads"), L"\uE896");
    EXPECT_EQ(KnownFolderGlyph(L"Pictures"), L"\uEB9F");
    EXPECT_EQ(KnownFolderGlyph(L"Music"), L"\uEC4F");
    EXPECT_EQ(KnownFolderGlyph(L"Videos"), L"\uE8B2");
    EXPECT_EQ(KnownFolderGlyph(L"This PC"), L"\uEDA2");
    EXPECT_EQ(KnownFolderGlyph(L"Recycle Bin"), L"\uE74D");
    EXPECT_EQ(KnownFolderGlyph(L"desktop"), L"\uEC64");
    EXPECT_TRUE(KnownFolderGlyph(L"Projects").empty());
}

TEST(SidebarIconInfo, DriveTypeGlyph_ReturnsExpectedCodepoints)
{
    EXPECT_EQ(DriveTypeGlyph(DRIVE_FIXED), L"\uEDA2");
    EXPECT_EQ(DriveTypeGlyph(DRIVE_REMOVABLE), L"\uE88E");
    EXPECT_EQ(DriveTypeGlyph(DRIVE_REMOTE), L"\uEDA3");
    EXPECT_EQ(DriveTypeGlyph(DRIVE_CDROM), L"\uEDA2");
    EXPECT_EQ(DriveTypeGlyph(DRIVE_RAMDISK), L"\uEDA2");
    EXPECT_EQ(DriveTypeGlyph(DRIVE_UNKNOWN), L"\uEDA2");
}
