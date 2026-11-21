#include "pch.h"
#include <gtest/gtest.h>

#include "Domain/WatchDebounce.h"
#include "Shell/DirectoryWatcher.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
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

    struct ChangeCounter
    {
        std::mutex mutex;
        std::condition_variable cv;
        int count{ 0 };
        uint64_t lastEventTick{ 0 };

        void Increment(uint64_t const eventTickMs)
        {
            {
                std::lock_guard lock{ mutex };
                ++count;
                lastEventTick = eventTickMs;
            }
            cv.notify_all();
        }

        bool WaitForAtLeast(int target, std::chrono::milliseconds timeout = std::chrono::seconds{ 5 })
        {
            std::unique_lock lock{ mutex };
            return cv.wait_for(lock, timeout, [&]() { return count >= target; });
        }

        int Snapshot()
        {
            std::lock_guard lock{ mutex };
            return count;
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

TEST(WatchDebounceTests, SnapshotCoverageAndInactivePaneSelectionRestoreRules)
{
    // Events strictly before enumeration started are already covered by that snapshot.
    EXPECT_TRUE(IsWatchEventCoveredBySnapshot(1000, 1050));
    EXPECT_FALSE(IsWatchEventCoveredBySnapshot(1050, 1050));
    EXPECT_FALSE(IsWatchEventCoveredBySnapshot(1100, 1050));
    EXPECT_FALSE(IsWatchEventCoveredBySnapshot(0, 1050));
    EXPECT_FALSE(IsWatchEventCoveredBySnapshot(1000, 0));

    // Active pane always restores selection; inactive right pane (1) restores when background
    // watch refresh requests selection preservation while left pane (0) stays active.
    EXPECT_TRUE(ShouldRestoreSelectionInLoadedPane(/*loadedPaneIndex=*/0, /*activePaneIndex=*/0, false));
    EXPECT_FALSE(ShouldRestoreSelectionInLoadedPane(/*loadedPaneIndex=*/1, /*activePaneIndex=*/0, false));
    EXPECT_TRUE(ShouldRestoreSelectionInLoadedPane(/*loadedPaneIndex=*/1, /*activePaneIndex=*/0, true));
}

TEST(WatchDebounceTests, CoalescesBurstsAndFlushesAfterQuietPeriod)
{
    DirectoryWatchCoordinator coord;
    EXPECT_FALSE(coord.HasPending());
    EXPECT_FALSE(coord.ShouldFlush(1000, 300, 1500));

    coord.NoteChange(1000);
    EXPECT_TRUE(coord.HasPending());
    EXPECT_EQ(coord.LastEventTickMs(), 1000u);
    EXPECT_FALSE(coord.ShouldFlush(1100, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1100, 300, 1500), 200u);

    // Another event arrives at 1200 -> quiet window slides to 1500.
    coord.NoteChange(1200);
    EXPECT_EQ(coord.LastEventTickMs(), 1200u);
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

TEST(WatchDebounceTests, SelfMutationSuppressionDefersAndReconcilesOnceAfterDeadline)
{
    DirectoryWatchCoordinator coord;
    coord.SuppressFor(1000, 600);
    EXPECT_TRUE(coord.IsSuppressed(1200));

    // Events during the suppression window are deferred rather than dropped.
    coord.NoteChange(1200);
    EXPECT_TRUE(coord.HasPending());
    EXPECT_TRUE(coord.HasDeferredDuringSuppression());
    EXPECT_FALSE(coord.ShouldFlush(1200, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1200, 300, 1500), 400u);

    coord.NoteChange(1599);
    EXPECT_FALSE(coord.ShouldFlush(1599, 300, 1500));

    // Once the suppression window ends, exactly one reconciliation flush becomes ready.
    EXPECT_FALSE(coord.IsSuppressed(1600));
    EXPECT_TRUE(coord.ShouldFlush(1600, 300, 1500));
    EXPECT_EQ(coord.RemainingWaitMs(1600, 300, 1500), 0u);

    coord.MarkFlushed();
    EXPECT_FALSE(coord.HasPending());
    EXPECT_FALSE(coord.HasDeferredDuringSuppression());
}

TEST(DirectoryWatcherTests, CoalescesCreateBurstAfterArmed)
{
    ScopedTempDir temp{ L"create-burst" };
    auto counter = std::make_shared<ChangeCounter>();

    DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        temp.path,
        [counter](uint64_t const tickMs) { counter->Increment(tickMs); },
        /*debounceMs=*/120,
        /*maxWaitMs=*/600));
    ASSERT_TRUE(watcher.WaitUntilArmed(5000));
    EXPECT_TRUE(watcher.IsWatching());
    EXPECT_EQ(watcher.WatchedPath(), temp.path);

    for (int i = 0; i < 12; ++i)
    {
        std::ofstream out(temp.path / (L"file_" + std::to_wstring(i) + L".txt"));
        out << "hello";
    }

    ASSERT_TRUE(counter->WaitForAtLeast(1));
    // Wait for debounce window to drain and verify the burst was coalesced.
    std::this_thread::sleep_for(std::chrono::milliseconds{ 300 });
    int const totalAfterDrain = counter->Snapshot();
    EXPECT_GE(totalAfterDrain, 1);
    EXPECT_LT(totalAfterDrain, 12);

    watcher.Stop();
    EXPECT_FALSE(watcher.IsWatching());
    EXPECT_TRUE(watcher.WatchedPath().empty());
}

TEST(DirectoryWatcherTests, DetectsRenameInIsolatedDirectory)
{
    ScopedTempDir temp{ L"rename" };
    {
        std::ofstream out(temp.path / L"before.txt");
        out << "rename-me";
    }

    auto counter = std::make_shared<ChangeCounter>();
    DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        temp.path,
        [counter](uint64_t const tickMs) { counter->Increment(tickMs); },
        60,
        300));
    ASSERT_TRUE(watcher.WaitUntilArmed(5000));

    std::filesystem::rename(temp.path / L"before.txt", temp.path / L"after.txt");
    EXPECT_TRUE(counter->WaitForAtLeast(1));

    watcher.Stop();
}

