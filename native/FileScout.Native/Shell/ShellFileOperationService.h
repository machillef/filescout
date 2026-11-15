#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <stdexcept>
#include <string>
#include <vector>

namespace FileScoutNative::Shell
{
    struct FileOperationError
    {
        std::wstring action;
        std::wstring path;
        std::wstring detail;
        // True when the user (or a shell confirmation) aborted the operation. Callers test this,
        // never the localised detail text.
        bool canceled = false;
    };

    class FileOperationException final : public std::runtime_error
    {
    public:
        explicit FileOperationException(FileOperationError error);

        [[nodiscard]] FileOperationError const& Error() const noexcept;
        [[nodiscard]] bool IsCanceled() const noexcept;

    private:
        static std::string BuildMessage(FileOperationError const& error);

        FileOperationError m_error;
    };

    // Maps the result of IFileOperation::PerformOperations plus GetAnyOperationsAborted to an
    // error, or nullopt on success. HRESULT_FROM_WIN32(ERROR_CANCELLED), COPYENGINE_E_USER_CANCELLED,
    // or the abort flag with a success HRESULT is a canceled outcome; any other failure HRESULT is
    // a failure even when the abort flag is set.
    [[nodiscard]] std::optional<FileOperationError> ClassifyFileOperationOutcome(
        HRESULT performResult,
        bool anyOperationsAborted,
        std::wstring_view action,
        std::wstring_view path);

    struct RenameRequest
    {
        std::filesystem::path path;
        std::wstring new_name;
    };

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
        [[nodiscard]] std::vector<std::filesystem::path> RenameItems(
            HWND ownerWindow,
            std::span<RenameRequest const> requests) const;
        void DeleteItem(HWND ownerWindow, std::filesystem::path const& path) const;
        void DeleteItems(
            HWND ownerWindow,
            std::span<std::filesystem::path const> paths) const;
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
        void CopyItemFromPath(
            HWND ownerWindow,
            std::filesystem::path const& sourcePath,
            std::filesystem::path const& destinationDirectory,
            std::wstring const& newName) const;

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
        // Runs the queued operations. Throws FileOperationException naming action and path (or the
        // item that failed), with IsCanceled() when the user aborted.
        void PerformFileOperation(
            IFileOperation* fileOperation,
            std::wstring_view action,
            std::wstring const& path) const;
    };
}
