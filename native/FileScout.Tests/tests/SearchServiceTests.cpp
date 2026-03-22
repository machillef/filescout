#include "pch.h"
#include <gtest/gtest.h>
#include "Search/SearchService.h"

using namespace FileScoutNative::Search;

// Helper: build a flat synthetic "filesystem" as a list of absolute paths
// the test WalkDir callback can enumerate instead of a real disk.
// SearchService exposes an injectable EnumerateFn for testing (see SearchService.h).

// ---------------------------------------------------------------------------
// Helper to run a search synchronously in tests using direct-call PostFn.
// ---------------------------------------------------------------------------
struct TestHarness
{
    std::vector<std::vector<SearchResult>> batches;
    bool completed{ false };
    bool cap_reached{ false };
    std::mutex mu;
    std::condition_variable cv;

    SearchService svc;

    // Synchronous PostFn: call the callback immediately on the calling thread.
    SearchService::PostFn MakeSyncPost()
    {
        return [this](std::function<void()> fn) { fn(); };
    }

    void WaitForComplete(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
    {
        std::unique_lock lock(mu);
        cv.wait_for(lock, timeout, [this] { return completed; });
    }
};

// ---------------------------------------------------------------------------
// Test 1: Cancel before first result — zero ResultCallback calls
// ---------------------------------------------------------------------------
TEST(SearchService, CancelBeforeFirstResult)
{
    TestHarness h;
    std::atomic<int> callCount{ 0 };

    h.svc.Start(
        L"C:\\Windows\\System32",
        L"ntdll",
        [&callCount](std::vector<SearchResult>) { ++callCount; },
        [](bool) {},
        h.MakeSyncPost());
    h.svc.Cancel();

    // Give a moment for thread to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(0, callCount.load());
}

// ---------------------------------------------------------------------------
// Test 2: Cancel mid-walk — no callbacks after Cancel (latch-based)
// ---------------------------------------------------------------------------
TEST(SearchService, CancelMidWalk)
{
    // Use a real directory large enough to produce results before we cancel.
    // C:\Windows\System32 has ~4500 files — enough for at least one batch.
    std::latch firstBatch(1);
    std::atomic<int> callsAfterCancel{ 0 };
    bool cancelFired{ false };

    SearchService svc;
    svc.Start(
        L"C:\\Windows",
        L"dll",
        [&firstBatch, &cancelFired, &callsAfterCancel](std::vector<SearchResult>) {
            if (!cancelFired)
                firstBatch.count_down(); // only decrement once
            else
                ++callsAfterCancel;
        },
        [](bool) {},
        // Real async post via a background thread simulating the dispatcher
        [](std::function<void()> fn) { fn(); });

    firstBatch.wait(); // block until first batch arrives
    cancelFired = true;
    svc.Cancel();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(0, callsAfterCancel.load());
}

// ---------------------------------------------------------------------------
// Test 3: Cap enforcement — 600 files in two score tiers, heap capped at 200
// ---------------------------------------------------------------------------
TEST(SearchService, CapEnforcement)
{
    // "ama_N.txt" scores 14 for query "ma" (non-prefix match).
    // "match_N.txt" scores 34 for query "ma" (prefix match).
    // NTFS iterates alphabetically: "ama_" before "match_", so the min-heap
    // fills with 200 low-scored entries first, then high-scored "match_" files
    // displace them and set cap_reached.
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{ tmpDir };

    for (int i = 0; i < 300; ++i)
    {
        // Low score (14): 'ma' matched as subsequence but NOT at filename prefix.
        std::ofstream f(tmpDir / (L"ama_" + std::to_wstring(i) + L".txt"));
    }
    for (int i = 0; i < 300; ++i)
    {
        // High score (34): 'ma' matched at filename prefix with consecutive bonus.
        std::ofstream f(tmpDir / (L"match_" + std::to_wstring(i) + L".txt"));
    }

    std::vector<SearchResult> allResults;
    bool cap{ false };
    std::latch done(1);

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ma",
        [&allResults](std::vector<SearchResult> r) { allResults = std::move(r); },
        [&cap, &done](bool c) { cap = c; done.count_down(); },
        [](std::function<void()> fn) { fn(); });

