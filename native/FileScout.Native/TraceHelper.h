#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <windows.h>

// Appends `line` and a newline as UTF-8. A std::wofstream converts through the "C" locale and
// drops non-ASCII text (a Greek file name, a localized error message). Best effort: logging runs
// on error paths, so it never throws.
inline void AppendUtf8LogLine(std::filesystem::path const& path, std::wstring_view const line) noexcept
{
    try
    {
        std::string bytes;
        if (!line.empty())
        {
            auto const size = ::WideCharToMultiByte(
                CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                bytes.resize(static_cast<std::size_t>(size));
                ::WideCharToMultiByte(
                    CP_UTF8, 0, line.data(), static_cast<int>(line.size()), bytes.data(), size, nullptr, nullptr);
            }
        }
        bytes.push_back('\n');

        std::ofstream stream{ path, std::ios::app | std::ios::binary };
        if (stream.is_open())
        {
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
    }
    catch (...)
    {
    }
}

// Persistent, append-only trace log for startup crash diagnosis.
// Writes to %TEMP%\FileScout.Native.trace.log — never deleted.
inline void TraceLog(std::wstring const& line) noexcept
{
    // Best effort as a whole: it runs on error paths (the unhandled-exception handler), where a
    // throw from building the path or the line would skip the error dialog (UA-09).
    try
    {
        wchar_t tempPath[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tempPath) == 0) { return; }
        auto path = std::filesystem::path{ tempPath } / L"FileScout.Native.trace.log";

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        wchar_t ts[32]{};
        swprintf_s(ts, L"%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        AppendUtf8LogLine(path, std::wstring{ ts } + L" [" + std::to_wstring(::GetTickCount64()) + L"] " + line);
    }
    catch (...)
    {
    }
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