TEST(DirectoryWatcherTests, DetectsDeleteInIsolatedDirectory)
{
    ScopedTempDir temp{ L"delete" };
    {
        std::ofstream out(temp.path / L"remove_me.txt");
        out << "bye";
    }

    auto counter = std::make_shared<ChangeCounter>();
    DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        temp.path,
        [counter](uint64_t const tickMs) { counter->Increment(tickMs); },
        60,
        300));
    ASSERT_TRUE(watcher.WaitUntilArmed(5000));

    std::filesystem::remove(temp.path / L"remove_me.txt");
    EXPECT_TRUE(counter->WaitForAtLeast(1));

    watcher.Stop();
}

TEST(DirectoryWatcherTests, ReportsFailureForMissingDirectoryAndSupportsCleanRetargetAndStop)
{
    ScopedTempDir dirA{ L"retargetA" };
    ScopedTempDir dirB{ L"retargetB" };

    auto readyStatus = std::make_shared<std::atomic<int>>(-1);
    DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        dirA.path / L"does-not-exist",
        [](uint64_t) {},
        100,
        500,
        [readyStatus](bool const armed) { readyStatus->store(armed ? 1 : 0); }));
    EXPECT_FALSE(watcher.WaitUntilArmed(5000));
    EXPECT_EQ(readyStatus->load(), 0);
    EXPECT_FALSE(watcher.IsWatching());

    ASSERT_TRUE(watcher.Start(dirA.path, [](uint64_t) {}, 100, 500));
    ASSERT_TRUE(watcher.WaitUntilArmed(5000));
    EXPECT_TRUE(watcher.IsWatching());

    // Retarget to dirB without waiting for any events.
    ASSERT_TRUE(watcher.Start(dirB.path, [](uint64_t) {}, 100, 500));
    ASSERT_TRUE(watcher.WaitUntilArmed(5000));
    EXPECT_EQ(watcher.WatchedPath(), dirB.path);

    // Idempotent Stop.
    watcher.Stop();
    watcher.Stop();
    EXPECT_FALSE(watcher.IsWatching());
}
