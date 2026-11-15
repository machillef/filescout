#include "pch.h"
#include "Search/SearchService.h"
#include "Search/FuzzyMatcher.h"
#include "TraceHelper.h"

#include <algorithm>
#include <chrono>

namespace
{
    bool IsSupportedLocalRoot(std::filesystem::path const& path)
    {
        if (path.empty())
        {
            return false;
        }

        auto const native = path.native();
        if (native.rfind(L"\\\\", 0) == 0 || native.rfind(L"//", 0) == 0)
        {
            return false;
        }

        auto const root = path.root_path();
        if (root.empty())
        {
            return false;
        }

        switch (GetDriveTypeW(root.c_str()))
        {
        case DRIVE_FIXED:
        case DRIVE_REMOVABLE:
        case DRIVE_CDROM:
        case DRIVE_RAMDISK:
            return true;
        default:
            return false;
        }
    }
}

namespace FileScoutNative::Search
{
    SearchService::~SearchService()
    {
        if (m_cancelled_flag) *m_cancelled_flag = true;
        if (m_generation) ++(*m_generation);
        if (m_worker.thread.joinable()) m_worker.thread.join();
        for (auto& worker : m_retired_workers)
        {
            if (worker.thread.joinable()) worker.thread.join();
        }
    }

    void SearchService::ReapFinishedWorkers()
    {
        // Join first, then erase with a side-effect-free predicate (erase_if may call it any number of
        // times and on moved elements).
        for (auto& worker : m_retired_workers)
        {
            if (worker.finished->load() && worker.thread.joinable())
            {
                worker.thread.join();
            }
        }
        std::erase_if(m_retired_workers, [](Worker const& worker)
        {
            return worker.finished->load() && !worker.thread.joinable();
        });
    }

    void SearchService::Cancel()
    {
        if (m_cancelled_flag) *m_cancelled_flag = true;
        if (m_generation) ++(*m_generation);
        // Non-blocking: thread exits at its next cancellation checkpoint.
    }

