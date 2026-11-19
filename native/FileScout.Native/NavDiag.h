#pragma once

// Navigation diagnostics: per-navigation stage timings for a folder load, measurement only.
//
// Off unless BOTH hold:
//   - the binary was built with FILESCOUT_NAVDIAG defined (msbuild /p:FileScoutNavDiag=true), and
//   - FILESCOUT_NAVDIAG_OUT names an output file at process start.
// Otherwise every call is a no-op and nothing is written.
//
// The output is one anonymous JSON object per navigation (JSON Lines): durations in microseconds,
// item counts and path-taken flags. It never contains a file name, a folder path or any other text
// taken from the file system.
//
// "model" spans are code timestamps (steady_clock around our own calls). The only frame signal is the
// second CompositionTarget::Rendering callback after the last publish: a pre-render callback, not a
// verified present, so every record says onscreen_verified=false.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace FileScoutNative::NavDiag
{
#ifdef FILESCOUT_NAVDIAG
    inline constexpr bool kCompiledIn = true;
#else
    inline constexpr bool kCompiledIn = false;
#endif

    using Clock = std::chrono::steady_clock;
    using Tick = Clock::time_point;  // Tick{} means "not reached"

    enum class Source { Other, Sidebar };
    enum class Kind { Folder, RecycleBin, Archive };
    enum class Outcome { Completed, Superseded, Failed };
    enum class StagedTrigger { None, Rendering2, FallbackTimer };

    [[nodiscard]] inline Tick Now() noexcept { return Clock::now(); }

    // Sub-stage breakdown of a sidebar rebuild triggered during a click.
    struct SidebarRebuildTiming
    {
        long long total_us = -1;
        long long favorites_fetch_us = -1;
        long long icons_us = -1;
        long long places_ui_us = -1;
        long long bookmarks_us = -1;
        long long thispc_us = -1;
    };

    // One navigation, carried by value through the load's lambdas (no shared mutable state).
    struct Trace
    {
        bool active = false;
        std::uint64_t nav_id = 0;
        Source source = Source::Other;
        Kind kind = Kind::Folder;
        int pane = 0;
        bool sidebar_rebuilt = false;   // the click also rebuilt the sidebar (recents changed)
        bool staged = false;            // first batch + deferred full publish
        StagedTrigger staged_trigger = StagedTrigger::None;
        bool enqueue_ok = true;         // DispatcherQueue::TryEnqueue result for the publish
        bool frame_observed = false;    // Rendering#2 after the last publish arrived before the timeout
        Outcome outcome = Outcome::Completed;
        std::size_t items = 0;
        std::size_t first_batch_rows = 0;
        long long recents_update_us = -1;
        SidebarRebuildTiming sidebar_timing{};

        Tick click{};                // sidebar Click handler entry (Source::Sidebar only)
        Tick load_start{};           // LoadDirectoryAsync entry (UI thread)
        Tick pre_reset_begin{};      // rows.Clear() of the folder being left
        Tick pre_reset_end{};
        Tick ui_pre_done{};          // UI-thread work done, about to resume on the thread pool
        Tick enumerate_begin{};
        Tick enumerate_end{};
        Tick sort_begin{};
        Tick sort_end{};
        Tick rows_begin{};           // FileRowViewModel construction
        Tick rows_end{};
        Tick enqueue{};              // TryEnqueue of the publish callback
        Tick callback{};             // publish callback entry on the UI thread
        Tick reset_begin{};          // rows.Clear() inside the publish callback
        Tick reset_end{};
        Tick first_append_end{};     // first batch appended
        Tick first_publish{};        // first batch + ApplyPaneStateToRows + chrome done
        Tick final_callback{};       // staged: deferred full-publish callback entry
        Tick replace_end{};          // staged: ReplaceAll done
        Tick final_publish{};        // full list published (== first_publish when not staged)
        Tick frame_after_final{};    // Rendering#2 after final_publish (not a verified present)
    };

    // Microseconds from a to b, or -1 when either end was not reached.
    [[nodiscard]] inline long long SpanUs(Tick const a, Tick const b) noexcept
    {
        if (a == Tick{} || b == Tick{} || b < a)
        {
            return -1;
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    }

    [[nodiscard]] inline char const* ToString(Source const s) noexcept
    {
        return s == Source::Sidebar ? "sidebar" : "other";
    }
    [[nodiscard]] inline char const* ToString(Kind const k) noexcept
    {
        switch (k)
        {
        case Kind::RecycleBin: return "recycle_bin";
        case Kind::Archive: return "archive";
        default: return "folder";
        }
    }
    [[nodiscard]] inline char const* ToString(Outcome const o) noexcept
    {
        switch (o)
        {
        case Outcome::Superseded: return "superseded";
        case Outcome::Failed: return "failed";
        default: return "completed";
        }
    }
    [[nodiscard]] inline char const* ToString(StagedTrigger const tr) noexcept
    {
        switch (tr)
        {
        case StagedTrigger::Rendering2: return "rendering_2";
        case StagedTrigger::FallbackTimer: return "fallback_timer";
        default: return "none";
        }
    }

    // Named spans, in pipeline order. Names are fixed literals; nothing user-derived.
    [[nodiscard]] inline std::vector<std::pair<char const*, long long>> ModelSpans(Trace const& t)
    {
        Tick const origin = t.click != Tick{} ? t.click : t.load_start;
        return {
            { "click_to_load_start", SpanUs(t.click, t.load_start) },
            { "recents_update", t.recents_update_us },
            { "sidebar_rebuild", t.sidebar_timing.total_us },
            { "sidebar_favorites_fetch", t.sidebar_timing.favorites_fetch_us },
            { "sidebar_icons", t.sidebar_timing.icons_us },
            { "sidebar_places_ui", t.sidebar_timing.places_ui_us },
            { "sidebar_bookmarks", t.sidebar_timing.bookmarks_us },
            { "sidebar_thispc", t.sidebar_timing.thispc_us },
            { "pre_list_reset", SpanUs(t.pre_reset_begin, t.pre_reset_end) },
            { "ui_pre_total", SpanUs(t.load_start, t.ui_pre_done) },
            { "enumerate", SpanUs(t.enumerate_begin, t.enumerate_end) },
            { "sort", SpanUs(t.sort_begin, t.sort_end) },
            { "row_build", SpanUs(t.rows_begin, t.rows_end) },
            { "dispatch_wait", SpanUs(t.enqueue, t.callback) },
            { "list_reset", SpanUs(t.reset_begin, t.reset_end) },
            { "first_append", SpanUs(t.reset_end, t.first_append_end) },
            { "first_publish_rest", SpanUs(t.first_append_end, t.first_publish) },
            { "staged_frame_wait", SpanUs(t.first_publish, t.final_callback) },
            { "final_replace_all", SpanUs(t.final_callback, t.replace_end) },
            { "final_publish_rest", SpanUs(t.replace_end, t.final_publish) },
            { "origin_to_first_publish", SpanUs(origin, t.first_publish) },
            { "origin_to_final_publish", SpanUs(origin, t.final_publish) },
        };
    }

    [[nodiscard]] inline std::string ToJsonLine(Trace const& t, std::uint64_t const seq)
    {
        auto num = [](long long const v) { return std::to_string(v); };
        auto flag = [](bool const v) { return std::string{ v ? "true" : "false" }; };
        Tick const origin = t.click != Tick{} ? t.click : t.load_start;

        std::string json = "{\"seq\":" + std::to_string(seq) +
            ",\"nav\":" + std::to_string(t.nav_id) +
            ",\"source\":\"" + ToString(t.source) + "\"" +
            ",\"kind\":\"" + ToString(t.kind) + "\"" +
            ",\"outcome\":\"" + ToString(t.outcome) + "\"" +
            ",\"pane\":" + std::to_string(t.pane) +
            ",\"items\":" + std::to_string(t.items) +
            ",\"first_batch_rows\":" + std::to_string(t.first_batch_rows) +
            ",\"staged\":" + flag(t.staged) +
            ",\"staged_trigger\":\"" + ToString(t.staged_trigger) + "\"" +
            ",\"sidebar_rebuilt\":" + flag(t.sidebar_rebuilt) +
            ",\"enqueue_ok\":" + flag(t.enqueue_ok) +
            ",\"model_us\":{";
        bool first = true;
        for (auto const& [name, us] : ModelSpans(t))
        {
            json += (first ? "\"" : ",\"") + std::string{ name } + "\":" + num(us);
            first = false;
        }
        json += "},\"frame_signal\":\"rendering_callback_2_after_final_publish\"" ;
        json += ",\"frame_observed\":" + flag(t.frame_observed);
        json += ",\"frame_us\":{\"final_publish_to_frame\":" + num(SpanUs(t.final_publish, t.frame_after_final)) +
            ",\"origin_to_frame\":" + num(SpanUs(origin, t.frame_after_final)) + "}";
        json += ",\"onscreen_verified\":false}";
        return json;
    }

    class Recorder
    {
    public:
        static Recorder& Instance()
        {
            static Recorder recorder;
            return recorder;
        }

        [[nodiscard]] bool Enabled() const noexcept { return kCompiledIn && !m_outPath.empty(); }

        // UI thread. Wraps a sidebar Click: the load started inside it picks the click up.
        void BeginSidebarClick() noexcept
        {
            if (!Enabled()) { return; }
            m_pendingClick = Now();
            m_pendingRebuilt = false;
            m_pendingRecentsUs = -1;
            m_pendingSidebarTiming = {};
        }
        void NoteRecentsUpdate(long long const us) noexcept
        {
            if (Enabled() && m_pendingClick != Tick{}) { m_pendingRecentsUs = us; }
        }
        void NoteSidebarRebuilt(SidebarRebuildTiming const& timing = {}) noexcept
        {
            if (Enabled() && m_pendingClick != Tick{})
            {
                m_pendingRebuilt = true;
                m_pendingSidebarTiming = timing;
            }
        }
        // UI thread, after the Click handler's LoadDirectory returned. A click that started no load
        // (e.g. an archive opened externally) is counted, not attributed to the next navigation.
        void EndSidebarClick()
        {
            if (!Enabled() || m_pendingClick == Tick{}) { return; }
            m_pendingClick = Tick{};
            Append("{\"seq\":" + std::to_string(++m_seq) +
                ",\"event\":\"sidebar_click_without_load\",\"onscreen_verified\":false}");
        }

        // UI thread, LoadDirectoryAsync entry. Returns an inactive trace when disabled.
        [[nodiscard]] Trace StartLoad(int const pane, Kind const kind)
        {
            Trace t;
            if (!Enabled()) { return t; }
            t.active = true;
            t.nav_id = ++m_navId;
            t.pane = pane;
            t.kind = kind;
            t.load_start = Now();
            if (m_pendingClick != Tick{})
            {
                t.source = Source::Sidebar;
                t.click = m_pendingClick;
                t.sidebar_rebuilt = m_pendingRebuilt;
                t.recents_update_us = m_pendingRecentsUs;
                t.sidebar_timing = m_pendingSidebarTiming;
                m_pendingClick = Tick{};
            }
            return t;
        }

        void Emit(Trace const& t)
        {
            if (!Enabled() || !t.active) { return; }
            Append(ToJsonLine(t, ++m_seq));
        }

    private:
        Recorder()
        {
            if constexpr (kCompiledIn)
            {
                m_outPath = ReadEnv();
            }
        }

        void Append(std::string const& line)
        {
            std::lock_guard lock{ m_mutex };
#ifdef _WIN32
            FILE* file = nullptr;
            if (_wfopen_s(&file, m_outPath.c_str(), L"ab") != 0 || file == nullptr) { return; }
#else
            FILE* file = std::fopen(m_outPath.c_str(), "ab");
            if (file == nullptr) { return; }
#endif
            std::fwrite(line.data(), 1, line.size(), file);
            std::fputc('\n', file);
            std::fclose(file);
        }

#ifdef _WIN32
        static std::wstring ReadEnv()
        {
            DWORD const size = ::GetEnvironmentVariableW(L"FILESCOUT_NAVDIAG_OUT", nullptr, 0);
            if (size == 0) { return {}; }
            std::wstring value(size, L'\0');
            DWORD const written = ::GetEnvironmentVariableW(L"FILESCOUT_NAVDIAG_OUT", value.data(), size);
            value.resize(written);
            return value;
        }
        std::wstring m_outPath;
#else
        static std::string ReadEnv()
        {
            char const* value = std::getenv("FILESCOUT_NAVDIAG_OUT");
            return value ? std::string{ value } : std::string{};
        }
        std::string m_outPath;
#endif
        Tick m_pendingClick{};           // UI thread only
        bool m_pendingRebuilt = false;   // UI thread only
        long long m_pendingRecentsUs = -1; // UI thread only
        SidebarRebuildTiming m_pendingSidebarTiming{}; // UI thread only
        std::uint64_t m_navId = 0;       // UI thread only
        std::atomic<std::uint64_t> m_seq{ 0 };
        std::mutex m_mutex;
    };
}
