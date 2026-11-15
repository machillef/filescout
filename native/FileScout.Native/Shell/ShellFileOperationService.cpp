#include "pch.h"
#include "ShellFileOperationService.h"
#include "PropertiesLaunchSpec.h"

#include <sherrors.h>

namespace FileScoutNative::Shell
{
    FileOperationException::FileOperationException(FileOperationError error)
        : std::runtime_error(BuildMessage(error)),
          m_error(std::move(error))
    {
    }

    FileOperationError const& FileOperationException::Error() const noexcept
    {
        return m_error;
    }

    bool FileOperationException::IsCanceled() const noexcept
    {
        return m_error.canceled;
    }

    std::string FileOperationException::BuildMessage(FileOperationError const& error)
    {
        std::wstring message = error.action.empty() ? L"File operation failed" : error.action;
        if (!error.path.empty())
        {
            message += L" failed for ";
            message += error.path;
        }

        if (!error.detail.empty())
        {
            message += L": ";
            message += error.detail;
        }

        return winrt::to_string(winrt::hstring{ message });
    }

    namespace
    {
        constexpr std::wstring_view canceledFileOperationDetail = L"Windows canceled the file operation.";

        [[nodiscard]] bool IsCanceledHResult(HRESULT const result) noexcept
        {
            return result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || result == COPYENGINE_E_USER_CANCELLED;
        }

        // Closed beta policy: keep the shell backend non-interactive and surface all outcomes
        // in FileScout's own dark-themed UI instead of letting Explorer show separate dialogs.
        constexpr DWORD baseMutationFlags =
            FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | FOFX_ADDUNDORECORD;
        // Copy keeps Windows' collision-safe duplication behavior rather than failing outright.
        constexpr DWORD copyMutationFlags =
            baseMutationFlags | FOF_RENAMEONCOLLISION | FOFX_PRESERVEFILEEXTENSIONS;
        // Delete skips the extra Explorer confirmation because FileScout already confirms first.
        constexpr DWORD deleteMutationFlags =
            baseMutationFlags | FOF_NOCONFIRMATION | FOFX_RECYCLEONDELETE;

        [[noreturn]] void ThrowFileOperationFailure(FileOperationError error)
        {
            throw FileOperationException(std::move(error));
        }

        [[nodiscard]] std::wstring BuildHResultDetail(winrt::hresult_error const& error)
        {
            auto const detailMessage = error.message();
            auto detail = std::wstring{ detailMessage.c_str() };
            if (detail.empty())
            {
                detail = L"Windows returned an unexpected error.";
            }

            return detail;
        }
    }

    namespace
    {
        // Records the first item that failed. With FOF_NOERRORUI the shell does not fail
        // PerformOperations for a refused item; it only sets the aborted flag, so without this the
        // failure looks like a cancel and loses which item it was.
        struct ItemFailureSink : winrt::implements<ItemFailureSink, IFileOperationProgressSink>
        {
            HRESULT first_failure = S_OK;
            std::wstring failed_path;

            void Record(HRESULT const result, IShellItem* const item, IShellItem* const fallback = nullptr)
            {
                if (SUCCEEDED(result) || FAILED(first_failure))
                {
                    return;
                }

                first_failure = result;
                for (auto* const candidate : { item, fallback })
                {
                    PWSTR text{};
                    if (candidate != nullptr && SUCCEEDED(candidate->GetDisplayName(SIGDN_FILESYSPATH, &text)))
                    {
                        failed_path = text;
                        CoTaskMemFree(text);
                        return;
                    }
                }
            }

