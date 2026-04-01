#include "pch.h"
#include <gtest/gtest.h>
#include <fstream>
#include "Shell/ShellInteropService.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Domain;

namespace
{
    // Creates a temp directory with 4 known entries:
    //   SubFolder/    (directory)
    //   sample.txt    (non-empty text file)
    //   photo.png     (non-empty file with image extension)
    //   archive.7z    (archive-like extension; shell support is machine-dependent)
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

// ── EnumerateDirectory ─────────────────────────────────────────────────────────

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

// S6: archive heuristic tests (BuildFastShellItemRef replaces CanEnumerateShellNamespace).
// The new heuristic sets is_supported_archive = true only for .zip and .tar by extension,
// without any shell call. These tests would fail with the old CanEnumerateShellNamespace path
// for fake files (shell can't parse non-archive content).

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
    // .7z is an archive candidate but NOT in the supported whitelist.
    // Old CanEnumerateShellNamespace was non-deterministic (depended on installed shell handlers).
    ShellInteropService svc;
    auto items = svc.EnumerateDirectory(m_root);
    auto archive = FindItem(items, L"archive.7z");
    EXPECT_TRUE(archive.is_archive_candidate);
    EXPECT_FALSE(archive.is_supported_archive);
}

// ── EnumerateDrives ────────────────────────────────────────────────────────────

TEST(EnumerateDrivesTest, ReturnsAtLeastOneDrive)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ShellInteropService svc;
    auto drives = svc.EnumerateDrives();
    EXPECT_GE(drives.size(), 1u);
    CoUninitialize();
}

// ── Large directory enumeration benchmark ──────────────────────────────────────
// Regression gate for the enumeration hot path: 1000 files across 10 extensions
// must enumerate in < 300 ms. If this starts failing it means EnumerateDirectory
// performance regressed (e.g. a blocking shell call was re-introduced per item).

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
            // 100 files per extension × 10 extensions = 1000 files
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
