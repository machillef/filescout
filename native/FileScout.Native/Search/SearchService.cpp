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

        auto cancelled  = m_cancelled_flag; // shared ownership — valid after detach
        auto generation = m_generation.load();

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
