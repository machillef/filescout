#include "pch.h"
#include "ShellFileOperationService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        void ThrowShellExecuteFailure(std::filesystem::path const& path, wchar_t const* verb)
        {
            std::wstring message = L"ShellExecuteW failed for ";
            message += path.wstring();
            if (verb != nullptr)
            {
                message += L" (";
                message += verb;
                message += L")";
            }

            throw std::runtime_error(winrt::to_string(winrt::hstring{ message }));
        }
    }

    void ShellFileOperationService::OpenPath(HWND const ownerWindow, std::filesystem::path const& path) const
    {
        ExecuteShellVerb(ownerWindow, path, nullptr);
    }

    void ShellFileOperationService::OpenWith(HWND const ownerWindow, std::filesystem::path const& path) const
    {
        ExecuteShellVerb(ownerWindow, path, L"openas");
    }

    void ShellFileOperationService::ShowProperties(
        HWND const ownerWindow,
        std::filesystem::path const& path) const
    {
        ExecuteShellVerb(ownerWindow, path, L"properties");
    }

    void ShellFileOperationService::ExecuteShellVerb(
        HWND const ownerWindow,
        std::filesystem::path const& path,
        wchar_t const* const verb) const
    {
        auto const file = path.wstring();
        auto const result = ShellExecuteW(
            ownerWindow,
            verb,
            file.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);

        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            ThrowShellExecuteFailure(path, verb);
        }
    }
}