            HRESULT __stdcall StartOperations() noexcept override { return S_OK; }
            HRESULT __stdcall FinishOperations(HRESULT) noexcept override { return S_OK; }
            HRESULT __stdcall PreRenameItem(DWORD, IShellItem*, LPCWSTR) noexcept override { return S_OK; }
            HRESULT __stdcall PostRenameItem(DWORD, IShellItem* item, LPCWSTR, HRESULT result, IShellItem*) noexcept override
            {
                Record(result, item);
                return S_OK;
            }
            HRESULT __stdcall PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) noexcept override { return S_OK; }
            HRESULT __stdcall PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem*) noexcept override
            {
                Record(result, item);
                return S_OK;
            }
            HRESULT __stdcall PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) noexcept override { return S_OK; }
            HRESULT __stdcall PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem*) noexcept override
            {
                Record(result, item);
                return S_OK;
            }
            HRESULT __stdcall PreDeleteItem(DWORD, IShellItem*) noexcept override { return S_OK; }
            HRESULT __stdcall PostDeleteItem(DWORD, IShellItem* item, HRESULT result, IShellItem*) noexcept override
            {
                Record(result, item);
                return S_OK;
            }
            HRESULT __stdcall PreNewItem(DWORD, IShellItem*, LPCWSTR) noexcept override { return S_OK; }
            HRESULT __stdcall PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT result, IShellItem*) noexcept override
            {
                // The item argument is the destination folder, not the item that failed; record no
                // path so the caller's requested target path is reported.
                Record(result, nullptr, nullptr);
                return S_OK;
            }
            HRESULT __stdcall UpdateProgress(UINT, UINT) noexcept override { return S_OK; }
            HRESULT __stdcall ResetTimer() noexcept override { return S_OK; }
            HRESULT __stdcall PauseTimer() noexcept override { return S_OK; }
            HRESULT __stdcall ResumeTimer() noexcept override { return S_OK; }
        };
    }

    std::optional<FileOperationError> ClassifyFileOperationOutcome(
        HRESULT const performResult,
        bool const anyOperationsAborted,
        std::wstring_view const action,
        std::wstring_view const path)
    {
        // With FOF_NOERRORUI a failing item also sets the aborted flag, so a failure HRESULT wins:
        // a refused rename must not be reported as "canceled".
        if (FAILED(performResult) && !IsCanceledHResult(performResult))
        {
            return FileOperationError{
                .action = std::wstring{ action },
                .path = std::wstring{ path },
                .detail = BuildHResultDetail(winrt::hresult_error{ performResult }),
            };
        }

        if (anyOperationsAborted || IsCanceledHResult(performResult))
        {
            return FileOperationError{
                .action = std::wstring{ action },
                .path = std::wstring{ path },
                .detail = std::wstring{ canceledFileOperationDetail },
                .canceled = true,
            };
        }

        return std::nullopt;
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
        auto const spec = detail::BuildPropertiesLaunchSpec(path);
        if (!SHObjectProperties(
            ownerWindow,
            spec.object_type,
            spec.object_name.c_str(),
            spec.property_page.empty() ? nullptr : spec.property_page.c_str()))
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Show properties",
                .path = path.wstring(),
                .detail = L"Windows could not open the Properties dialog.",
            });
        }
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
            std::wstring detail = L"ShellExecuteW returned an error";
            if (verb != nullptr)
            {
                detail += L" for verb ";
                detail += verb;
            }

            ThrowFileOperationFailure(FileOperationError{
                .action = verb == nullptr ? L"Open" : L"Open with shell verb",
                .path = path.wstring(),
                .detail = std::move(detail),
            });
        }
    }

    std::filesystem::path ShellFileOperationService::CreateFolder(
        HWND const ownerWindow,
        std::filesystem::path const& parentDirectory,
        std::wstring const& name) const
    {
        auto const targetPath = parentDirectory / name;
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
            auto destinationFolder = CreateShellItem(parentDirectory);

            winrt::check_hresult(fileOperation->NewItem(
                destinationFolder.get(),
                FILE_ATTRIBUTE_DIRECTORY,
                name.c_str(),
                nullptr,
                nullptr));
            PerformFileOperation(fileOperation.get(), L"Create folder", targetPath.wstring());
            return targetPath;
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Create folder",
                .path = targetPath.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    std::filesystem::path ShellFileOperationService::RenameItem(
        HWND const ownerWindow,
        std::filesystem::path const& path,
        std::wstring const& newName) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
            auto item = CreateShellItem(path);

            winrt::check_hresult(fileOperation->RenameItem(item.get(), newName.c_str(), nullptr));
            PerformFileOperation(fileOperation.get(), L"Rename", path.wstring());
            return path.parent_path() / newName;
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Rename",
                .path = path.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    std::vector<std::filesystem::path> ShellFileOperationService::RenameItems(
        HWND const ownerWindow,
        std::span<RenameRequest const> const requests) const
    {
        std::vector<std::filesystem::path> renamedPaths;
        if (requests.empty())
        {
            return renamedPaths;
        }

        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
        renamedPaths.reserve(requests.size());

        for (auto const& request : requests)
        {
            if (request.new_name.empty())
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Batch rename",
                    .path = request.path.wstring(),
                    .detail = L"Batch rename requests need a target name.",
                });
            }

            try
            {
                auto item = CreateShellItem(request.path);
                winrt::check_hresult(fileOperation->RenameItem(item.get(), request.new_name.c_str(), nullptr));
                renamedPaths.push_back(request.path.parent_path() / request.new_name);
            }
            catch (FileOperationException const&)
            {
                throw;
            }
            catch (winrt::hresult_error const& error)
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Batch rename",
                    .path = request.path.wstring(),
                    .detail = BuildHResultDetail(error),
                });
            }
        }

        try
        {
            PerformFileOperation(fileOperation.get(), L"Batch rename", requests.front().path.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            // The shell runs the whole batch in one PerformOperations call and does not say which
            // request failed, so name the first one.
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Batch rename",
                .path = requests.front().path.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
        return renamedPaths;
    }

    void ShellFileOperationService::DeleteItem(HWND const ownerWindow, std::filesystem::path const& path) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, deleteMutationFlags);
            auto item = CreateShellItem(path);

            winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
            PerformFileOperation(fileOperation.get(), L"Delete", path.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Delete",
                .path = path.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::DeleteItems(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const paths) const
    {
        if (paths.empty())
        {
            return;
        }

        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, deleteMutationFlags);
            for (auto const& path : paths)
            {
                auto item = CreateShellItem(path);
                winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
            }

            PerformFileOperation(fileOperation.get(), L"Delete", paths.front().wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Delete",
                .path = paths.front().wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::CopyItemsFromPaths(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const sourcePaths,
        std::filesystem::path const& destinationDirectory,
        bool const move) const
    {
        if (sourcePaths.empty())
        {
            return;
        }

        try
        {
            auto fileOperation = CreateFileOperation(
                ownerWindow,
                move ? baseMutationFlags : copyMutationFlags);
            auto destinationFolder = CreateShellItem(destinationDirectory);

            for (auto const& sourcePath : sourcePaths)
            {
                auto sourceItem = CreateShellItem(sourcePath);
                if (move)
                {
                    winrt::check_hresult(
                        fileOperation->MoveItem(sourceItem.get(), destinationFolder.get(), nullptr, nullptr));
                }
                else
                {
                    winrt::check_hresult(
                        fileOperation->CopyItem(sourceItem.get(), destinationFolder.get(), nullptr, nullptr));
                }
            }

            PerformFileOperation(fileOperation.get(), move ? L"Move" : L"Copy", sourcePaths.front().wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = move ? L"Move" : L"Copy",
                .path = sourcePaths.front().wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::CopyItemFromPath(
        HWND const ownerWindow,
        std::filesystem::path const& sourcePath,
        std::filesystem::path const& destinationDirectory,
        std::wstring const& newName) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, copyMutationFlags);
            auto destinationFolder = CreateShellItem(destinationDirectory);
            auto sourceItem = CreateShellItem(sourcePath);

            auto const* targetName = newName.empty() ? nullptr : newName.c_str();
            winrt::check_hresult(fileOperation->CopyItem(
                sourceItem.get(),
                destinationFolder.get(),
                targetName,
                nullptr));

            PerformFileOperation(fileOperation.get(), L"Copy", sourcePath.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Copy",
                .path = sourcePath.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::PasteItems(
        HWND const ownerWindow,
        IDataObject* const clipboardDataObject,
        std::filesystem::path const& destinationDirectory,
        bool const move) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(
                ownerWindow,
                move ? baseMutationFlags : copyMutationFlags);
            auto destinationFolder = CreateShellItem(destinationDirectory);

            if (move)
            {
                winrt::check_hresult(fileOperation->MoveItems(clipboardDataObject, destinationFolder.get()));
            }
            else
            {
                winrt::check_hresult(fileOperation->CopyItems(clipboardDataObject, destinationFolder.get()));
            }

            PerformFileOperation(fileOperation.get(), move ? L"Paste move" : L"Paste copy", destinationDirectory.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = move ? L"Paste move" : L"Paste copy",
                .path = destinationDirectory.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    winrt::com_ptr<IShellItem> ShellFileOperationService::CreateShellItem(
        std::filesystem::path const& path) const
    {
        auto widePath = path.wstring();
        winrt::com_ptr<IShellItem> shellItem;
        winrt::check_hresult(
            SHCreateItemFromParsingName(widePath.c_str(), nullptr, __uuidof(IShellItem), shellItem.put_void()));
        return shellItem;
    }

    winrt::com_ptr<IFileOperation> ShellFileOperationService::CreateFileOperation(
        HWND const ownerWindow,
        DWORD const operationFlags) const
    {
        winrt::com_ptr<IFileOperation> fileOperation;
        winrt::check_hresult(
            CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileOperation), fileOperation.put_void()));

        winrt::check_hresult(fileOperation->SetOwnerWindow(ownerWindow));
        winrt::check_hresult(fileOperation->SetOperationFlags(operationFlags));
        return fileOperation;
    }

    void ShellFileOperationService::PerformFileOperation(
        IFileOperation* const fileOperation,
        std::wstring_view const action,
        std::wstring const& path) const
    {
        auto const sink = winrt::make_self<ItemFailureSink>();
        DWORD cookie = 0;
        // Without the sink, refused items would be reported as success or "canceled"; do not run.
        winrt::check_hresult(fileOperation->Advise(sink.get(), &cookie));
        bool const advised = true;

        HRESULT performResult = fileOperation->PerformOperations();

        if (advised)
        {
            fileOperation->Unadvise(cookie);
        }

        BOOL operationsAborted = FALSE;
        HRESULT const abortQueryResult = fileOperation->GetAnyOperationsAborted(&operationsAborted);
        if (FAILED(abortQueryResult))
        {
            operationsAborted = FALSE;
        }

        // A refused item is a failure of that item, even though the shell only aborted the batch.
        std::wstring failurePath = path;
        if (FAILED(sink->first_failure) && (SUCCEEDED(performResult) || performResult == HRESULT_FROM_WIN32(ERROR_CANCELLED) ||
                                            performResult == COPYENGINE_E_USER_CANCELLED))
        {
            performResult = sink->first_failure;
            if (!sink->failed_path.empty())
            {
                failurePath = sink->failed_path;
            }
        }

        if (auto error = ClassifyFileOperationOutcome(performResult, operationsAborted != FALSE, action, failurePath))
        {
            ThrowFileOperationFailure(std::move(*error));
        }

        // A success code from PerformOperations does not prove everything completed; if the abort
        // status itself cannot be read, the outcome is unknown, so report it instead of success.
        winrt::check_hresult(abortQueryResult);
    }
}
