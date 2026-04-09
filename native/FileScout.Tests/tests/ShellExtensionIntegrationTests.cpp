#include "pch.h"
#include <gtest/gtest.h>
#include "..\..\FileScout.ShellExt\FileScoutContextMenu.h"

TEST(ShellExtensionIntegration, BuildFileScoutPathFromShellExtensionPathMatchesNativeOutputLayout)
{
    auto const path = FileScoutShellExt::BuildFileScoutPathFromShellExtensionPath(
        LR"(C:\repo\native\bin\x64\Debug\ShellExt\FileScout.ShellExt.dll)");

    EXPECT_EQ(path, LR"(C:\repo\native\x64\Debug\FileScout.Native\FileScoutNative.exe)");
}

TEST(ShellExtensionIntegration, ContextMenuHandlerKeysDoNotRegisterDirectoryBackground)
{
    auto const& keys = FileScoutShellExt::ContextMenuHandlerRegistryKeys();

    EXPECT_EQ(keys.size(), 2U);
    EXPECT_EQ(std::wstring{ keys[0] }, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\FileScout");
    EXPECT_EQ(std::wstring{ keys[1] }, L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers\\FileScout");
}

TEST(ShellExtensionIntegration, ResolveFileScoutPathFallsBackWhenStoredPathIsMissing)
{
    auto const path = FileScoutShellExt::ResolveFileScoutPath(
        LR"(C:\definitely-missing\FileScoutNative.exe)",
        LR"(C:\repo\native\x64\Debug\FileScout.Native\FileScoutNative.exe)");

    EXPECT_EQ(path, LR"(C:\repo\native\x64\Debug\FileScout.Native\FileScoutNative.exe)");
}

TEST(ShellExtensionIntegration, QuoteCommandLineArgumentEscapesDriveRoots)
{
    auto const quoted = FileScoutShellExt::QuoteCommandLineArgument(LR"(C:\)");

    EXPECT_EQ(quoted, LR"("C:\\")");
}

TEST(ShellExtensionIntegration, ShellExecuteResultBelowThirtyTwoIsFailure)
{
    EXPECT_FALSE(FileScoutShellExt::DidShellExecuteSucceed(reinterpret_cast<HINSTANCE>(31)));
    EXPECT_FALSE(FileScoutShellExt::DidShellExecuteSucceed(reinterpret_cast<HINSTANCE>(32)));
    EXPECT_TRUE(FileScoutShellExt::DidShellExecuteSucceed(reinterpret_cast<HINSTANCE>(33)));
}
