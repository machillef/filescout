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

TEST(ShellExtensionIntegration, PrepareForInitializeClearsPreviousSelectionState)
{
    std::vector<std::wstring> selectedPaths{ L"C:\\old.txt" };

    FileScoutShellExt::PrepareForInitialize(selectedPaths, 2);

    EXPECT_TRUE(selectedPaths.empty());
    EXPECT_GE(selectedPaths.capacity(), 2U);
}

// ─── S-12 ────────────────────────────────────────────────────────────────────

TEST(ShellExtensionIntegration, ClsidIsARealGuidAndMatchesItsString)
{
    wchar_t text[64]{};
    ASSERT_GT(StringFromGUID2(CLSID_FileScoutContextMenu, text, static_cast<int>(std::size(text))), 0);

    EXPECT_EQ(std::wstring{ text }, std::wstring{ FileScoutShellExt::ClsidString });
    EXPECT_NE(std::wstring{ text }, L"{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}") << "placeholder CLSID";
    EXPECT_TRUE(IsEqualCLSID(__uuidof(FileScoutContextMenu), CLSID_FileScoutContextMenu));
}

TEST(ShellExtensionIntegration, InstalledLayoutNextToTheDllWins)
{
    std::wstring const dll = LR"(C:\Program Files\FileScout\FileScout.ShellExt.dll)";
    auto const path = FileScoutShellExt::ResolveFileScoutPathFromShellExtensionPath(
        dll,
        [](std::filesystem::path const& candidate)
        {
            return candidate == std::filesystem::path{ LR"(C:\Program Files\FileScout\FileScoutNative.exe)" };
        });

    EXPECT_EQ(path, LR"(C:\Program Files\FileScout\FileScoutNative.exe)");
}

TEST(ShellExtensionIntegration, DevelopmentLayoutIsFoundFromTheBuildOutput)
{
    std::wstring const dll = LR"(C:\repo\native\bin\x64\Release\ShellExt\FileScout.ShellExt.dll)";
    std::filesystem::path const exe = LR"(C:\repo\native\x64\Release\FileScout.Native\FileScoutNative.exe)";
    auto const path = FileScoutShellExt::ResolveFileScoutPathFromShellExtensionPath(
        dll,
        [&](std::filesystem::path const& candidate) { return candidate == exe; });

    EXPECT_EQ(path, exe.wstring());
}

TEST(ShellExtensionIntegration, CandidatesCoverInstalledThenDevelopmentLayout)
{
    auto const candidates = FileScoutShellExt::BuildFileScoutPathCandidates(
        LR"(C:\repo\native\bin\x64\Debug\ShellExt\FileScout.ShellExt.dll)");

    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_EQ(candidates[0], LR"(C:\repo\native\bin\x64\Debug\ShellExt\FileScoutNative.exe)");
    EXPECT_EQ(candidates[1], LR"(C:\repo\native\x64\Debug\FileScout.Native\FileScoutNative.exe)");
}

TEST(ShellExtensionIntegration, ReadGrowingStringReturnsPathsLongerThanMaxPath)
{
    std::wstring const longPath = LR"(\\?\C:\)" + std::wstring(700, L'a') + LR"(\FileScout.ShellExt.dll)";

    // GetModuleFileNameW semantics: writes what fits and returns the buffer size when truncated.
    auto const text = FileScoutShellExt::ReadGrowingString([&](wchar_t* buffer, DWORD capacity) -> DWORD
    {
        auto const count = (std::min)(static_cast<std::size_t>(capacity), longPath.size());
        std::copy_n(longPath.begin(), count, buffer);
        if (count == capacity)
        {
            buffer[capacity - 1] = L'\0';
            return capacity;
        }
        buffer[count] = L'\0';
        return static_cast<DWORD>(count);
    });

    EXPECT_EQ(text, longPath);
}