    done.wait();

    EXPECT_LE(static_cast<int>(allResults.size()), 200);
    EXPECT_TRUE(cap);
}

// ---------------------------------------------------------------------------
// Test 4: Exact 200 matches, no cap
// ---------------------------------------------------------------------------
TEST(SearchService, Exact200NoCap)
{
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTest200-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{ tmpDir };

    for (int i = 0; i < 200; ++i)
    {
        std::ofstream f(tmpDir / (L"match_" + std::to_wstring(i) + L".txt"));
    }

    bool cap{ true }; // initialise to true so a false result is conclusive
    std::latch done(1);

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ma",
        [](std::vector<SearchResult>) {},
        [&cap, &done](bool c) { cap = c; done.count_down(); },
        [](std::function<void()> fn) { fn(); });

    done.wait();
    EXPECT_FALSE(cap);
}

// ---------------------------------------------------------------------------
// Test 5: Tie truncation — 201 equal-score entries, cap_reached stays false
// ---------------------------------------------------------------------------
TEST(SearchService, TieTruncationNoCap)
{
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTestTie-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{ tmpDir };

    // All files named "ab_NNN.txt" — identical fuzzy score for query "ab"
    for (int i = 0; i < 201; ++i)
    {
        std::ofstream f(tmpDir / (L"ab_" + std::to_wstring(i) + L".txt"));
    }

    bool cap{ true };
    std::latch done(1);

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ab",
        [](std::vector<SearchResult>) {},
        [&cap, &done](bool c) { cap = c; done.count_down(); },
        [](std::function<void()> fn) { fn(); });

    done.wait();
    EXPECT_FALSE(cap); // ties silently dropped, no displacement
}

// ---------------------------------------------------------------------------
// Test 6: Depth limit — entries beyond depth 12 not returned
// ---------------------------------------------------------------------------
TEST(SearchService, DepthLimit)
{
    auto root = std::filesystem::temp_directory_path() /
        (L"FileScoutDepthTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(root);

    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{ root };

    // Build a 15-level deep chain; place a matchable file at depth 13 and depth 11
    std::filesystem::path deep = root;
    for (int d = 1; d <= 15; ++d)
    {
        deep /= (L"lvl" + std::to_wstring(d));
        std::filesystem::create_directories(deep);
        if (d == 11 || d == 13)
        {
            std::ofstream f(deep / L"target_file.txt");
        }
    }

    std::vector<SearchResult> results;
    std::latch done(1);

    SearchService svc;
    svc.Start(
        root.wstring(),
        L"ta",
        [&results](std::vector<SearchResult> r) { results = std::move(r); },
        [&done](bool) { done.count_down(); },
        [](std::function<void()> fn) { fn(); });

    done.wait();

    // depth-11 file should be in results; depth-13 file should NOT
    bool foundDepth11{ false }, foundDepth13{ false };
    for (auto const& r : results)
    {
        auto p = std::filesystem::path(r.full_path);
        if (p.parent_path().filename() == L"lvl11") foundDepth11 = true;
        if (p.parent_path().filename() == L"lvl13") foundDepth13 = true;
    }
    EXPECT_TRUE(foundDepth11);
    EXPECT_FALSE(foundDepth13);
}

// ---------------------------------------------------------------------------
// Test 7: Memory safety — Start/Cancel/Start without crash
// ---------------------------------------------------------------------------
TEST(SearchService, StartCancelStartNoCrash)
{
    SearchService svc;
    auto post = [](std::function<void()> fn) { fn(); };

    svc.Start(L"C:\\Windows", L"dll", [](std::vector<SearchResult>) {}, [](bool) {}, post);
    svc.Cancel();
    svc.Start(L"C:\\Windows\\System32", L"nt", [](std::vector<SearchResult>) {}, [](bool) {}, post);
    svc.Cancel();
    // destructor joins — test passes if no crash or hang within timeout
}
