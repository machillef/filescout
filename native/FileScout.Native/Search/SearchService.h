#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
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
        using CompleteCallback = std::function<void(bool cap_reached, bool failed)>;

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

        void Start(std::wstring root, std::wstring query,
                   ResultCallback on_results, std::function<void(bool)> on_complete,
                   PostFn post_fn)
        {
            Start(
                std::move(root),
                std::move(query),
                std::move(on_results),
                [on_complete = std::move(on_complete)](bool cap_reached, bool)
                {
                    on_complete(cap_reached);
                },
                std::move(post_fn));
        }

        // Signals the walk to stop. Non-blocking. Safe to call from the UI thread.
        void Cancel();

    private:
        static constexpr uint32_t    k_max_depth = 12;
        static constexpr std::size_t k_heap_cap = 200;
        static constexpr int         k_batch_interval_ms = 50;

        std::shared_ptr<std::atomic<bool>> m_cancelled_flag;
        std::shared_ptr<std::atomic<uint64_t>> m_generation;
        std::thread m_thread;

        static void WalkDir(
            std::filesystem::path const& dir,
            uint32_t current_depth,
            std::wstring const& query_lower,
            std::vector<std::pair<int, std::wstring>>& heap,
            std::shared_ptr<std::atomic<bool>> const& cancelled,
            std::shared_ptr<std::atomic<uint64_t>> const& generation_ptr,
            uint64_t generation,
            bool& cap_reached,
            bool& results_dirty,
            std::function<void()> const& flush_fn,
            std::chrono::steady_clock::time_point& last_flush);
    };

} // namespace FileScoutNative::Search
