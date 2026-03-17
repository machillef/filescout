#pragma once

namespace FileScoutNative::Shell
{
    class ShellFileOperationService
    {
    public:
        void OpenPath(HWND ownerWindow, std::filesystem::path const& path) const;
        void OpenWith(HWND ownerWindow, std::filesystem::path const& path) const;
        void ShowProperties(HWND ownerWindow, std::filesystem::path const& path) const;

    private:
        void ExecuteShellVerb(
            HWND ownerWindow,
            std::filesystem::path const& path,
            wchar_t const* verb) const;
    };
}
