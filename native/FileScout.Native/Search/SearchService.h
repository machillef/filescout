#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "Search/SearchLimits.h"

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

        using PostFn = std::function<void(std::function<void()>)>;

        SearchService()  = default;
        ~SearchService();

        SearchService(SearchService const&)            = delete;
        SearchService& operator=(SearchService const&) = delete;

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

        void Cancel();

    private:
        static constexpr uint32_t    k_max_depth = 12;
        static constexpr std::size_t k_heap_cap = k_max_search_results;
        static constexpr int         k_batch_interval_ms = 50;

        std::shared_ptr<std::atomic<bool>> m_cancelled_flag;
        std::shared_ptr<std::atomic<uint64_t>> m_generation;

        struct Worker
        {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> finished;
        };
        Worker m_worker;
        std::vector<Worker> m_retired_workers;

        void ReapFinishedWorkers();

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

}
