#pragma once

namespace FileScoutNative::Shell
{
    class ShellFileOperationService
    {
    public:
        void OpenPath(HWND ownerWindow, std::filesystem::path const& path) const;
        void OpenWith(HWND ownerWindow, std::filesystem::path const& path) const;
        void ShowProperties(HWND ownerWindow, std::filesystem::path const& path) const;
        [[nodiscard]] std::filesystem::path CreateFolder(
            HWND ownerWindow,
            std::filesystem::path const& parentDirectory,
            std::wstring const& name) const;
        [[nodiscard]] std::filesystem::path RenameItem(
            HWND ownerWindow,
            std::filesystem::path const& path,
            std::wstring const& newName) const;
        void DeleteItem(HWND ownerWindow, std::filesystem::path const& path) const;
        void PasteItems(
            HWND ownerWindow,
            IDataObject* clipboardDataObject,
            std::filesystem::path const& destinationDirectory,
            bool move) const;
        void CopyItemsFromPaths(
            HWND ownerWindow,
            std::span<std::filesystem::path const> sourcePaths,
            std::filesystem::path const& destinationDirectory,
            bool move) const;

    private:
        void ExecuteShellVerb(
            HWND ownerWindow,
            std::filesystem::path const& path,
            wchar_t const* verb) const;
        [[nodiscard]] winrt::com_ptr<IShellItem> CreateShellItem(
            std::filesystem::path const& path) const;
        [[nodiscard]] winrt::com_ptr<IFileOperation> CreateFileOperation(
            HWND ownerWindow,
            DWORD operationFlags) const;
        void PerformFileOperation(IFileOperation* fileOperation) const;
    };
}
