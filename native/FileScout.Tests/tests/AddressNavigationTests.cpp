#include "pch.h"
#include <gtest/gtest.h>
#include <fstream>
#include "Domain/AddressNavigation.h"

using namespace FileScoutNative::Domain;

TEST(AddressNavigation, ThisPcResolvesToDrivesView)
{
    auto const target = ResolveAddressNavigationTarget(
        L"This PC",
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const&) { return false; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::DrivesView);
}

TEST(AddressNavigation, ExistingDirectoryResolvesToDirectory)
{
    auto const target = ResolveAddressNavigationTarget(
        std::filesystem::temp_directory_path().wstring(),
        [](std::filesystem::path const& path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        },
        [](std::filesystem::path const& path)
        {
            std::error_code ec;
            return std::filesystem::is_directory(path, ec) && !ec;
        },
        [](std::filesystem::path const&) { return false; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::Directory);
}

TEST(AddressNavigation, ExistingFileResolvesToFile)
{
    auto const tempFile = std::filesystem::temp_directory_path() / L"filescout-address-test.txt";
    {
        std::ofstream stream{ tempFile };
        stream << "hello";
    }

    auto const target = ResolveAddressNavigationTarget(
        tempFile.wstring(),
        [](std::filesystem::path const& path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        },
        [](std::filesystem::path const& path)
        {
            std::error_code ec;
            return std::filesystem::is_directory(path, ec) && !ec;
        },
        [](std::filesystem::path const&) { return false; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::File);
    EXPECT_EQ(target.path, tempFile);

    std::error_code ec;
    std::filesystem::remove(tempFile, ec);
}

TEST(AddressNavigation, ShellNamespaceFallsBackWhenFilesystemDoesNotExist)
{
    auto const target = ResolveAddressNavigationTarget(
        LR"(\\wsl.localhost)",
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const& path)
        {
            return path == std::filesystem::path{ LR"(\\wsl.localhost)" };
        });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::ShellNamespace);
}

TEST(AddressNavigation, InvalidInputStaysInvalid)
{
    auto const target = ResolveAddressNavigationTarget(
        L"   ",
        [](std::filesystem::path const&) { return true; },
        [](std::filesystem::path const&) { return true; },
        [](std::filesystem::path const&) { return true; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::Invalid);
}

TEST(AddressNavigation, RelativeGarbageStaysInvalid)
{
    auto const target = ResolveAddressNavigationTarget(
        L"whatever",
        [](std::filesystem::path const&) { return true; },
        [](std::filesystem::path const&) { return true; },
        [](std::filesystem::path const&) { return true; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::Invalid);
}

TEST(AddressNavigation, DriveLetterOnlyNormalizesToDriveRoot)
{
    auto const target = ResolveAddressNavigationTarget(
        L"C:",
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const&) { return false; },
        [](std::filesystem::path const&) { return false; });

    EXPECT_EQ(target.kind, AddressNavigationTargetKind::Directory);
    EXPECT_EQ(target.path, std::filesystem::path{ L"C:\\" });
}
