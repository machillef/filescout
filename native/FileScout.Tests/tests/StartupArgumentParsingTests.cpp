#include "pch.h"
#include <gtest/gtest.h>

namespace FileScoutNative::detail
{
    std::optional<std::filesystem::path> ResolveLaunchPathArgument(std::span<wchar_t const* const> args);
}

TEST(StartupArgumentParsing, LeadingDashPathIsAcceptedAsLaunchPath)
{
    constexpr wchar_t const* args[]{ L"-reports" };

    auto const launchPath = FileScoutNative::detail::ResolveLaunchPathArgument(args);

    ASSERT_TRUE(launchPath.has_value());
    EXPECT_EQ(launchPath->wstring(), L"-reports");
}

namespace FileScoutNative::detail
{
    enum class StartupAction { OpenWindow, SetDefaultFileManager, RestoreExplorer };
    StartupAction ResolveStartupAction(std::span<wchar_t const* const> args);
    bool ShouldDelegateToExplorer(std::wstring_view argument);
}

TEST(StartupArgumentParsing, VirtualShellFoldersAreHandedBackToExplorer)
{
    EXPECT_TRUE(FileScoutNative::detail::ShouldDelegateToExplorer(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
    EXPECT_TRUE(FileScoutNative::detail::ShouldDelegateToExplorer(L"Shell:RecycleBinFolder"));
    EXPECT_FALSE(FileScoutNative::detail::ShouldDelegateToExplorer(L"C:\\Users"));
    EXPECT_FALSE(FileScoutNative::detail::ShouldDelegateToExplorer(L"\\\\server\\share"));
}

// The Drive verb passes "%1" quoted. For a drive root that is "C:\", and CommandLineToArgvW reads
// the backslash before the closing quote as an escaped quote, so argv holds C:".
TEST(StartupArgumentParsing, DriveRootMangledByArgvQuotingIsRepaired)
{
    constexpr wchar_t const* args[]{ L"C:\"" };

    auto const launchPath = FileScoutNative::detail::ResolveLaunchPathArgument(args);

    ASSERT_TRUE(launchPath.has_value());
    EXPECT_EQ(launchPath->wstring(), L"C:\\");
}

TEST(StartupArgumentParsing, PlainFolderArgumentIsUnchanged)
{
    constexpr wchar_t const* args[]{ L"C:\\Users\\runetest\\Downloads" };

    auto const launchPath = FileScoutNative::detail::ResolveLaunchPathArgument(args);

    ASSERT_TRUE(launchPath.has_value());
    EXPECT_EQ(launchPath->wstring(), L"C:\\Users\\runetest\\Downloads");
}

TEST(StartupArgumentParsing, DefaultFileManagerFlagsSelectHeadlessActions)
{
    constexpr wchar_t const* set[]{ L"--set-default-file-manager" };
    constexpr wchar_t const* restore[]{ L"--restore-explorer" };
    constexpr wchar_t const* folder[]{ L"C:\\Windows" };
    using FileScoutNative::detail::StartupAction;

    EXPECT_EQ(FileScoutNative::detail::ResolveStartupAction(set), StartupAction::SetDefaultFileManager);
    EXPECT_EQ(FileScoutNative::detail::ResolveStartupAction(restore), StartupAction::RestoreExplorer);
    EXPECT_EQ(FileScoutNative::detail::ResolveStartupAction(folder), StartupAction::OpenWindow);
    EXPECT_EQ(FileScoutNative::detail::ResolveStartupAction({}), StartupAction::OpenWindow);
}
