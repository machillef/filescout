#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

namespace FileScoutNative::Bench
{
    [[nodiscard]] inline std::optional<double> Percentile(std::vector<double> values, double const p)
    {
        if (values.empty())
        {
            return std::nullopt;
        }
        std::sort(values.begin(), values.end());
        auto const clamped = (std::clamp)(p, 0.0, 100.0);
        auto rank = static_cast<std::size_t>(std::ceil(clamped / 100.0 * static_cast<double>(values.size())));
        rank = (std::max)(rank, std::size_t{ 1 });
        return values[rank - 1];
    }

    [[nodiscard]] inline std::string JsonEscape(std::wstring_view text)
    {
        std::string out;
        for (wchar_t const c : text)
        {
            if (c == L'"' || c == L'\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c < 0x20) { out += ' '; }
            else if (c < 0x80) { out += static_cast<char>(c); }
            else { out += '?'; }
        }
        return out;
    }

    struct Report
    {
        std::vector<std::pair<std::wstring, double>> marks;
        std::vector<double> frameIntervalsMs;
        std::size_t itemCount = 0;

        [[nodiscard]] std::string ToJson() const
        {
            std::string json = "{\n  \"marks\": {";
            for (std::size_t i = 0; i < marks.size(); ++i)
            {
                char number[64]{};
                std::snprintf(number, sizeof(number), "%.3f", marks[i].second);
                json += (i == 0 ? "\n    \"" : ",\n    \"") + JsonEscape(marks[i].first) + "\": " + number;
            }
            json += "\n  },\n  \"items\": " + std::to_string(itemCount) + ",\n  \"frames\": " +
                std::to_string(frameIntervalsMs.size());
            for (auto const p : { 50.0, 95.0, 99.0, 100.0 })
            {
                char line[96]{};
                std::snprintf(line, sizeof(line), ",\n  \"frame_p%d_ms\": %.3f", static_cast<int>(p),
                    Percentile(frameIntervalsMs, p).value_or(-1.0));
                json += line;
            }
            std::size_t over = 0;
            for (auto const v : frameIntervalsMs) { over += v > 33.4 ? 1 : 0; }
            json += ",\n  \"frames_over_33ms\": " + std::to_string(over) + "\n}\n";
            return json;
        }
    };

    class Recorder
    {
    public:
        static Recorder& Instance()
        {
            static Recorder recorder;
            return recorder;
        }

        [[nodiscard]] bool Enabled() const noexcept { return !m_outPath.empty(); }

        [[nodiscard]] double NowMs() const
        {
            FILETIME now{};
            ::GetSystemTimePreciseAsFileTime(&now);
            return static_cast<double>(ToTicks(now) - m_creationTicks) / 10'000.0;
        }

        void Mark(std::wstring_view name)
        {
            if (!Enabled()) { return; }
            std::lock_guard lock{ m_mutex };
            for (auto const& [existing, _] : m_report.marks)
            {
                if (existing == name) { return; }
            }
            m_report.marks.emplace_back(std::wstring{ name }, NowMs());
        }

        [[nodiscard]] bool HasMark(std::wstring_view name) const
        {
            std::lock_guard lock{ m_mutex };
            return std::any_of(m_report.marks.begin(), m_report.marks.end(),
                [&](auto const& mark) { return mark.first == name; });
        }

        void AddFrameInterval(double ms) { if (Enabled()) { m_report.frameIntervalsMs.push_back(ms); } }
        void SetItemCount(std::size_t count) { m_report.itemCount = count; }

        [[nodiscard]] std::wstring const& TargetPath() const noexcept { return m_targetPath; }
        // Pixels scrolled per frame; FILESCOUT_BENCH_SCROLL_STEP overrides the 96 px default. A step
        // near the viewport height realizes a full page of rows per frame, as a fast mouse wheel does.
        [[nodiscard]] double ScrollStepPx() const noexcept { return m_scrollStepPx; }

        void Write() const
        {
            if (!Enabled()) { return; }
            std::string json;
            {
                std::lock_guard lock{ m_mutex };
                json = m_report.ToJson();
            }
            FILE* file = nullptr;
            if (_wfopen_s(&file, m_outPath.c_str(), L"wb") == 0 && file != nullptr)
            {
                std::fwrite(json.data(), 1, json.size(), file);
                std::fclose(file);
            }
        }

    private:
        Recorder()
        {
            m_outPath = ReadEnv(L"FILESCOUT_BENCH_OUT");
            m_targetPath = ReadEnv(L"FILESCOUT_BENCH_PATH");
            if (auto const step = ReadEnv(L"FILESCOUT_BENCH_SCROLL_STEP"); !step.empty())
            {
                m_scrollStepPx = (std::max)(1.0, std::wcstod(step.c_str(), nullptr));
            }
            FILETIME creation{}, exitTime{}, kernel{}, user{};
            ::GetProcessTimes(::GetCurrentProcess(), &creation, &exitTime, &kernel, &user);
            m_creationTicks = ToTicks(creation);
        }

        static unsigned long long ToTicks(FILETIME const& time)
        {
            return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        }

        static std::wstring ReadEnv(wchar_t const* name)
        {
            DWORD const size = ::GetEnvironmentVariableW(name, nullptr, 0);
            if (size == 0) { return {}; }
            std::wstring value(size, L'\0');
            DWORD const written = ::GetEnvironmentVariableW(name, value.data(), size);
            value.resize(written);
            return value;
        }

        std::wstring m_outPath;
        std::wstring m_targetPath;
        double m_scrollStepPx = 96.0;
        unsigned long long m_creationTicks = 0;
        Report m_report;
        mutable std::mutex m_mutex;
    };
}
