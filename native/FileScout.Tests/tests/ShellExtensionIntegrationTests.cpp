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
