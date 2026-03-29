#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/FileRowViewModel.h"
#include "Domain/RowFormatting.h"

using namespace FileScoutNative::Domain;

// ---------------------------------------------------------------------------
// FormatFileSize — boundary tests
// ---------------------------------------------------------------------------

TEST(RowFormatting, FormatFileSize_Zero)
{
    EXPECT_EQ(L"0 B", FormatFileSize(0));
}

TEST(RowFormatting, FormatFileSize_FewBytes)
{
    EXPECT_EQ(L"512 B", FormatFileSize(512));
}

TEST(RowFormatting, FormatFileSize_OneKilobyte)
{
    // 1024 bytes → 1.0 KB (value < 10, so 1 decimal place)
    EXPECT_EQ(L"1.0 KB", FormatFileSize(1024));
}

TEST(RowFormatting, FormatFileSize_LargeKilobytes)
{
    // 11 KB — value >= 10 so 0 decimal places
    EXPECT_EQ(L"11 KB", FormatFileSize(11 * 1024));
}

TEST(RowFormatting, FormatFileSize_Megabytes)
{
    EXPECT_EQ(L"2.0 MB", FormatFileSize(2ULL * 1024 * 1024));
}

TEST(RowFormatting, FormatFileSize_Gigabytes)
{
    EXPECT_EQ(L"3.0 GB", FormatFileSize(3ULL * 1024 * 1024 * 1024));
}

// ---------------------------------------------------------------------------
// BuildDetailText — card-mode detail chip
// ---------------------------------------------------------------------------

TEST(RowFormatting, BuildDetailText_NoSize_TypeLabelOnly)
{
    // Folders have no size — show only the type label
    EXPECT_EQ(L"Folder", BuildDetailText(L"Folder", 0, false));
}

TEST(RowFormatting, BuildDetailText_WithSize_TypeAndSize)
{
    // File with size: "TypeLabel  |  FormattedSize"
    EXPECT_EQ(L"PDF Document  |  1.0 KB", BuildDetailText(L"PDF Document", 1024, true));
}

TEST(FileRowViewModel, GroupHeaderHiddenByDefault)
{
    auto const vm = winrt::make_self<winrt::FileScoutNative::implementation::FileRowViewModel>()
        .as<winrt::FileScoutNative::FileRowViewModel>();

    EXPECT_EQ(L"", std::wstring{ vm.GroupHeaderText() });
    EXPECT_DOUBLE_EQ(0.0, vm.GroupHeaderOpacity());
    EXPECT_DOUBLE_EQ(0.0, vm.GroupHeaderHeight());
}

TEST(FileRowViewModel, GroupHeaderVisibleWhenConfigured)
{
    auto const impl = winrt::make_self<winrt::FileScoutNative::implementation::FileRowViewModel>();
    impl->SetGroupHeader(L"Folders", true);
    auto const vm = impl.as<winrt::FileScoutNative::FileRowViewModel>();

    EXPECT_EQ(L"Folders", std::wstring{ vm.GroupHeaderText() });
    EXPECT_DOUBLE_EQ(1.0, vm.GroupHeaderOpacity());
    EXPECT_DOUBLE_EQ(42.0, vm.GroupHeaderHeight());
}
