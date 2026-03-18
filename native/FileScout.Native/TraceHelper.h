#pragma once
#include <fstream>
#include <string>
#include <filesystem>
#include <windows.h>

// Persistent, append-only trace log for startup crash diagnosis.
// Writes to %TEMP%\FileScout.Native.trace.log — never deleted.
inline void TraceLog(std::wstring const& line)
{
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) { return; }
    auto path = std::filesystem::path{ tempPath } / L"FileScout.Native.trace.log";
    std::wofstream stream{ path.wstring(), std::ios::app };
    if (!stream.is_open()) { return; }
    stream << L"[" << ::GetTickCount64() << L"] " << line << L"\n";
    stream.flush();
}