    void SearchService::Start(
        std::wstring root,
        std::wstring query,
        ResultCallback on_results,
        CompleteCallback on_complete,
        PostFn post_fn)
    {
        // Signal the old walk and retire its worker; it is joined once it has finished.
        if (m_cancelled_flag) *m_cancelled_flag = true;
        if (m_generation) ++(*m_generation);
        if (m_worker.thread.joinable()) m_retired_workers.push_back(std::move(m_worker));
        m_worker = {};
        ReapFinishedWorkers();

        // Fresh cancellation token and shared generation counter for this walk.
        m_cancelled_flag = std::make_shared<std::atomic<bool>>(false);
        if (!m_generation)
        {
            m_generation = std::make_shared<std::atomic<uint64_t>>(0);
        }

        auto cancelled = m_cancelled_flag;
        auto generation_ptr = m_generation;
        auto generation = generation_ptr->load();

        auto const root_path = std::filesystem::path(root);
        if (!IsSupportedLocalRoot(root_path))
        {
            post_fn([on_complete = std::move(on_complete), generation_ptr, generation]() mutable
            {
                if (generation_ptr->load() != generation) return;
                on_complete(false, true);
            });
            return;
        }

        auto finished = std::make_shared<std::atomic<bool>>(false);
        m_worker.finished = finished;
        m_worker.thread = std::thread([root_path = std::move(root_path), query = std::move(query),
                                on_results = std::move(on_results),
                                on_complete = std::move(on_complete),
                                post_fn = std::move(post_fn),
                                cancelled, generation_ptr, generation, finished]() mutable
        {
            struct MarkFinished
            {
                std::shared_ptr<std::atomic<bool>> flag;
                ~MarkFinished() { *flag = true; }
            } markFinished{ finished };

            try
            {
                std::wstring query_lower(query.size(), L'\0');
                std::transform(query.begin(), query.end(), query_lower.begin(), ::towlower);

                std::vector<std::pair<int, std::wstring>> heap;
                heap.reserve(k_heap_cap + 1);

                bool cap_reached = false;
                bool results_dirty = false;

                auto flush = [&]()
                {
                    if (!results_dirty)
                    {
                        return;
                    }

                    if (*cancelled || generation_ptr->load() != generation) return;

                    auto sorted = heap;
                    std::sort(sorted.begin(), sorted.end(),
                        [](auto const& left, auto const& right) { return left.first > right.first; });

                    std::vector<SearchResult> results;
                    results.reserve(sorted.size());
                    for (auto const& [score, path] : sorted)
                    {
                        SearchResult result;
                        result.full_path = path;
                        result.filename = std::filesystem::path(path).filename().wstring();
                        result.score = score;
                        results.push_back(std::move(result));
                    }

                    post_fn([on_results, results = std::move(results), generation_ptr, generation]() mutable
                    {
                        if (generation_ptr->load() != generation) return;
                        on_results(std::move(results));
                    });

                    results_dirty = false;
                };

                auto last_flush = std::chrono::steady_clock::now();
                WalkDir(
                    root_path,
                    0,
                    query_lower,
                    heap,
                    cancelled,
                    generation_ptr,
                    generation,
                    cap_reached,
                    results_dirty,
                    flush,
                    last_flush);

                if (!*cancelled && generation_ptr->load() == generation)
                {
                    flush();

                    post_fn([on_complete, cap_reached, generation_ptr, generation]() mutable
                    {
                        if (generation_ptr->load() != generation) return;
                        on_complete(cap_reached, false);
                    });
                }
            }
            catch (...)
            {
                TraceLog(L"SearchService worker aborted due to unexpected exception.");
                post_fn([on_complete, generation_ptr, generation]() mutable
                {
                    if (generation_ptr->load() != generation) return;
                    on_complete(false, true);
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
        std::shared_ptr<std::atomic<uint64_t>> const& generation_ptr,
        uint64_t generation,
        bool& cap_reached,
        bool& results_dirty,
        std::function<void()> const& flush_fn,
        std::chrono::steady_clock::time_point& last_flush)
    {
        if (*cancelled || generation_ptr->load() != generation) return;

        std::error_code ec;
        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) return;

        for (auto const& entry : it)
        {
            if (*cancelled || generation_ptr->load() != generation) return;

            auto const now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush).count()
                >= k_batch_interval_ms)
            {
                flush_fn();
                last_flush = now;
            }

            auto const status = entry.symlink_status(ec);
            if (ec)
            {
                continue;
            }

            if (std::filesystem::is_symlink(status))
            {
                continue;
            }

            if (std::filesystem::is_directory(status))
            {
                if (current_depth < k_max_depth)
                {
                    WalkDir(
                        entry.path(),
                        current_depth + 1,
                        query_lower,
                        heap,
                        cancelled,
                        generation_ptr,
                        generation,
                        cap_reached,
                        results_dirty,
                        flush_fn,
                        last_flush);
                }
                continue;
            }

            auto const filename = entry.path().filename().wstring();
            int const score = FuzzyScore(query_lower, filename);
            if (score < 0) continue;

            if (heap.size() < k_heap_cap)
            {
                heap.emplace_back(score, entry.path().wstring());
                std::push_heap(heap.begin(), heap.end(),
                    [](auto const& left, auto const& right) { return left.first > right.first; });
                results_dirty = true;
            }
            else if (score > heap.front().first)
            {
                std::pop_heap(heap.begin(), heap.end(),
                    [](auto const& left, auto const& right) { return left.first > right.first; });
                heap.back() = { score, entry.path().wstring() };
                std::push_heap(heap.begin(), heap.end(),
                    [](auto const& left, auto const& right) { return left.first > right.first; });
                cap_reached = true;
                results_dirty = true;
            }
        }
    }

} // namespace FileScoutNative::Search
