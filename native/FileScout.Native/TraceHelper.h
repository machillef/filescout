#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <windows.h>

// Persistent, append-only trace log for startup crash diagnosis.
// Writes to %TEMP%\FileScout.Native.trace.log — never deleted.
inline void TraceLog(std::wstring const& line)
{
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) { return; }
    auto path = std::filesystem::path{ tempPath } / L"FileScout.Native.trace.log";
    std::wofstream stream{ path, std::ios::app };
    if (!stream.is_open()) { return; }

    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t ts[32]{};
    swprintf_s(ts, L"%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    stream << ts << L" [" << ::GetTickCount64() << L"] " << line << L"\n";
    stream.flush();
}

// RAII timer that logs "[TIMER] <label>: done in <N>ms" to sink on destruction.
// Default sink is TraceLog. Pass a custom sink for testing or alternate output.
class ScopedTimer
{
public:
    using Sink = std::function<void(std::wstring const&)>;

    explicit ScopedTimer(std::wstring label, Sink sink = TraceLog)
        : m_label(std::move(label))
        , m_sink(std::move(sink))
        , m_start(std::chrono::steady_clock::now())
    {}

    ~ScopedTimer() noexcept
    {
        auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        m_sink(L"[TIMER] " + m_label + L": done in " + std::to_wstring(ms) + L"ms");
    }

    ScopedTimer(ScopedTimer const&) = delete;
    ScopedTimer& operator=(ScopedTimer const&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

private:
    std::wstring m_label;
    Sink m_sink;
    std::chrono::steady_clock::time_point m_start;
};
