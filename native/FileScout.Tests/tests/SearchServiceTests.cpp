#include "pch.h"
#include <future>
#include <gtest/gtest.h>
#include "Search/SearchService.h"

using namespace FileScoutNative::Search;

struct TestHarness
{
    std::vector<std::vector<SearchResult>> batches;

    SearchService svc;

    SearchService::PostFn MakeSyncPost()
    {
        return [this](std::function<void()> fn) { fn(); };
    }
};

TEST(SearchService, CancelBeforeFirstResult)
{
    TestHarness h;
    std::atomic<int> callCount{ 0 };

    h.svc.Start(
        L"C:\\Windows\\System32",
        L"ntdll",
        [&callCount](std::vector<SearchResult>) { ++callCount; },
        [](bool, bool) {},
        h.MakeSyncPost());
    h.svc.Cancel();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(0, callCount.load());
}

TEST(SearchService, CancelMidWalk)
{
    std::promise<void> firstBatchPromise;
    auto firstBatch = firstBatchPromise.get_future();
    std::atomic<int> callsAfterCancel{ 0 };
    std::atomic<bool> cancelFired{ false };

    SearchService svc;
    svc.Start(
        L"C:\\Windows",
        L"dll",
        [&firstBatchPromise, &cancelFired, &callsAfterCancel](std::vector<SearchResult>) {
            if (!cancelFired.exchange(true))
            {
                firstBatchPromise.set_value();
            }
            else
            {
                ++callsAfterCancel;
            }
        },
        [](bool, bool) {},
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(firstBatch.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    svc.Cancel();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(0, callsAfterCancel.load());
}

TEST(SearchService, CapEnforcement)
{
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup
    {
        std::filesystem::path p;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{ tmpDir };

    for (int i = 0; i < 200; ++i)
    {
        std::ofstream f(tmpDir / (L"aab_" + std::to_wstring(i) + L".txt"));
    }
    {
        std::ofstream f(tmpDir / L"ab_top.txt");
    }

    std::vector<SearchResult> allResults;
    bool cap{ false };
    bool failed{ false };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ab",
        [&allResults](std::vector<SearchResult> r) { allResults = std::move(r); },
        [&cap, &failed, &donePromise](bool c, bool f) {
            cap = c;
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(10)), std::future_status::ready);

    EXPECT_LE(static_cast<int>(allResults.size()), 200);
    EXPECT_TRUE(cap);
    EXPECT_FALSE(failed);
}

TEST(SearchService, Exact200NoCap)
{
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTest200-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup
    {
        std::filesystem::path p;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{ tmpDir };

    for (int i = 0; i < 200; ++i)
    {
        std::ofstream f(tmpDir / (L"match_" + std::to_wstring(i) + L".txt"));
    }

    bool cap{ true };
    bool failed{ true };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ma",
        [](std::vector<SearchResult>) {},
        [&cap, &failed, &donePromise](bool c, bool f) {
            cap = c;
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_FALSE(cap);
    EXPECT_FALSE(failed);
}

TEST(SearchService, TieTruncationNoCap)
{
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTestTie-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup
    {
        std::filesystem::path p;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{ tmpDir };

    for (int i = 0; i < 201; ++i)
    {
        std::ofstream f(tmpDir / (L"ab_" + std::to_wstring(i) + L".txt"));
    }

    bool cap{ true };
    bool failed{ true };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ab",
        [](std::vector<SearchResult>) {},
        [&cap, &failed, &donePromise](bool c, bool f) {
            cap = c;
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_FALSE(cap);
    EXPECT_FALSE(failed);
}

TEST(SearchService, DepthLimit)
{
    auto root = std::filesystem::temp_directory_path() /
        (L"FileScoutDepthTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root);

    struct Cleanup
    {
        std::filesystem::path p;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{ root };

    std::filesystem::path deep = root;
    for (int d = 1; d <= 15; ++d)
    {
        deep /= (L"lvl" + std::to_wstring(d));
        std::filesystem::create_directories(deep);
        if (d == 12 || d == 13)
        {
            std::ofstream f(deep / L"target_file.txt");
        }
    }

    std::vector<SearchResult> results;
    bool failed{ true };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        root.wstring(),
        L"ta",
        [&results](std::vector<SearchResult> r) { results = std::move(r); },
        [&failed, &donePromise](bool, bool f) {
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(10)), std::future_status::ready);

    bool foundDepth12{ false };
    bool foundDepth13{ false };
    for (auto const& r : results)
    {
        auto p = std::filesystem::path(r.full_path);
        if (p.parent_path().filename() == L"lvl12")
        {
            foundDepth12 = true;
        }
        if (p.parent_path().filename() == L"lvl13")
        {
            foundDepth13 = true;
        }
    }

    EXPECT_TRUE(foundDepth12);
    EXPECT_FALSE(foundDepth13);
    EXPECT_FALSE(failed);
}

TEST(SearchService, RejectsNonLocalRootsAsFailure)
{
    std::vector<SearchResult> results;
    bool cap{ true };
    bool failed{ false };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        LR"(\\server\share)",
        L"ab",
        [&results](std::vector<SearchResult> r) { results = std::move(r); },
        [&cap, &failed, &donePromise](bool c, bool f) {
            cap = c;
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(results.empty());
    EXPECT_FALSE(cap);
    EXPECT_TRUE(failed);
}

TEST(SearchService, LongWalkWithoutNewMatchesDoesNotEmitRedundantEmptyBatches)
{
    auto root = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchNoMatch-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root);

    struct Cleanup
    {
        std::filesystem::path p;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{ root };

    for (int dirIndex = 0; dirIndex < 30; ++dirIndex)
    {
        auto const dir = root / (L"bucket_" + std::to_wstring(dirIndex));
        std::filesystem::create_directories(dir);
        for (int fileIndex = 0; fileIndex < 400; ++fileIndex)
        {
            std::ofstream f(dir / (L"alpha_" + std::to_wstring(dirIndex) + L"_" +
                std::to_wstring(fileIndex) + L".txt"));
        }
    }

    std::atomic<int> resultBatchCount{ 0 };
    bool failed{ true };
    std::promise<void> donePromise;
    auto done = donePromise.get_future();

    SearchService svc;
    svc.Start(
        root.wstring(),
        L"qq",
        [&resultBatchCount](std::vector<SearchResult>) { ++resultBatchCount; },
        [&failed, &donePromise](bool, bool f) {
            failed = f;
            donePromise.set_value();
        },
        [](std::function<void()> fn) { fn(); });

    ASSERT_EQ(done.wait_for(std::chrono::seconds(20)), std::future_status::ready);
    EXPECT_EQ(0, resultBatchCount.load());
    EXPECT_FALSE(failed);
}

TEST(SearchService, StartCancelStartNoCrash)
{
    SearchService svc;
    auto post = [](std::function<void()> fn) { fn(); };

    svc.Start(L"C:\\Windows", L"dll", [](std::vector<SearchResult>) {}, [](bool, bool) {}, post);
    svc.Cancel();
    svc.Start(L"C:\\Windows\\System32", L"nt", [](std::vector<SearchResult>) {}, [](bool, bool) {}, post);
    svc.Cancel();
}
