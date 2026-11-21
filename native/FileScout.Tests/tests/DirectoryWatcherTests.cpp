#include "pch.h"
#include <gtest/gtest.h>

#include "Domain/WatchDebounce.h"
#include "Shell/DirectoryWatcher.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

using namespace FileScoutNative::Domain;
using namespace FileScoutNative::Shell;

namespace
{
    std::filesystem::path MakeUniqueWatchTestDir(std::wstring_view const suffix)
    {
        auto dir = std::filesystem::temp_directory_path() /
            (L"FileScoutWatchTests-" +
             std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(::GetTickCount64()) + L"-" +
             std::wstring{ suffix });
        std::filesystem::create_directories(dir);
        return dir;
    }

    struct ScopedTempDir
    {
        std::filesystem::path path;
        explicit ScopedTempDir(std::wstring_view const suffix)
            : path(MakeUniqueWatchTestDir(suffix))
        {
        }
        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
}

TEST(WatchDebounceTests, ShouldWatchLocationRejectsVirtualAndEmptyLocations)
{
    EXPECT_FALSE(ShouldWatchLocation({}, false, false, false));
    EXPECT_FALSE(ShouldWatchLocation(L"C:\\Users", true, false, false));
    EXPECT_FALSE(ShouldWatchLocation(L"C:\\Users\\a.zip", false, true, false));
    EXPECT_FALSE(ShouldWatchLocation(L"C:\\Users", false, false, true));
    EXPECT_FALSE(ShouldWatchLocation(std::filesystem::path{ kRecycleBinLocation }, false, false, false));
    EXPECT_FALSE(ShouldWatchLocation(std::filesystem::path{ kDrivesViewLocation }, false, false, false));
    EXPECT_TRUE(ShouldWatchLocation(L"C:\\Users\\Docs", false, false, false));
}

TEST(WatchDebounceTests, CoalescesBurstsAndFlushesAfterQuietPeriod)
{
    DirectoryWatchCoordinator coord;
    EXPECT_FALSE(coord.HasPending());
    EXPECT_FALSE(coord.ShouldFlush(1000, 300, 1500));

    coord.NoteChange(1000);
    EXPECT_TRUE(coord.HasPending());
    EXPECT_FALSE(coord.ShouldFlush(1100, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1100, 300, 1500), 200u);

    // Another event arrives at 1200 -> quiet window slides to 1500.
    coord.NoteChange(1200);
    EXPECT_FALSE(coord.ShouldFlush(1450, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1450, 300, 1500), 50u);

    EXPECT_TRUE(coord.ShouldFlush(1500, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1500, 300, 1500), 0u);

    coord.MarkFlushed();
    EXPECT_FALSE(coord.HasPending());
    EXPECT_FALSE(coord.ShouldFlush(2000, 300, 1500));
}

TEST(WatchDebounceTests, MaxWaitPreventsStarvationDuringContinuousWrites)
{
    DirectoryWatchCoordinator coord;
    coord.NoteChange(1000);
    for (uint64_t t = 1100; t <= 2400; t += 100)
    {
        coord.NoteChange(t);
        EXPECT_FALSE(coord.ShouldFlush(t, 300, 1500));
    }

    // At t = 2500, 1500ms have elapsed since the first pending event even though the last was 100ms ago.
    EXPECT_TRUE(coord.ShouldFlush(2500, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(2450, 300, 1500), 50u);
}

TEST(WatchDebounceTests, SelfMutationSuppressionIgnoresWindowThenResumes)
{
    DirectoryWatchCoordinator coord;
    coord.SuppressFor(1000, 600);
    EXPECT_TRUE(coord.IsSuppressed(1200));

    coord.NoteChange(1200);
    EXPECT_FALSE(coord.HasPending());

    coord.NoteChange(1599);
    EXPECT_FALSE(coord.HasPending());

    EXPECT_FALSE(coord.IsSuppressed(1600));
    coord.NoteChange(1600);
    EXPECT_TRUE(coord.HasPending());
    EXPECT_TRUE(coord.ShouldFlush(1900, 300, 1500));
}

TEST(DirectoryWatcherTests, DetectsCreateRenameAndDeleteAndCoalescesBurst)
{
    ScopedTempDir temp{ L"basic" };

    std::mutex mutex;
    std::condition_variable cv;
    int callbackCount = 0;

    DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        temp.path,
        [&]()
        {
            {
                std::lock_guard lock{ mutex };
                ++callbackCount;
            }
            cv.notify_all();
        },
        /*debounceMs=*/80,
        /*maxWaitMs=*/400));
    EXPECT_TRUE(watcher.IsWatching());
    EXPECT_EQ(watcher.WatchedPath(), temp.path);

    auto waitForCount = [&](int target)
    {
        std::unique_lock lock{ mutex };
        return cv.wait_for(lock, std::chrono::seconds{ 5 }, [&]() { return callbackCount >= target; });
    };

    // Create a burst of files rapidly; debounce should coalesce them into a small number of callbacks.
    for (int i = 0; i < 12; ++i)
    {
        std::ofstream out(temp.path / (L"file_" + std::to_wstring(i) + L".txt"));
        out << "hello";
    }

    ASSERT_TRUE(waitForCount(1));
    int const countAfterBurst = [&]()
    {
        std::lock_guard lock{ mutex };
        return callbackCount;
    }();
    EXPECT_LT(countAfterBurst, 12);

    // Rename a file -> triggers another notification.
    std::filesystem::rename(temp.path / L"file_0.txt", temp.path / L"renamed_0.txt");
    ASSERT_TRUE(waitForCount(countAfterBurst + 1));

    int const countAfterRename = [&]()
    {
        std::lock_guard lock{ mutex };
        return callbackCount;
    }();

    // Delete a file -> triggers another notification.
    std::filesystem::remove(temp.path / L"renamed_0.txt");
    ASSERT_TRUE(waitForCount(countAfterRename + 1));

    watcher.Stop();
    EXPECT_FALSE(watcher.IsWatching());
    EXPECT_TRUE(watcher.WatchedPath().empty());
}

TEST(DirectoryWatcherTests, RejectsMissingDirectoryAndSupportsCleanRetargetAndImmediateStop)
{
    ScopedTempDir dirA{ L"retargetA" };
    ScopedTempDir dirB{ L"retargetB" };

    DirectoryWatcher watcher;
    EXPECT_FALSE(watcher.Start(dirA.path / L"does-not-exist", []() {}));
    EXPECT_FALSE(watcher.IsWatching());

    EXPECT_TRUE(watcher.Start(dirA.path, []() {}, 100, 500));
    EXPECT_TRUE(watcher.IsWatching());

    // Immediately retarget to dirB without waiting for any events.
    EXPECT_TRUE(watcher.Start(dirB.path, []() {}, 100, 500));
    EXPECT_EQ(watcher.WatchedPath(), dirB.path);

    // Idempotent Stop.
    watcher.Stop();
    watcher.Stop();
    EXPECT_FALSE(watcher.IsWatching());
}
