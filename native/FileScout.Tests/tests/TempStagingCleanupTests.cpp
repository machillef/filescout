#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/TempStagingCleanup.h"

using namespace FileScoutNative::Shell;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

class TempStagingCleanupFixture : public ::testing::Test
{
protected:
    fs::path m_temp;
    fs::file_time_type m_now;

    void SetUp() override
    {
        m_temp = fs::temp_directory_path() / L"FileScoutTests_TempCleanup";
        std::error_code ec;
        fs::remove_all(m_temp, ec);
        fs::create_directories(m_temp);
        m_now = fs::file_time_type::clock::now();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_temp, ec);
    }

    fs::path MakeDir(fs::path const& dir, std::chrono::hours const age)
    {
        fs::create_directories(dir);
        std::ofstream{ dir / L"payload.txt" } << "x";
        fs::last_write_time(dir, m_now - age);
        return dir;
    }
};

TEST_F(TempStagingCleanupFixture, DeletesOnlyFileScoutStagingDirsOlderThanADay)
{
    auto const oldDrag = MakeDir(DragStagingRoot(m_temp) / L"old-token", 48h);
    auto const newDrag = MakeDir(DragStagingRoot(m_temp) / L"new-token", 1h);
    auto const oldArchive = MakeDir(ArchiveOpenRoot(m_temp) / L"a.zip" / L"1-2-3", 30h);
    auto const newArchive = MakeDir(ArchiveOpenRoot(m_temp) / L"b.zip" / L"4-5-6", 2h);
    auto const foreignOld = MakeDir(m_temp / L"SomeOtherApp" / L"old", 100h);
    auto const foreignInFileScoutRoot = MakeDir(m_temp / L"FileScout" / L"NotStaging", 100h);

    auto const deleted = CleanupStaleStagingDirectories(m_temp, m_now);

    EXPECT_EQ(deleted, 2u);
    EXPECT_FALSE(fs::exists(oldDrag));
    EXPECT_TRUE(fs::exists(newDrag));
    EXPECT_FALSE(fs::exists(oldArchive));
    EXPECT_FALSE(fs::exists(oldArchive.parent_path())) << "empty per-archive folder is removed";
    EXPECT_TRUE(fs::exists(newArchive));
    EXPECT_TRUE(fs::exists(foreignOld));
    EXPECT_TRUE(fs::exists(foreignInFileScoutRoot));
}

TEST_F(TempStagingCleanupFixture, MissingRootsAreNotAnError)
{
    EXPECT_EQ(CleanupStaleStagingDirectories(m_temp, m_now), 0u);
}

TEST_F(TempStagingCleanupFixture, RootsMatchTheNamesTheAppUses)
{
    EXPECT_EQ(DragStagingRoot(m_temp), m_temp / L"FileScout" / L"DragStage");
    EXPECT_EQ(ArchiveOpenRoot(m_temp), m_temp / L"FileScoutNative" / L"ArchiveOpen");
}
