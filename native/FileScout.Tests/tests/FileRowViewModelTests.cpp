#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/FileRowViewModel.h"
#include "Domain/RowFormatting.h"

using namespace FileScoutNative::Domain;

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
    EXPECT_EQ(L"1.0 KB", FormatFileSize(1024));
}

TEST(RowFormatting, FormatFileSize_LargeKilobytes)
{
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

TEST(RowFormatting, FormatFileSizePreservesRoundingAndUnitBoundaries)
{
    constexpr auto kb = 1024ULL;
    constexpr auto mb = kb * kb;
    constexpr auto tb = mb * mb;
    EXPECT_EQ(L"1023 B", FormatFileSize(kb - 1));
    EXPECT_EQ(L"1.2 KB", FormatFileSize(5 * kb / 4));
    EXPECT_EQ(L"1.8 KB", FormatFileSize(7 * kb / 4));
    EXPECT_EQ(L"10.0 KB", FormatFileSize(10 * kb - 1));
    EXPECT_EQ(L"10 KB", FormatFileSize(10 * kb));
    EXPECT_EQ(L"10 KB", FormatFileSize(21 * kb / 2));
    EXPECT_EQ(L"12 KB", FormatFileSize(23 * kb / 2));
    EXPECT_EQ(L"1024 KB", FormatFileSize(mb - 1));
    EXPECT_EQ(L"1.0 MB", FormatFileSize(mb));
    EXPECT_EQ(L"1024 TB", FormatFileSize(1024 * tb));
}

TEST(RowFormatting, BuildDetailText_NoSize_TypeLabelOnly)
{
    EXPECT_EQ(L"Folder", BuildDetailText(L"Folder", 0, false));
}

TEST(RowFormatting, BuildDetailText_WithSize_TypeAndSize)
{
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

TEST(FileRowViewModelTests, CutOpacity_DefaultsToOne) {
    auto impl = winrt::make_self<winrt::FileScoutNative::implementation::FileRowViewModel>();
    EXPECT_DOUBLE_EQ(1.0, impl->CutOpacity());
    EXPECT_FALSE(impl->IsCut());
}

TEST(FileRowViewModelTests, CutOpacity_IsFourWhenCutFlagSet) {
    auto impl = winrt::make_self<winrt::FileScoutNative::implementation::FileRowViewModel>();
    impl->IsCut(true);
    EXPECT_TRUE(impl->IsCut());
    EXPECT_DOUBLE_EQ(0.4, impl->CutOpacity());

    impl->IsCut(false);
    EXPECT_DOUBLE_EQ(1.0, impl->CutOpacity());
}
