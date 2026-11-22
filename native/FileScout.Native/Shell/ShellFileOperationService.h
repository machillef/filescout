#pragma once

#include "..\Domain\UndoStack.h"

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
        bool canceled = false;
        HRESULT hresult = S_OK;
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

    [[nodiscard]] std::optional<FileOperationError> ClassifyFileOperationOutcome(
        HRESULT performResult,
        bool anyOperationsAborted,
        std::wstring_view action,
        std::wstring_view path);

    [[nodiscard]] DWORD DeleteOperationFlags(bool permanent) noexcept;

    // True when the shell refused to recycle an item because it is larger than the Recycle Bin
    // allows; Explorer then offers a permanent delete.
    [[nodiscard]] bool IsTooBigForRecycleBin(FileOperationError const& error) noexcept;

    [[nodiscard]] std::wstring FormatWindowsTerminalDirectoryArgs(std::filesystem::path const& directory);

    struct RenameRequest
    {
        std::filesystem::path path;
        std::wstring new_name;
    };

    struct TransferredItemPair
    {
        std::filesystem::path source;
        std::filesystem::path destination;
    };

    struct DeletedItemRecord
    {
        std::filesystem::path original_path;
        std::wstring recycle_bin_parsing_name;
    };

    // Captures a best-effort filesystem identity (volume serial, file index, creation time)
    // for an existing file or directory so Undo can verify the path still refers to the same item.
    [[nodiscard]] std::optional<FileScoutNative::Domain::FileIdentity> TryCaptureFileIdentity(
        std::filesystem::path const& path) noexcept;

    enum class RecordedIdentityCheck
    {
        Matches,
        Gone,
        Unreadable,
    };

    [[nodiscard]] RecordedIdentityCheck CheckRecordedIdentity(
        std::filesystem::path const& path,
        std::optional<FileScoutNative::Domain::FileIdentity> const& recorded) noexcept;

    // True when the copy still has the source's tree (names, sizes, file write times);
    // std::nullopt when the copy cannot be read.
    [[nodiscard]] std::optional<bool> CopyMatchesSource(
        std::filesystem::path const& source,
        std::filesystem::path const& copy) noexcept;

    [[nodiscard]] std::optional<std::string> EncodeForCodePage(std::wstring_view text, UINT codePage);

    class ShellFileOperationService
    {
    public:
        void OpenPath(HWND ownerWindow, std::filesystem::path const& path) const;
        void OpenWith(HWND ownerWindow, std::filesystem::path const& path) const;
        void ShowProperties(HWND ownerWindow, std::filesystem::path const& path) const;
        void ShowPropertiesForItems(
            HWND ownerWindow,
            std::span<std::filesystem::path const> paths) const;
        [[nodiscard]] std::filesystem::path CreateFolder(
            HWND ownerWindow,
            std::filesystem::path const& parentDirectory,
            std::wstring const& name) const;
        [[nodiscard]] std::filesystem::path CreateEmptyFile(
            HWND ownerWindow,
            std::filesystem::path const& parentDirectory,
            std::wstring const& name) const;
        void OpenInTerminal(HWND ownerWindow, std::filesystem::path const& directory) const;
        [[nodiscard]] std::filesystem::path RenameItem(
            HWND ownerWindow,
            std::filesystem::path const& path,
            std::wstring const& newName) const;
        // Fills renamedOut (when given) with the renames that completed, even when it throws.
        [[nodiscard]] std::vector<std::filesystem::path> RenameItems(
            HWND ownerWindow,
            std::span<RenameRequest const> requests,
            std::vector<TransferredItemPair>* renamedOut = nullptr) const;
        void DeleteItem(HWND ownerWindow, std::filesystem::path const& path, bool permanent = false) const;
        void DeleteItems(
            HWND ownerWindow,
            std::span<std::filesystem::path const> paths,
            bool permanent = false,
            std::vector<DeletedItemRecord>* deletedOut = nullptr) const;
        void PasteItems(
            HWND ownerWindow,
            IUnknown* items,
            std::filesystem::path const& destinationDirectory,
            bool move,
            std::vector<TransferredItemPair>* transferredOut = nullptr) const;
        void CopyItemsFromPaths(
            HWND ownerWindow,
            std::span<std::filesystem::path const> sourcePaths,
            std::filesystem::path const& destinationDirectory,
            bool move,
            std::vector<TransferredItemPair>* transferredOut = nullptr) const;
        [[nodiscard]] std::filesystem::path CopyItemFromPath(
            HWND ownerWindow,
            std::filesystem::path const& sourcePath,
            std::filesystem::path const& destinationDirectory,
            std::wstring const& newName) const;
        void MoveItemFromPath(
            HWND ownerWindow,
            std::filesystem::path const& sourcePath,
            std::filesystem::path const& destinationDirectory,
            std::wstring const& newName = {}) const;
        [[nodiscard]] uint32_t RestoreRecycleBinItems(
            HWND ownerWindow,
            std::span<std::wstring const> parsingNames,
            std::vector<std::wstring>* restoredParsingNamesOut = nullptr) const;
        // Returns std::nullopt if SHQueryRecycleBinW fails so callers can distinguish
        // a query error from a confirmed empty Recycle Bin.
        [[nodiscard]] std::optional<uint64_t> QueryRecycleBinItemCount() const noexcept;
        // Returns true if the Recycle Bin was emptied, false if it was already empty.
        [[nodiscard]] bool EmptyRecycleBin(HWND ownerWindow) const;
        [[nodiscard]] std::filesystem::path CompressItemsToZip(
            HWND ownerWindow,
            std::filesystem::path const& parentDirectory,
            std::span<std::filesystem::path const> sourcePaths) const;

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
        void PerformFileOperation(
            IFileOperation* fileOperation,
            std::wstring_view action,
            std::wstring const& path,
            std::vector<TransferredItemPair>* transferredOut = nullptr,
            std::vector<DeletedItemRecord>* deletedOut = nullptr) const;
    };
}
