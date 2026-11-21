#include "pch.h"
#include <gtest/gtest.h>
#include <fstream>
#include "Domain/IconCacheKey.h"
#include "Shell/ShellInteropService.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Domain;

namespace
{
    class ShellInteropFixture : public ::testing::Test
    {
    protected:
        std::filesystem::path m_root;

        void SetUp() override
        {
            // SHGetFileInfoW requires COM.
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            wchar_t tempPath[MAX_PATH]{};
            ASSERT_NE(GetTempPathW(MAX_PATH, tempPath), 0u);

            m_root = std::filesystem::path{ tempPath } / L"FileScoutTests_ShellInterop";
            std::filesystem::remove_all(m_root);
            std::filesystem::create_directories(m_root);

            std::filesystem::create_directory(m_root / L"SubFolder");

            { std::ofstream f{ m_root / L"sample.txt" }; f << "hello world"; }
            { std::ofstream f{ m_root / L"photo.png" };  f << "fake png bytes"; }
            { std::ofstream f{ m_root / L"archive.7z" }; f << "fake archive bytes"; }
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
            CoUninitialize();
        }

        ShellItemRef FindItem(std::vector<ShellItemRef> const& items,
                              std::wstring_view name) const
        {
            auto it = std::ranges::find_if(items, [&](ShellItemRef const& r)
                { return r.display_name == name; });
            if (it == items.end())
            {
                throw std::runtime_error("item not found in enumeration");
            }
            return *it;
        }
    };
}

TEST_F(ShellInteropFixture, EnumerateDirectory_ReturnsItems)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    EXPECT_GE(items.size(), 3u);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_FolderHasIsFolderTrue)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto folder = FindItem(items, L"SubFolder");
    EXPECT_TRUE(folder.is_folder);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_FileHasIsFolderFalse)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_FALSE(file.is_folder);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_DateModifiedPopulatedForFile)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_TRUE(file.date_modified_available);
    EXPECT_FALSE(file.date_modified_str.empty());
}

TEST_F(ShellInteropFixture, EnumerateDirectory_FileSizeBytesPopulated)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_TRUE(file.size_available);
    EXPECT_GT(file.size_bytes, 0u);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_FolderHasNoSizeAvailable)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto folder = FindItem(items, L"SubFolder");
    EXPECT_FALSE(folder.size_available);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_IsImageTrueForPng)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto img = FindItem(items, L"photo.png");
    EXPECT_TRUE(img.is_image);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_IsImageFalseForTxt)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_FALSE(file.is_image);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_KindLabelFolderForDirectory)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto folder = FindItem(items, L"SubFolder");
    EXPECT_EQ(folder.kind_label, L"Folder");
}

TEST_F(ShellInteropFixture, EnumerateDirectory_KindLabelExtensionForFile)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_EQ(file.kind_label, L"TXT file");
}

TEST_F(ShellInteropFixture, EnumerateDirectory_ArchiveCandidateGetsArchiveKindLabel)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto archive = FindItem(items, L"archive.7z");
    EXPECT_TRUE(archive.is_archive_candidate);
    EXPECT_EQ(archive.kind_label, L"7Z archive");
}

TEST_F(ShellInteropFixture, EnumerateDirectory_NonArchiveFileIsNotArchiveCandidate)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto file = FindItem(items, L"sample.txt");
    EXPECT_FALSE(file.is_archive_candidate);
    EXPECT_FALSE(file.is_supported_archive);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_PathsArePopulated)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    for (auto const& item : items)
    {
        EXPECT_FALSE(item.path.empty());
        EXPECT_FALSE(item.parsing_name.empty());
        EXPECT_FALSE(item.display_name.empty());
    }
}

TEST_F(ShellInteropFixture, CanEnumerateShellNamespace_ForExistingDirectory)
{
    ShellInteropService svc;
    EXPECT_TRUE(svc.CanEnumerateShellNamespace(m_root));
}

TEST_F(ShellInteropFixture, EnumerateShellDirectory_ReturnsItemsForExistingDirectory)
{
    ShellInteropService svc;
    auto items = svc.EnumerateShellDirectory(m_root);
    EXPECT_GE(items.size(), 3u);
    auto folder = FindItem(items, L"SubFolder");
    EXPECT_TRUE(folder.is_folder);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_ZipIsSupportedArchive)
{
    { std::ofstream f{ m_root / L"fake.zip" }; f << "not a real zip file"; }
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto zip = FindItem(items, L"fake.zip");
    EXPECT_TRUE(zip.is_archive_candidate);
    EXPECT_TRUE(zip.is_supported_archive);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_TarIsSupportedArchive)
{
    { std::ofstream f{ m_root / L"fake.tar" }; f << "not a real tar file"; }
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto tar = FindItem(items, L"fake.tar");
    EXPECT_TRUE(tar.is_archive_candidate);
    EXPECT_TRUE(tar.is_supported_archive);
}

TEST_F(ShellInteropFixture, EnumerateDirectory_7zIsNotSupportedArchive)
{
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto archive = FindItem(items, L"archive.7z");
    EXPECT_TRUE(archive.is_archive_candidate);
    EXPECT_FALSE(archive.is_supported_archive);
}

