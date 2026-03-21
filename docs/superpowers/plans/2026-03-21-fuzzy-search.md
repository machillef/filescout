# Fuzzy File Search (R17) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add fzf-style fuzzy file search to the FileScout address bar — typing filters the current pane instantly, Enter escalates to a background recursive tree walk capped at 200 best-scored results.

**Architecture:** Three new units: `FuzzyMatcher` (pure C++ header scorer), `SearchService` (background walk with shared_ptr cancellation and top-200 min-heap), and a three-state address bar state machine (None → Filter → Recursive) wired into `MainWindow`. The address bar's existing `TextBox` doubles as the search input; a per-pane `ProgressBar` signals active recursive walks.

**Tech Stack:** C++20, `std::filesystem`, `std::thread`, `std::shared_ptr<std::atomic<bool>>`, `std::latch` (tests), WinUI 3 `KeyboardAccelerator`, GoogleTest (existing test framework)

---

## File Map

| File | Status | Responsibility |
|---|---|---|
| `native/FileScout.Native/Search/FuzzyMatcher.h` | Create | Pure scorer — `FuzzyScore(query, filename) → int` |
| `native/FileScout.Native/Search/SearchService.h` | Create | Interface: `Start`, `Cancel`, `~SearchService`, `PostFn` typedef |
| `native/FileScout.Native/Search/SearchService.cpp` | Create | Walk impl: `WalkDir`, min-heap, 50ms batch dispatch, shared_ptr cancellation |
| `native/FileScout.Native/MainWindow.xaml.h` | Modify | Add `SearchMode` enum + search fields to `PaneContext` |
| `native/FileScout.Native/MainWindow.xaml` | Modify | Add per-pane `ProgressBar`; add Ctrl+F `KeyboardAccelerator` to `Window` |
| `native/FileScout.Native/MainWindow.xaml.cpp` | Modify | State machine, handlers, result row building, nav/tab cancel guards |
| `native/FileScout.Native/FileScout.Native.vcxproj` | Modify | Add Search/*.h + SearchService.cpp |
| `native/FileScout.Tests/pch.h` | Modify | Add `<thread>`, `<functional>`, `<atomic>`, `<latch>`, `<memory>`, `<queue>`, `<chrono>` |
| `native/FileScout.Tests/tests/FuzzyMatcherTests.cpp` | Create | Unit tests for scorer |
| `native/FileScout.Tests/tests/SearchServiceTests.cpp` | Create | Unit tests for service (synchronous PostFn) |
| `native/FileScout.Tests/FileScout.Tests.vcxproj` | Modify | Add SearchService.cpp + new test files |

**Build command (run after each task):**
```
cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe\" native\FileScout.Native.sln /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal"
```

**Run tests:**
```
native\bin\x64\Debug\Tests\FileScout.Tests.exe
```

---

## Task 1: FuzzyMatcher — scorer header + unit tests (R17a)

**Files:**
- Create: `native/FileScout.Native/Search/FuzzyMatcher.h`
- Create: `native/FileScout.Tests/tests/FuzzyMatcherTests.cpp`
- Modify: `native/FileScout.Native/FileScout.Native.vcxproj` (add ClInclude)
- Modify: `native/FileScout.Tests/FileScout.Tests.vcxproj` (add test file)
- Modify: `native/FileScout.Tests/pch.h` (add missing headers)

---

- [ ] **Step 1.1 — Add missing STL headers to the test PCH**

Open `native/FileScout.Tests/pch.h`. After the existing `#include <vector>` line, add:

```cpp
#include <atomic>
#include <chrono>
#include <functional>
#include <latch>
#include <memory>
#include <queue>
#include <thread>
```

---

- [ ] **Step 1.2 — Write the failing FuzzyMatcher tests**

Create `native/FileScout.Tests/tests/FuzzyMatcherTests.cpp`:

```cpp
#include "pch.h"
#include <gtest/gtest.h>
#include "Search/FuzzyMatcher.h"

using namespace FileScoutNative::Search;

TEST(FuzzyMatcher, EmptyQueryReturnsNoMatch)
{
    EXPECT_EQ(-1, FuzzyScore(L"", L"document.txt"));
}

TEST(FuzzyMatcher, SingleCharQueryReturnsNoMatch)
{
    EXPECT_EQ(-1, FuzzyScore(L"d", L"document.txt"));
}

TEST(FuzzyMatcher, NoMatchWhenCharNotInFilename)
{
    EXPECT_EQ(-1, FuzzyScore(L"zzz", L"document.txt"));
}

TEST(FuzzyMatcher, PrefixScoresHigherThanMidWord)
{
    // "doc" at start of "document.txt" should outscore "doc" buried in "readme.doc"
    int prefixScore = FuzzyScore(L"doc", L"document.txt");
    int midScore    = FuzzyScore(L"doc", L"readme.doc");
    EXPECT_GT(prefixScore, midScore);
    EXPECT_GE(prefixScore, 0);
}

TEST(FuzzyMatcher, ConsecutiveScoresHigherThanScattered)
{
    int consecutive = FuzzyScore(L"abc", L"abcdef.txt");
    int scattered   = FuzzyScore(L"abc", L"axbxcx.txt");
    EXPECT_GT(consecutive, scattered);
    EXPECT_GE(consecutive, 0);
}

TEST(FuzzyMatcher, WildcardIsLiteralPositive)
{
    // Filename literally contains '*' — query should match.
    int score = FuzzyScore(L"a*b", L"a*b_file.txt");
    EXPECT_GE(score, 0);
}

TEST(FuzzyMatcher, WildcardIsLiteralNegative)
{
    // Filename has no '*' — query with '*' should not match.
    EXPECT_EQ(-1, FuzzyScore(L"a*b", L"ab_file.txt"));
}
```

---

- [ ] **Step 1.3 — Run tests to verify they fail with "FuzzyMatcher.h not found"**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_filter=FuzzyMatcher*
```

Expected: compile error or linker error — `Search/FuzzyMatcher.h` does not exist yet.

---

- [ ] **Step 1.4 — Create `FuzzyMatcher.h`**

Create `native/FileScout.Native/Search/FuzzyMatcher.h`:

```cpp
#pragma once
#include <string_view>
#include <cwctype>

namespace FileScoutNative::Search
{
    // Returns a score >= 0 if query matches filename as a fuzzy subsequence, -1 if no match.
    // Query length < 2 always returns -1.
    // Characters *, ?, [ are treated as literals — no glob expansion ever occurs.
    // Stack-only: no heap allocation per call.
    inline int FuzzyScore(std::wstring_view query, std::wstring_view filename)
    {
        if (query.size() < 2)
            return -1;

        int score = 0;
        std::size_t fi = 0;     // current position in filename
        std::size_t qi = 0;     // current position in query
        int consecutiveRun = 0; // length of current consecutive match run

        while (qi < query.size() && fi < filename.size())
        {
            wchar_t qc = std::towlower(query[qi]);
            wchar_t fc = std::towlower(filename[fi]);

            if (qc == fc)
            {
                // Prefix bonus: first query char matches at filename index 0
                if (qi == 0 && fi == 0)
                    score += 20;

                // Consecutive bonus: this match immediately follows the previous match
                if (consecutiveRun > 0)
                {
                    score += 15;
                }
                consecutiveRun++;

                // Word-boundary bonus: match starts after a separator or case transition
                if (fi > 0)
                {
                    wchar_t prev = filename[fi - 1];
                    bool afterSeparator = (prev == L'.' || prev == L'_' ||
                                           prev == L'-' || prev == L' ');
                    bool afterCaseChange = (std::iswlower(prev) && std::iswupper(filename[fi]));
                    if (afterSeparator || afterCaseChange)
                        score += 8;
                }

                ++qi;
            }
            else
            {
                consecutiveRun = 0;
            }
            ++fi;
        }

        // All query characters must be found as a subsequence
        if (qi < query.size())
            return -1;

        // Match ratio penalty: penalise when query is very short vs filename
        int const ratio = static_cast<int>(
            (filename.size() - query.size()) * 2 / (filename.size() + 1));
        score -= ratio;

        return (score < 0) ? 0 : score;
    }

} // namespace FileScoutNative::Search
```

---

- [ ] **Step 1.5 — Register FuzzyMatcher.h in the main project vcxproj**

Open `native/FileScout.Native/FileScout.Native.vcxproj`. Find the `<ClInclude Include="Shell\ShellInteropService.h" />` line. Add after it:

```xml
    <ClInclude Include="Search\FuzzyMatcher.h" />
```

---

- [ ] **Step 1.6 — Register FuzzyMatcherTests.cpp in the test project vcxproj**

Open `native/FileScout.Tests/FileScout.Tests.vcxproj`. Find the `<ClCompile Include="tests\ShellFileOperationServiceTests.cpp" />` line. Add after it:

```xml
    <ClCompile Include="tests\FuzzyMatcherTests.cpp" />
```

---

- [ ] **Step 1.7 — Build**

Run the build command. Expected: SUCCESS with zero new warnings (MSB8027/LNK4042 pre-existing exceptions are acceptable).

---

- [ ] **Step 1.8 — Run FuzzyMatcher tests**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_filter=FuzzyMatcher*
```

Expected: all 7 FuzzyMatcher tests PASS.

---

- [ ] **Step 1.9 — Run full test suite to confirm no regression**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe
```

Expected: all tests PASS (57 pre-existing + 7 new = 64 total).

---

- [ ] **Step 1.10 — Commit**

```bash
git add native/FileScout.Native/Search/FuzzyMatcher.h \
        native/FileScout.Native/FileScout.Native.vcxproj \
        native/FileScout.Tests/pch.h \
        native/FileScout.Tests/tests/FuzzyMatcherTests.cpp \
        native/FileScout.Tests/FileScout.Tests.vcxproj
git commit -m "feat(search): add FuzzyMatcher header and unit tests (R17a)"
```

---

## Task 2: SearchService — background walk + unit tests (R17b)

**Files:**
- Create: `native/FileScout.Native/Search/SearchService.h`
- Create: `native/FileScout.Native/Search/SearchService.cpp`
- Create: `native/FileScout.Tests/tests/SearchServiceTests.cpp`
- Modify: `native/FileScout.Native/FileScout.Native.vcxproj`
- Modify: `native/FileScout.Tests/FileScout.Tests.vcxproj`

---

- [ ] **Step 2.1 — Write the failing SearchService tests**

Create `native/FileScout.Tests/tests/SearchServiceTests.cpp`:

```cpp
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
// Test 3: Cap enforcement — 500 synthetic paths, heap capped at 200
// ---------------------------------------------------------------------------
TEST(SearchService, CapEnforcement)
{
    // Build a temp directory with 500 files named "match_NNN.txt"
    auto tmpDir = std::filesystem::temp_directory_path() /
        (L"FileScoutSearchTest-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir);

    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); }
    } cleanup{ tmpDir };

    for (int i = 0; i < 500; ++i)
    {
        std::ofstream f(tmpDir / (L"match_" + std::to_wstring(i) + L".txt"));
    }

    std::vector<SearchResult> allResults;
    bool cap{ false };
    std::latch done(1);

    SearchService svc;
    svc.Start(
        tmpDir.wstring(),
        L"ma",  // matches all "match_*.txt" files
        [&allResults](std::vector<SearchResult> r) { allResults = std::move(r); },
        [&cap, &done](bool c) { cap = c; done.count_down(); },
        [](std::function<void()> fn) { fn(); });

    done.wait(std::chrono::seconds(10));

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

    done.wait(std::chrono::seconds(10));
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

    done.wait(std::chrono::seconds(10));
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

    done.wait(std::chrono::seconds(10));

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
```

---

- [ ] **Step 2.2 — Create `SearchService.h`**

Create `native/FileScout.Native/Search/SearchService.h`:

```cpp
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace FileScoutNative::Search
{
    struct SearchResult
    {
        std::wstring full_path;
        std::wstring filename;
        int          score;
    };

    class SearchService
    {
    public:
        using ResultCallback   = std::function<void(std::vector<SearchResult>)>;
        using CompleteCallback = std::function<void(bool cap_reached)>;

        // PostFn: schedules a void() callback on the UI thread.
        // Production: caller wraps DispatcherQueue::TryEnqueue.
        // Tests: direct synchronous call lambda (no dispatcher required).
        using PostFn = std::function<void(std::function<void()>)>;

        SearchService()  = default;
        ~SearchService();

        SearchService(SearchService const&)            = delete;
        SearchService& operator=(SearchService const&) = delete;

        // Signals any running walk to stop (non-blocking), then starts a new walk.
        // Safe to call from the UI thread.
        void Start(std::wstring root, std::wstring query,
                   ResultCallback on_results, CompleteCallback on_complete,
                   PostFn post_fn);

        // Signals the walk to stop. Non-blocking. Safe to call from the UI thread.
        void Cancel();

    private:
        static constexpr uint32_t k_max_depth    = 12;
        static constexpr std::size_t k_heap_cap  = 200;
        static constexpr int k_batch_interval_ms = 50;

        std::shared_ptr<std::atomic<bool>> m_cancelled_flag;
        std::atomic<uint64_t>              m_generation{ 0 };
        bool                               m_cap_reached{ false };
        std::thread                        m_thread;

        // Internal walk called on the background thread.
        void WalkDir(
            std::filesystem::path const& dir,
            uint32_t current_depth,
            std::wstring const& query_lower,
            std::vector<std::pair<int, std::wstring>>& heap,   // min-heap {score, path}
            std::shared_ptr<std::atomic<bool>> const& cancelled,
            uint64_t generation,
            bool& cap_reached);
    };

} // namespace FileScoutNative::Search
```

---

- [ ] **Step 2.3 — Create `SearchService.cpp`**

Create `native/FileScout.Native/Search/SearchService.cpp`:

```cpp
#include "pch.h"
#include "Search/SearchService.h"
#include "Search/FuzzyMatcher.h"

#include <algorithm>
#include <chrono>

namespace FileScoutNative::Search
{
    SearchService::~SearchService()
    {
        if (m_cancelled_flag) *m_cancelled_flag = true;
        ++m_generation;
        if (m_thread.joinable()) m_thread.join();
    }

    void SearchService::Cancel()
    {
        if (m_cancelled_flag) *m_cancelled_flag = true;
        ++m_generation;
        // Non-blocking: thread exits at its next cancellation checkpoint.
    }

    void SearchService::Start(
        std::wstring root,
        std::wstring query,
        ResultCallback on_results,
        CompleteCallback on_complete,
        PostFn post_fn)
    {
        // Signal old walk and detach it (safe: old thread owns its shared_ptr).
        if (m_cancelled_flag) *m_cancelled_flag = true;
        ++m_generation;
        if (m_thread.joinable()) m_thread.detach();

        // Fresh cancellation token for this walk.
        m_cancelled_flag = std::make_shared<std::atomic<bool>>(false);
        m_cap_reached    = false;

        auto cancelled   = m_cancelled_flag; // shared ownership — valid after detach
        auto generation  = m_generation.load();

        m_thread = std::thread([this, root = std::move(root), query = std::move(query),
                                on_results = std::move(on_results),
                                on_complete = std::move(on_complete),
                                post_fn = std::move(post_fn),
                                cancelled, generation]() mutable
        {
            // Convert query to lowercase once for the whole walk.
            std::wstring query_lower(query.size(), L'\0');
            std::transform(query.begin(), query.end(), query_lower.begin(), ::towlower);

            // Min-heap: pair<score, absolute_path_string>.
            // We keep a MAX of k_heap_cap entries; it is a MIN-heap so we can
            // cheaply evict the lowest-scoring entry when adding a better one.
            std::vector<std::pair<int, std::wstring>> heap;
            heap.reserve(k_heap_cap + 1);

            bool cap_reached = false;

            auto lastFlush = std::chrono::steady_clock::now();

            auto flush = [&]()
            {
                if (*cancelled || m_generation.load() != generation) return;

                // Sort heap descending by score for display.
                auto sorted = heap;
                std::sort(sorted.begin(), sorted.end(),
                    [](auto const& a, auto const& b) { return a.first > b.first; });

                std::vector<SearchResult> results;
                results.reserve(sorted.size());
                for (auto const& [score, path] : sorted)
                {
                    SearchResult r;
                    r.full_path = path;
                    r.filename  = std::filesystem::path(path).filename().wstring();
                    r.score     = score;
                    results.push_back(std::move(r));
                }

                auto gen = generation;
                post_fn([on_results, results = std::move(results), &m_gen = m_generation, gen]() mutable
                {
                    if (m_gen.load() != gen) return;
                    on_results(std::move(results));
                });
            };

            // Recursive walk via WalkDir helper.
            WalkDir(std::filesystem::path(root), 0, query_lower, heap, cancelled, generation, cap_reached);

            if (!*cancelled && m_generation.load() == generation)
            {
                // Final flush.
                flush();

                auto gen = generation;
                post_fn([on_complete, cap_reached, &m_gen = m_generation, gen]() mutable
                {
                    if (m_gen.load() != gen) return;
                    on_complete(cap_reached);
                });
            }
        });
    }

    void SearchService::WalkDir(
        std::filesystem::path const& dir,
        uint32_t current_depth,
        std::wstring const& query_lower,
        std::vector<std::pair<int, std::wstring>>& heap,
        std::shared_ptr<std::atomic<bool>> const& cancelled,
        uint64_t generation,
        bool& cap_reached)
    {
        if (current_depth >= k_max_depth) return;
        if (*cancelled || m_generation.load() != generation) return;

        std::error_code ec;
        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return; // access denied or other error — skip silently

        for (auto const& entry : it)
        {
            if (*cancelled || m_generation.load() != generation) return;

            if (entry.is_directory(ec))
            {
                if (!ec)
                    WalkDir(entry.path(), current_depth + 1, query_lower, heap, cancelled, generation, cap_reached);
                continue;
            }

            // Score the filename (not the full path — filename-only matching per spec).
            auto const filename = entry.path().filename().wstring();
            int const score = FuzzyScore(query_lower, filename);
            if (score < 0) continue;

            if (heap.size() < k_heap_cap)
            {
                heap.emplace_back(score, entry.path().wstring());
                // Maintain min-heap property.
                std::push_heap(heap.begin(), heap.end(),
                    [](auto const& a, auto const& b) { return a.first > b.first; });
            }
            else if (score > heap.front().first) // strictly higher — displace min
            {
                std::pop_heap(heap.begin(), heap.end(),
                    [](auto const& a, auto const& b) { return a.first > b.first; });
                heap.back() = { score, entry.path().wstring() };
                std::push_heap(heap.begin(), heap.end(),
                    [](auto const& a, auto const& b) { return a.first > b.first; });
                cap_reached = true;
            }
        }
    }

} // namespace FileScoutNative::Search
```

> **Note:** The `flush` lambda in `Start` needs access to `m_generation` from the thread body. Since the thread captures `this` and `generation`, this is fine — `m_generation` is an `std::atomic` member, safe to read from the background thread as long as the `SearchService` object lives. The destructor joins if joinable, so the object outlives the thread.

---

- [ ] **Step 2.4 — Register new files in the main project vcxproj**

In `native/FileScout.Native/FileScout.Native.vcxproj`:

After `<ClInclude Include="Search\FuzzyMatcher.h" />`, add:
```xml
    <ClInclude Include="Search\SearchService.h" />
```

After `<ClCompile Include="Shell\ShellInteropService.cpp" />`, add:
```xml
    <ClCompile Include="Search\SearchService.cpp" />
```

---

- [ ] **Step 2.5 — Register SearchService in the test project vcxproj**

In `native/FileScout.Tests/FileScout.Tests.vcxproj`:

In the "Production sources" ItemGroup, after `<ClCompile Include="..\FileScout.Native\Shell\ShellFileOperationService.cpp" />`, add:
```xml
    <ClCompile Include="..\FileScout.Native\Search\SearchService.cpp" />
```

In the "Test sources" ItemGroup, after `<ClCompile Include="tests\FuzzyMatcherTests.cpp" />`, add:
```xml
    <ClCompile Include="tests\SearchServiceTests.cpp" />
```

---

- [ ] **Step 2.6 — Add `<thread>` to the main project pch.h**

Open `native/FileScout.Native/pch.h`. Add `<thread>`, `<functional>`, `<atomic>`, and `<memory>` if not already present. (Check the file first — do not duplicate.)

---

- [ ] **Step 2.7 — Build**

Run the build command. Fix any compile errors before proceeding. Common issues:
- `min`/`max` macro conflicts with `std::` — use `(std::min)` / `(std::max)` pattern
- Incomplete include paths — verify the main project's `pch.h` has `<filesystem>`, `<thread>`, `<atomic>`

---

- [ ] **Step 2.8 — Run SearchService tests**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe --gtest_filter=SearchService*
```

Expected: all 7 SearchService tests PASS. Tests 3–6 create temp directories and delete them.

---

- [ ] **Step 2.9 — Run full test suite**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe
```

Expected: all tests PASS (64 + 7 = 71 total).

---

- [ ] **Step 2.10 — Commit**

```bash
git add native/FileScout.Native/Search/SearchService.h \
        native/FileScout.Native/Search/SearchService.cpp \
        native/FileScout.Native/FileScout.Native.vcxproj \
        native/FileScout.Native/pch.h \
        native/FileScout.Tests/tests/SearchServiceTests.cpp \
        native/FileScout.Tests/FileScout.Tests.vcxproj
git commit -m "feat(search): add SearchService background walk and unit tests (R17b)"
```

---

## Task 3: SearchFilter mode — address bar state machine (R17c)

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.h` (add `SearchMode`, `PaneContext` fields)
- Modify: `native/FileScout.Native/MainWindow.xaml` (add Ctrl+F `KeyboardAccelerator`)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp` (Ctrl+F handler, TextChanged, Escape, pane/tab cancel guards)

---

- [ ] **Step 3.1 — Add `SearchMode` enum and search fields to `PaneContext`**

Open `native/FileScout.Native/MainWindow.xaml.h`. After the `enum class ViewMode { Cards, List };` line, add:

```cpp
    // Search state for the address bar.
    enum class SearchMode { None, Filter, Recursive };
```

Inside `struct PaneContext`, after the `bool user_interacted_after_first_batch{ false };` field, add:

```cpp
        // --- Search state (R17) ---
        SearchMode search_mode{ SearchMode::None };
        std::wstring search_query;
        // Snapshot of current tab's items taken at Filter entry.
        // Used for both scoring and Escape-restore; avoids live pane.items divergence.
        std::vector<::FileScoutNative::Domain::ShellItemRef> search_snapshot;
```

Also add a forward declaration for `SearchService` include at the top of the file (after the existing `#include` block):

```cpp
#include "Search/SearchService.h"
```

And inside `struct PaneContext`, after `search_snapshot`, add:

```cpp
        ::FileScoutNative::Search::SearchService search_service;
```

---

- [ ] **Step 3.2 — Add Ctrl+F `KeyboardAccelerator` to the XAML**

Open `native/FileScout.Native/MainWindow.xaml`. Add a `Window.KeyboardAccelerators` block immediately after the `<Window` attributes and before `<Grid>`:

```xml
    <Window.KeyboardAccelerators>
        <KeyboardAccelerator Key="F"
                             Modifiers="Control"
                             Invoked="SearchAccelerator_Invoked" />
    </Window.KeyboardAccelerators>
```

---

- [ ] **Step 3.3 — Add `SearchAccelerator_Invoked` declaration to `MainWindow.xaml.h`**

In the `MainWindow` struct, in the public handler declarations section, add:

```cpp
        void SearchAccelerator_Invoked(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void AddressTextBox_TextChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
```

---

- [ ] **Step 3.4 — Wire `TextChanged` in the XAML**

In `MainWindow.xaml`, on the `AddressTextBox` element, add the `TextChanged` attribute:

```xml
                        <TextBox x:Name="AddressTextBox"
                                 ...
                                 KeyDown="AddressTextBox_KeyDown"
                                 TextChanged="AddressTextBox_TextChanged" />
```

---

- [ ] **Step 3.5 — Implement `SearchAccelerator_Invoked` in `MainWindow.xaml.cpp`**

Add near the other accelerator/keyboard handlers:

```cpp
void MainWindow::SearchAccelerator_Invoked(
    IInspectable const&,
    KeyboardAcceleratorInvokedEventArgs const& args)
{
    args.Handled(true);

    auto& pane = ActivePane();

    // Disabled while staged load is pending — snapshot would be incomplete.
    if (pane.awaiting_final_batch) return;

    // Ignored when already in Filter or Recursive mode.
    if (pane.search_mode != SearchMode::None) return;

    // Transition to Filter mode.
    pane.search_mode = SearchMode::Filter;
    pane.search_query.clear();

    // Snapshot the current tab's items (value copy — independent of ListView mutations).
    auto const tabIndex = pane.active_tab_index;
    pane.search_snapshot = pane.tabs[tabIndex].items;

    // Clear address bar and set placeholder.
    auto const folderName = pane.current_path.filename().empty()
        ? pane.current_path.wstring()
        : pane.current_path.filename().wstring();
    AddressTextBox().Text(L"");
    AddressTextBox().PlaceholderText(L"Search in " + folderName + L"\u2026");
    AddressTextBox().Focus(FocusState::Programmatic);
}
```

---

- [ ] **Step 3.6 — Implement `AddressTextBox_TextChanged`**

Add the handler:

```cpp
void MainWindow::AddressTextBox_TextChanged(
    IInspectable const&,
    TextChangedEventArgs const&)
{
    auto& pane = ActivePane();
    if (pane.search_mode != SearchMode::Filter) return;

    std::wstring query{ AddressTextBox().Text() };
    pane.search_query = query;

    auto& rows = pane.tab_rows[pane.active_tab_index];
    rows.Clear();

    if (query.size() < 2)
    {
        // Show all snapshot items unfiltered.
        bool const listMode = (pane.view_mode == ViewMode::List);
        for (auto const& item : pane.search_snapshot)
            rows.Append(CreateRowView(item, listMode));
        return;
    }

    // Score all snapshot items and collect top 200.
    std::vector<std::pair<int, ::FileScoutNative::Domain::ShellItemRef const*>> scored;
    scored.reserve(pane.search_snapshot.size());
    for (auto const& item : pane.search_snapshot)
    {
        int s = ::FileScoutNative::Search::FuzzyScore(query, item.name);
        if (s >= 0)
            scored.emplace_back(s, &item);
    }
    std::sort(scored.begin(), scored.end(),
        [](auto const& a, auto const& b) { return a.first > b.first; });
    if (scored.size() > 200) scored.resize(200);

    bool const listMode = (pane.view_mode == ViewMode::List);
    for (auto const& [score, itemPtr] : scored)
        rows.Append(CreateRowView(*itemPtr, listMode));
}
```

---

- [ ] **Step 3.7 — Update `AddressTextBox_KeyDown` to handle Escape in Filter mode**

In `MainWindow.xaml.cpp`, find `AddressTextBox_KeyDown`. At the top of the handler, add:

```cpp
    auto& pane = ActivePane();

    if (args.Key() == VirtualKey::Escape)
    {
        if (pane.search_mode == SearchMode::Filter || pane.search_mode == SearchMode::Recursive)
        {
            args.Handled(true);
            // Cancel any background walk.
            pane.search_service.Cancel();
            pane.search_mode = SearchMode::None;
            pane.search_query.clear();

            // Restore ListView from snapshot.
            auto& rows = pane.tab_rows[pane.active_tab_index];
            rows.Clear();
            bool const listMode = (pane.view_mode == ViewMode::List);
            for (auto const& item : pane.search_snapshot)
                rows.Append(CreateRowView(item, listMode));

            // Restore address bar.
            AddressTextBox().IsReadOnly(false);
            AddressTextBox().PlaceholderText(L"Enter a filesystem path");
            AddressTextBox().Text(pane.current_path.wstring());
            return;
        }
    }
```

The Enter key guard (Recursive transition) will be added in Task 4.

---

- [ ] **Step 3.8 — Cancel search on pane-focus switch**

In `MainWindow.xaml.cpp`, find `SetActivePaneIndex`. At the very start of the function body, add:

```cpp
    // Exit search mode on the outgoing pane before switching.
    auto& outgoing = m_panes[m_activePaneIndex];
    if (outgoing.search_mode != SearchMode::None)
    {
        outgoing.search_service.Cancel();
        outgoing.search_mode = SearchMode::None;
        outgoing.search_query.clear();
        // (ListView restore happens implicitly when the pane regains focus later.)
    }
```

---

- [ ] **Step 3.9 — Cancel search on tab switch**

In `PaneTabView_SelectionChanged`, at the top of the handler (before the existing tab-switch logic), add:

```cpp
    auto& pane = m_panes[PaneIndexForTabView(sender.as<TabView>())];
    if (pane.search_mode != SearchMode::None)
    {
        pane.search_service.Cancel();
        pane.search_mode = SearchMode::None;
        pane.search_query.clear();
    }
```

---

- [ ] **Step 3.10 — Build**

Run the build command. This is the first WinRT-aware task — verify the new `KeyboardAccelerator` XAML attribute compiles without errors.

**Known pitfall:** If the TextBox swallows Ctrl+F before the `Window.KeyboardAccelerators` fires, add `ProcessKeyboardAccelerators` handling on the TextBox. Check this during manual testing in Task 5.

---

- [ ] **Step 3.11 — Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.h \
        native/FileScout.Native/MainWindow.xaml \
        native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(search): add SearchFilter mode — address bar state machine (R17c)"
```

---

## Task 4: SearchRecursive mode — Enter escalation + streaming results (R17d)

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml` (add per-pane `ProgressBar`)
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp` (Enter guard, result streaming, status bar)

---

- [ ] **Step 4.1 — Add per-pane `ProgressBar` to XAML**

In `MainWindow.xaml`, the left pane `<Grid>` currently has 2 rows (sort header + TabView). Change it to 3 rows by adding a `ProgressBar` row between them.

**Left pane — find this block:**
```xml
            <Border Grid.Column="1" x:Name="LeftPaneBorder" ...>
                <Grid>
                    <Grid.RowDefinitions>
                        <RowDefinition Height="Auto" />
                        <RowDefinition Height="*" />
                    </Grid.RowDefinitions>
                    <Border x:Name="LeftPaneSortHeader" Grid.Row="0" .../>
                    <TabView x:Name="LeftPaneTabView" Grid.Row="1" .../>
                </Grid>
            </Border>
```

**Replace with:**
```xml
            <Border Grid.Column="1" x:Name="LeftPaneBorder" ...>
                <Grid>
                    <Grid.RowDefinitions>
                        <RowDefinition Height="Auto" />
                        <RowDefinition Height="Auto" />
                        <RowDefinition Height="*" />
                    </Grid.RowDefinitions>
                    <Border x:Name="LeftPaneSortHeader" Grid.Row="0" .../>
                    <ProgressBar x:Name="LeftPaneSearchProgress"
                                 Grid.Row="1"
                                 IsIndeterminate="True"
                                 Visibility="Collapsed" />
                    <TabView x:Name="LeftPaneTabView" Grid.Row="2" .../>
                </Grid>
            </Border>
```

Repeat the same change for the **right pane** (`RightPaneBorder` / `RightPaneTabView`), using `x:Name="RightPaneSearchProgress"` and `Grid.Row="2"` for the TabView.

---

- [ ] **Step 4.2 — Add `ProgressBar` accessor helper to `MainWindow.xaml.h`**

In the private section of `MainWindow`, add:

```cpp
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::ProgressBar SearchProgressForPane(int32_t paneIndex);
```

---

- [ ] **Step 4.3 — Implement `SearchProgressForPane`**

In `MainWindow.xaml.cpp`:

```cpp
ProgressBar MainWindow::SearchProgressForPane(int32_t paneIndex)
{
    return (paneIndex == 0) ? LeftPaneSearchProgress() : RightPaneSearchProgress();
}
```

---

- [ ] **Step 4.4 — Add Enter escalation to `AddressTextBox_KeyDown`**

In `AddressTextBox_KeyDown`, after the Escape block added in Task 3, add:

```cpp
    if (args.Key() == VirtualKey::Enter && pane.search_mode == SearchMode::Filter)
    {
        args.Handled(true);  // Prevent normal path-navigation from firing.

        if (pane.search_query.size() < 2) return; // guard: query too short

        pane.search_mode = SearchMode::Recursive;
        AddressTextBox().IsReadOnly(true);
        AddressTextBox().Text(L"Search: " + pane.search_query);

        // Show per-pane progress bar.
        auto progress = SearchProgressForPane(m_activePaneIndex);
        progress.Visibility(Visibility::Visible);

        // Update status bar.
        StatusTextBlock().Text(L"0 matches");

        // Build the PostFn for this search session.
        auto paneIndex = m_activePaneIndex;
        auto post = [dq = m_dispatcherQueue](std::function<void()> fn)
        {
            dq.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                          std::move(fn));
        };

        // Capture count for live status updates.
        auto resultCount = std::make_shared<std::atomic<int>>(0);

        pane.search_service.Start(
            pane.current_path.wstring(),
            pane.search_query,
            // on_results: replace ListView with current top-N batch
            [this, paneIndex, resultCount](std::vector<::FileScoutNative::Search::SearchResult> results)
            {
                auto& p = m_panes[paneIndex];
                if (p.search_mode != SearchMode::Recursive) return;

                *resultCount = static_cast<int>(results.size());
                StatusTextBlock().Text(std::to_wstring(*resultCount) + L" matches");

                auto& rows = p.tab_rows[p.active_tab_index];
                rows.Clear();
                bool const listMode = (p.view_mode == ViewMode::List);
                for (auto const& r : results)
                {
                    // Build a temporary ShellItemRef for display.
                    // TODO R17e: add relative-path subtitle to row view.
                    ::FileScoutNative::Domain::ShellItemRef tmp;
                    tmp.name      = r.filename;
                    tmp.full_path = std::filesystem::path(r.full_path);
                    rows.Append(CreateRowView(tmp, listMode));
                }
            },
            // on_complete
            [this, paneIndex, resultCount](bool cap_reached)
            {
                auto& p = m_panes[paneIndex];
                if (p.search_mode != SearchMode::Recursive) return;

                SearchProgressForPane(paneIndex).Visibility(Visibility::Collapsed);

                if (cap_reached)
                    StatusTextBlock().Text(
                        L"Showing top 200 matches \u2014 refine your query to narrow results");
                else
                    StatusTextBlock().Text(
                        std::to_wstring(resultCount->load()) + L" matches in " +
                        p.current_path.wstring());
            },
            post);

        return;
    }
```

---

- [ ] **Step 4.5 — Hide ProgressBar when Escape exits Recursive mode**

The Escape handler added in Task 3 already calls `pane.search_service.Cancel()` and transitions to None. Add one line to also hide the progress bar. Find the Escape block and add:

```cpp
            SearchProgressForPane(m_activePaneIndex).Visibility(Visibility::Collapsed);
```

---

- [ ] **Step 4.6 — Cancel search on navigation (LoadDirectory)**

In `LoadDirectory`, at the very start, add:

```cpp
    // Exit search mode before navigating.
    auto& pane = ActivePane();
    if (pane.search_mode != SearchMode::None)
    {
        pane.search_service.Cancel();
        pane.search_mode = SearchMode::None;
        pane.search_query.clear();
        AddressTextBox().IsReadOnly(false);
        AddressTextBox().PlaceholderText(L"Enter a filesystem path");
        SearchProgressForPane(m_activePaneIndex).Visibility(Visibility::Collapsed);
    }
```

---

- [ ] **Step 4.7 — Build**

Run the build command.

---

- [ ] **Step 4.8 — Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml \
        native/FileScout.Native/MainWindow.xaml.h \
        native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(search): add SearchRecursive mode — Enter escalation, streaming results (R17d)"
```

---

## Task 5: Result row polish + navigate-on-select (R17e)

**Files:**
- Modify: `native/FileScout.Native/MainWindow.xaml.cpp` (bold highlights, relative-path subtitle, double-click navigation)

---

- [ ] **Step 5.1 — Add a search-aware row builder**

In `MainWindow.xaml.cpp`, in the `on_results` callback added in Task 4, replace the `CreateRowView(tmp, listMode)` call with a call to a new helper `CreateSearchRowView` that adds the relative-path subtitle. Add the helper:

```cpp
// Creates a list-mode row with the filename (plain, for now) and a
// relative-path subtitle showing where in the tree the file lives.
winrt::Windows::Foundation::IInspectable MainWindow::CreateSearchRowView(
    ::FileScoutNative::Search::SearchResult const& result,
    std::filesystem::path const& searchRoot)
{
    // Build relative path for subtitle (e.g. "src\components\")
    std::wstring subtitle;
    std::error_code ec;
    auto rel = std::filesystem::relative(
        std::filesystem::path(result.full_path).parent_path(), searchRoot, ec);
    if (!ec && !rel.empty() && rel.wstring() != L".")
        subtitle = rel.wstring() + L"\\";

    // Reuse existing list-mode row structure: StackPanel > TextBlock (name) + TextBlock (subtitle)
    StackPanel panel;
    panel.Margin({ 12, 4, 12, 4 });

    TextBlock nameText;
    nameText.Text(winrt::hstring(result.filename));
    nameText.FontSize(13);
    panel.Children().Append(nameText);

    if (!subtitle.empty())
    {
        TextBlock subText;
        subText.Text(winrt::hstring(subtitle));
        subText.FontSize(11);
        subText.Opacity(0.6);
        panel.Children().Append(subText);
    }

    return panel.as<winrt::Windows::Foundation::IInspectable>();
}
```

Add the declaration to `MainWindow.xaml.h`:
```cpp
        [[nodiscard]] winrt::Windows::Foundation::IInspectable CreateSearchRowView(
            ::FileScoutNative::Search::SearchResult const& result,
            std::filesystem::path const& searchRoot);
```

Update the `on_results` lambda in Task 4's Enter handler to call `CreateSearchRowView(r, p.current_path)` instead of `CreateRowView(tmp, listMode)`.

---

- [ ] **Step 5.2 — Store search results for navigate-on-select**

The on_results callback needs to store the `SearchResult` vector alongside the rows so double-click can look up the full path. Add a field to `PaneContext` in `MainWindow.xaml.h`:

```cpp
        // Last search result list — used for navigate-on-select in Recursive mode.
        std::vector<::FileScoutNative::Search::SearchResult> search_results;
```

In the `on_results` callback, after updating the ListView, add:
```cpp
                p.search_results = results; // keep for navigate-on-select
```

---

- [ ] **Step 5.3 — Navigate-on-select in `FileRowsListView_DoubleTapped`**

In `FileRowsListView_DoubleTapped`, add a guard at the top:

```cpp
    auto& pane = ActivePane();
    if (pane.search_mode == SearchMode::Recursive)
    {
        // Resolve which result was double-clicked by list index.
        auto idx = ResolveItemIndexFromSource(sender);
        if (!idx.has_value()) return;
        if (*idx < 0 || static_cast<std::size_t>(*idx) >= pane.search_results.size()) return;

        auto const& result = pane.search_results[static_cast<std::size_t>(*idx)];
        pane.search_service.Cancel();
        pane.search_mode = SearchMode::None;
        pane.search_query.clear();
        SearchProgressForPane(m_activePaneIndex).Visibility(Visibility::Collapsed);
        AddressTextBox().IsReadOnly(false);
        AddressTextBox().PlaceholderText(L"Enter a filesystem path");

        // Navigate to parent folder, select the file after load.
        auto fullPath = std::filesystem::path(result.full_path);
        LoadDirectory(fullPath.parent_path(), fullPath);
        return;
    }
```

---

- [ ] **Step 5.4 — Build**

Run the build command.

---

- [ ] **Step 5.5 — Run full test suite to confirm no regression**

```
native\bin\x64\Debug\Tests\FileScout.Tests.exe
```

Expected: all tests PASS.

---

- [ ] **Step 5.6 — Commit**

```bash
git add native/FileScout.Native/MainWindow.xaml.h \
        native/FileScout.Native/MainWindow.xaml.cpp
git commit -m "feat(search): result row polish, relative-path subtitle, navigate-on-select (R17e)"
```

---

## Manual Validation Checklist

After all tasks are complete, validate in the running app (`FileScoutNative.exe`):

- [ ] Ctrl+F activates filter mode; address bar shows placeholder `Search in <folder>…`
- [ ] Typing ≥2 chars filters the pane live; results re-sort by score
- [ ] Typing <2 chars shows all items unfiltered
- [ ] Escape exits filter, restores original item list
- [ ] Enter from filter mode enters Recursive mode; address bar shows `Search: <query>` (read-only)
- [ ] ProgressBar appears only in the searching pane
- [ ] Results stream in and update the ListView
- [ ] Status bar shows `<N> matches` during walk; final count or cap message on completion
- [ ] Cap message appears only on large directories (try `C:\Windows\System32` with query `ll`)
- [ ] Escape from Recursive mode exits cleanly; ProgressBar hides
- [ ] Ctrl+F while in Filter/Recursive: no-op
- [ ] Double-click a search result navigates to the file's parent and selects it
- [ ] Clicking the other pane exits search mode on the first pane
- [ ] Switching tabs exits search mode
- [ ] Navigating via sidebar exits search mode
- [ ] **Ctrl+F pitfall check:** if Ctrl+F is swallowed by the TextBox, add `ProcessKeyboardAccelerators` override per the spec note in the Safety Guards section
- [ ] Both panes can search independently without cancelling each other

---

## Update docs/ai/ After Completion

After all slices pass and manual validation is done, update:
- `docs/ai/slices.md` — Add R17 slice entry, mark complete
- `docs/ai/plan.md` — Update Current Focus
- `docs/ai/status.md` — Add R17 status entry
