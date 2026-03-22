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
        static constexpr uint32_t    k_max_depth    = 12;
        static constexpr std::size_t k_heap_cap     = 200;
        static constexpr int         k_batch_interval_ms = 50;

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
