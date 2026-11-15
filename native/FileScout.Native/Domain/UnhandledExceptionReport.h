#pragma once

#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    struct UnhandledExceptionReport
    {
        std::wstring title;
        std::wstring message;   // MessageBoxW body
        std::wstring log_line;  // TraceLog / startup log
    };

    // UA-09: an exception that reaches App::UnhandledException leaves the app in an unknown state
    // while it holds the user's files, so it is never marked handled: the app logs, tells the user and
    // lets WinUI terminate the process (which also produces a crash report).
    [[nodiscard]] constexpr bool ShouldMarkUnhandledExceptionHandled() noexcept
    {
        return false;
    }

    [[nodiscard]] inline UnhandledExceptionReport BuildUnhandledExceptionReport(
        std::wstring_view message,
        int32_t const hresult)
    {
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        {
            message.remove_suffix(1);
        }
        std::wstring const text = message.empty()
            ? std::wstring{ L"XAML raised an unhandled exception without a message." }
            : std::wstring{ message };

        wchar_t code[11]{};
        std::swprintf(code, std::size(code), L"0x%08X", static_cast<std::uint32_t>(hresult));

        return UnhandledExceptionReport{
            L"FileScout has to close",
            text + L"\nHRESULT: " + code +
                L"\n\nFileScout will close now so it does not keep working on your files in an unknown state.",
            L"Unhandled exception, terminating: " + text + L" (HRESULT " + code + L")",
        };
    }
}