TEST(IconCachePolicy, ExecutablesUsePerItemKeysAndLiteralLookup)
{
    auto const first = std::filesystem::path{ LR"(C:\Apps\Alpha.EXE)" };
    auto const second = std::filesystem::path{ LR"(C:\Apps\Beta.exe)" };

    EXPECT_TRUE(FileScoutNative::Domain::UsesPerItemIconIdentity(first, false));
    EXPECT_TRUE(FileScoutNative::Domain::UsesLiteralIconLookup(first, false));
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(first, false),
        LR"(__item__:c:\apps\alpha.exe)");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(second, false),
        LR"(__item__:c:\apps\beta.exe)");
}

TEST(IconCachePolicy, DocumentsAndFoldersKeepSharedBuckets)
{
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\alpha.TXT)", false),
        L".txt");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\folder)", true),
        L"__folder__");
    EXPECT_EQ(
        FileScoutNative::Domain::BuildFileIconCacheKey(LR"(C:\Temp\README)", false),
        L"__file__");
}

TEST(IconCachePolicy, DriveRootsGetTheirOwnKeys)
{
    using FileScoutNative::Domain::BuildFileIconCacheKey;

    auto const folderKey = BuildFileIconCacheKey(LR"(C:\Temp\folder)", true);
    auto const cKey = BuildFileIconCacheKey(LR"(C:\)", true);
    auto const dKey = BuildFileIconCacheKey(LR"(D:\)", true);

    EXPECT_NE(cKey, folderKey);
    EXPECT_NE(dKey, folderKey);
    EXPECT_NE(cKey, dKey);
    EXPECT_EQ(cKey, BuildFileIconCacheKey(LR"(c:\)", true));
}

TEST(IconCachePolicy, LiteralLookupAndPerItemKeysAgree)
{
    struct Case { wchar_t const* path; bool is_folder; };
    Case const cases[] = {
        { LR"(C:\)", true },
        { LR"(D:\)", true },
        { LR"(C:\Temp\folder)", true },
        { LR"(C:\Apps\Alpha.EXE)", false },
        { LR"(C:\Temp\alpha.txt)", false },
        { LR"(C:\Temp\README)", false },
    };

    for (auto const& c : cases)
    {
        std::filesystem::path const path{ c.path };
        auto const key = FileScoutNative::Domain::BuildFileIconCacheKey(path, c.is_folder);
        EXPECT_EQ(
            FileScoutNative::Domain::UsesLiteralIconLookup(path, c.is_folder),
            key.starts_with(L"__item__:"))
            << std::filesystem::path{ c.path }.string() << " -> " << std::filesystem::path{ key }.string();
    }
}

TEST(EnumerateDrivesTest, ReturnsAtLeastOneDrive)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ShellInteropService svc;
    auto drives = svc.EnumerateDrives();
    EXPECT_GE(drives.size(), 1u);
    CoUninitialize();
}

namespace
{
    class LargeDirectoryFixture : public ::testing::Test
    {
    protected:
        std::filesystem::path m_root;

        static constexpr int k_file_count = 1000;
        static constexpr int k_ext_count = 10;
        static constexpr wchar_t const* k_exts[k_ext_count] = {
            L".txt", L".cpp", L".h",   L".xml", L".json",
            L".log", L".csv", L".dat", L".cfg", L".ini"
        };

        void SetUp() override
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            wchar_t tempPath[MAX_PATH]{};
            ASSERT_NE(GetTempPathW(MAX_PATH, tempPath), 0u);
            m_root = std::filesystem::path{ tempPath } / L"FileScoutTests_LargeDir";
            std::filesystem::remove_all(m_root);
            std::filesystem::create_directories(m_root);
            for (int e = 0; e < k_ext_count; ++e)
            {
                for (int i = 0; i < k_file_count / k_ext_count; ++i)
                {
                    auto name = std::wstring(L"file")
                        + std::to_wstring(e * 100 + i)
                        + k_exts[e];
                    std::ofstream f{ m_root / name };
                    f << "x";
                }
            }
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
            CoUninitialize();
        }
    };
}

TEST_F(LargeDirectoryFixture, LargeDirectoryEnumerationBenchmark)
{
    ShellInteropService svc;

    auto const start_ms = ::GetTickCount64();
    auto items = svc.EnumerateDirectory(m_root);
    auto const elapsed_ms = ::GetTickCount64() - start_ms;

    ASSERT_EQ(items.size(), static_cast<std::size_t>(k_file_count))
        << "Expected exactly " << k_file_count << " items";

    EXPECT_LT(elapsed_ms, 300u)
        << "EnumerateDirectory(" << k_file_count << " files) took "
        << elapsed_ms << " ms — regression limit is 300 ms";

    for (auto const& item : items)
    {
        if (item.is_folder) continue;
        EXPECT_TRUE(item.size_available)
            << "size_available missing for: " << item.display_name;
        EXPECT_TRUE(item.date_modified_available)
            << "date_modified_available missing for: " << item.display_name;
    }
}
