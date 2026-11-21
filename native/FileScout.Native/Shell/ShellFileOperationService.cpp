#include "pch.h"
#include "ShellFileOperationService.h"
#include "PropertiesLaunchSpec.h"
#include "Domain/ArchiveCompression.h"

#include <sherrors.h>
#include <shlobj.h>

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
        // Permanent delete (Shift+Delete) bypasses Recycle Bin and shell undo records entirely.
        constexpr DWORD permanentDeleteMutationFlags =
            FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION;

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
            bool record_transfers = false;
            bool record_deletions = false;
            std::vector<TransferredItemPair> transferred_items;
            std::vector<DeletedItemRecord> deleted_items;

            static std::optional<std::filesystem::path> TryGetFileSystemPath(IShellItem* const item)
            {
                if (item == nullptr)
                {
                    return std::nullopt;
                }
                PWSTR text{};
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text != nullptr)
                {
                    std::filesystem::path path{ text };
                    ::CoTaskMemFree(text);
                    return path;
                }
                return std::nullopt;
            }

            static std::optional<std::wstring> TryGetParsingName(IShellItem* const item)
            {
                if (item == nullptr)
                {
                    return std::nullopt;
                }
                PWSTR text{};
                if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &text)) && text != nullptr)
                {
                    std::wstring parsingName{ text };
                    ::CoTaskMemFree(text);
                    return parsingName;
                }
                return std::nullopt;
            }

            void RecordTransfer(HRESULT const result, IShellItem* const source, IShellItem* const newlyCreated)
            {
                if (!record_transfers || FAILED(result))
                {
                    return;
                }
                auto srcPath = TryGetFileSystemPath(source);
                auto dstPath = TryGetFileSystemPath(newlyCreated);
                if (srcPath.has_value() && dstPath.has_value())
                {
                    transferred_items.push_back(TransferredItemPair{
                        .source = std::move(*srcPath),
                        .destination = std::move(*dstPath),
                    });
                }
            }

            void RecordDeletion(HRESULT const result, IShellItem* const source, IShellItem* const newlyCreated)
            {
                if (!record_deletions || FAILED(result))
                {
                    return;
                }
                auto srcPath = TryGetFileSystemPath(source);
                if (!srcPath.has_value())
                {
                    return;
                }
                auto binParsingName = TryGetParsingName(newlyCreated);
                deleted_items.push_back(DeletedItemRecord{
                    .original_path = std::move(*srcPath),
                    .recycle_bin_parsing_name = binParsingName.value_or(std::wstring{}),
                });
            }

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
            HRESULT __stdcall PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* newlyCreated) noexcept override
            {
                Record(result, item);
                RecordTransfer(result, item, newlyCreated);
                return S_OK;
            }
            HRESULT __stdcall PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) noexcept override { return S_OK; }
            HRESULT __stdcall PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT result, IShellItem* newlyCreated) noexcept override
            {
                Record(result, item);
                RecordTransfer(result, item, newlyCreated);
                return S_OK;
            }
            HRESULT __stdcall PreDeleteItem(DWORD, IShellItem*) noexcept override { return S_OK; }
            HRESULT __stdcall PostDeleteItem(DWORD, IShellItem* item, HRESULT result, IShellItem* newlyCreated) noexcept override
            {
                Record(result, item);
                RecordDeletion(result, item, newlyCreated);
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

    void ShellFileOperationService::ShowPropertiesForItems(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const paths) const
    {
        if (paths.empty())
        {
            return;
        }
        if (paths.size() == 1)
        {
            ShowProperties(ownerWindow, paths.front());
            return;
        }

        struct ScopedPidlList
        {
            std::vector<PIDLIST_ABSOLUTE> owned;
            ~ScopedPidlList()
            {
                for (auto* pidl : owned)
                {
                    ::ILFree(pidl);
                }
            }
        } pidls;

        std::vector<PCIDLIST_ABSOLUTE> rawPidls;
        pidls.owned.reserve(paths.size());
        rawPidls.reserve(paths.size());

        try
        {
            for (auto const& path : paths)
            {
                auto const widePath = path.wstring();
                PIDLIST_ABSOLUTE const pidl = ::ILCreateFromPathW(widePath.c_str());
                if (pidl == nullptr)
                {
                    ThrowFileOperationFailure(FileOperationError{
                        .action = L"Show properties",
                        .path = widePath,
                        .detail = L"Windows could not resolve one of the selected items.",
                    });
                }
                pidls.owned.push_back(pidl);
                rawPidls.push_back(pidl);
            }

            winrt::com_ptr<IShellItemArray> itemArray;
            winrt::check_hresult(::SHCreateShellItemArrayFromIDLists(
                static_cast<UINT>(rawPidls.size()),
                rawPidls.data(),
                itemArray.put()));

            winrt::com_ptr<IDataObject> dataObject;
            winrt::check_hresult(itemArray->BindToHandler(
                nullptr,
                BHID_DataObject,
                __uuidof(IDataObject),
                dataObject.put_void()));

            winrt::check_hresult(::SHMultiFileProperties(dataObject.get(), 0));
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Show properties",
                .path = paths.front().wstring(),
                .detail = BuildHResultDetail(error),
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

    std::wstring FormatWindowsTerminalDirectoryArgs(std::filesystem::path const& directory)
    {
        auto raw = directory.wstring();
        std::size_t trailingBackslashes = 0;
        for (auto it = raw.rbegin(); it != raw.rend() && *it == L'\\'; ++it)
        {
            ++trailingBackslashes;
        }
        // Double trailing backslashes before the closing quote so CommandLineToArgvW
        // does not treat `\"` as an escaped quote character (e.g. `C:\` -> `-d "C:\\"`).
        raw.append(trailingBackslashes, L'\\');
        return L"-d \"" + raw + L"\"";
    }

    std::filesystem::path ShellFileOperationService::CreateEmptyFile(
        HWND const,
        std::filesystem::path const& parentDirectory,
        std::wstring const& name) const
    {
        auto const targetPath = parentDirectory / name;
        HANDLE const handle = ::CreateFileW(
            targetPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            DWORD const lastError = ::GetLastError();
            std::wstring detail = (lastError == ERROR_FILE_EXISTS || lastError == ERROR_ALREADY_EXISTS)
                ? L"A file with that name already exists."
                : BuildHResultDetail(winrt::hresult_error{ HRESULT_FROM_WIN32(lastError) });
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Create file",
                .path = targetPath.wstring(),
                .detail = std::move(detail),
            });
        }
        ::CloseHandle(handle);
        ::SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, targetPath.c_str(), nullptr);
        return targetPath;
    }

    void ShellFileOperationService::OpenInTerminal(
        HWND const ownerWindow,
        std::filesystem::path const& directory) const
    {
        auto const dirStr = directory.wstring();
        auto const wtArgs = FormatWindowsTerminalDirectoryArgs(directory);

        auto tryLaunch = [&](wchar_t const* executable, wchar_t const* params) -> bool
        {
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_FLAG_NO_UI;
            sei.hwnd = ownerWindow;
            sei.lpVerb = L"open";
            sei.lpFile = executable;
            sei.lpParameters = params;
            sei.lpDirectory = dirStr.c_str();
            sei.nShow = SW_SHOWNORMAL;
            return ::ShellExecuteExW(&sei) != FALSE;
        };

        if (tryLaunch(L"wt.exe", wtArgs.c_str()) ||
            tryLaunch(L"pwsh.exe", nullptr) ||
            tryLaunch(L"powershell.exe", nullptr) ||
            tryLaunch(L"cmd.exe", nullptr))
        {
            return;
        }

        ThrowFileOperationFailure(FileOperationError{
            .action = L"Open in Terminal",
            .path = dirStr,
            .detail = L"Could not launch Windows Terminal, PowerShell, or Command Prompt.",
        });
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

    DWORD DeleteOperationFlags(bool const permanent) noexcept
    {
        return permanent ? permanentDeleteMutationFlags : deleteMutationFlags;
    }

    void ShellFileOperationService::DeleteItem(
        HWND const ownerWindow,
        std::filesystem::path const& path,
        bool const permanent) const
    {
        std::wstring_view const action = permanent ? L"Permanently delete" : L"Delete";
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, DeleteOperationFlags(permanent));
            auto item = CreateShellItem(path);

            winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
            PerformFileOperation(fileOperation.get(), action, path.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = std::wstring{ action },
                .path = path.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    std::optional<FileScoutNative::Domain::FileIdentity> TryCaptureFileIdentity(
        std::filesystem::path const& path) noexcept
    {
        if (path.empty())
        {
            return std::nullopt;
        }
        auto const wide = path.wstring();
        HANDLE const handle = ::CreateFileW(
            wide.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return std::nullopt;
        }

        BY_HANDLE_FILE_INFORMATION info{};
        BOOL const ok = ::GetFileInformationByHandle(handle, &info);
        ::CloseHandle(handle);
        if (!ok)
        {
            return std::nullopt;
        }

        return FileScoutNative::Domain::FileIdentity{
            .volume_serial_number = static_cast<uint32_t>(info.dwVolumeSerialNumber),
            .file_index_high = static_cast<uint32_t>(info.nFileIndexHigh),
            .file_index_low = static_cast<uint32_t>(info.nFileIndexLow),
            .creation_time_high = static_cast<uint32_t>(info.ftCreationTime.dwHighDateTime),
            .creation_time_low = static_cast<uint32_t>(info.ftCreationTime.dwLowDateTime),
        };
    }

    void ShellFileOperationService::DeleteItems(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const paths,
        bool const permanent,
        std::vector<DeletedItemRecord>* const deletedOut) const
    {
        if (paths.empty())
        {
            return;
        }

        std::wstring_view const action = permanent ? L"Permanently delete" : L"Delete";
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, DeleteOperationFlags(permanent));
            for (auto const& path : paths)
            {
                auto item = CreateShellItem(path);
                winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
            }

            PerformFileOperation(
                fileOperation.get(),
                action,
                paths.front().wstring(),
                nullptr,
                deletedOut);
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = std::wstring{ action },
                .path = paths.front().wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::CopyItemsFromPaths(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const sourcePaths,
        std::filesystem::path const& destinationDirectory,
        bool const move,
        std::vector<TransferredItemPair>* const transferredOut) const
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

            PerformFileOperation(
                fileOperation.get(),
                move ? L"Move" : L"Copy",
                sourcePaths.front().wstring(),
                transferredOut);
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

    void ShellFileOperationService::MoveItemFromPath(
        HWND const ownerWindow,
        std::filesystem::path const& sourcePath,
        std::filesystem::path const& destinationDirectory,
        std::wstring const& newName) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
            auto destinationFolder = CreateShellItem(destinationDirectory);
            auto sourceItem = CreateShellItem(sourcePath);

            auto const* targetName = newName.empty() ? nullptr : newName.c_str();
            winrt::check_hresult(fileOperation->MoveItem(
                sourceItem.get(),
                destinationFolder.get(),
                targetName,
                nullptr));

            PerformFileOperation(fileOperation.get(), L"Move", sourcePath.wstring());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Move",
                .path = sourcePath.wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    void ShellFileOperationService::PasteItems(
        HWND const ownerWindow,
        IUnknown* const items,
        std::filesystem::path const& destinationDirectory,
        bool const move,
        std::vector<TransferredItemPair>* const transferredOut) const
    {
        try
        {
            auto fileOperation = CreateFileOperation(
                ownerWindow,
                move ? baseMutationFlags : copyMutationFlags);
            auto destinationFolder = CreateShellItem(destinationDirectory);

            if (move)
            {
                winrt::check_hresult(fileOperation->MoveItems(items, destinationFolder.get()));
            }
            else
            {
                winrt::check_hresult(fileOperation->CopyItems(items, destinationFolder.get()));
            }

            PerformFileOperation(
                fileOperation.get(),
                move ? L"Paste move" : L"Paste copy",
                destinationDirectory.wstring(),
                transferredOut);
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

    uint32_t ShellFileOperationService::RestoreRecycleBinItems(
        HWND const ownerWindow,
        std::span<std::wstring const> const parsingNames,
        std::vector<std::wstring>* const restoredParsingNamesOut) const
    {
        if (restoredParsingNamesOut != nullptr)
        {
            restoredParsingNamesOut->clear();
        }
        if (parsingNames.empty())
        {
            return 0;
        }

        struct ScopedPidl
        {
            PIDLIST_ABSOLUTE pidl{ nullptr };
            ~ScopedPidl() { if (pidl) { ::ILFree(pidl); } }
        };

        struct ScopedChildPidls
        {
            std::vector<PITEMID_CHILD> items;
            ~ScopedChildPidls()
            {
                for (auto* child : items)
                {
                    ::ILFree(child);
                }
            }
        };

        struct ScopedMenu
        {
            HMENU menu{ nullptr };
            ~ScopedMenu() { if (menu) { ::DestroyMenu(menu); } }
        };

        auto matchesTarget = [&](std::wstring_view const candidate)
        {
            for (auto const& target : parsingNames)
            {
                if (::CompareStringOrdinal(
                        candidate.data(), static_cast<int>(candidate.size()),
                        target.c_str(), static_cast<int>(target.size()),
                        TRUE) == CSTR_EQUAL)
                {
                    return true;
                }
            }
            return false;
        };

        try
        {
            ScopedPidl binPidl;
            winrt::check_hresult(::SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &binPidl.pidl));

            winrt::com_ptr<IShellFolder> binFolder;
            winrt::check_hresult(::SHBindToObject(
                nullptr, binPidl.pidl, nullptr, __uuidof(IShellFolder), binFolder.put_void()));

            winrt::com_ptr<IEnumIDList> enumerator;
            HRESULT const enumHr = binFolder->EnumObjects(
                ownerWindow,
                SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                enumerator.put());
            if (FAILED(enumHr))
            {
                winrt::check_hresult(enumHr);
            }

            ScopedChildPidls matchedChildren;
            std::vector<std::wstring> matchedParsingNames;
            while (enumerator)
            {
                PITEMID_CHILD child = nullptr;
                ULONG fetched = 0;
                HRESULT const nextHr = enumerator->Next(1, &child, &fetched);
                winrt::check_hresult(nextHr);
                if (nextHr == S_FALSE || fetched == 0 || child == nullptr)
                {
                    break;
                }

                ScopedPidl fullPidl{ ::ILCombine(binPidl.pidl, child) };
                bool keep = false;
                std::wstring matchedName;
                if (fullPidl.pidl != nullptr)
                {
                    winrt::com_ptr<IShellItem> shellItem;
                    if (SUCCEEDED(::SHCreateItemFromIDList(
                            fullPidl.pidl, __uuidof(IShellItem), shellItem.put_void())))
                    {
                        PWSTR parsingText = nullptr;
                        if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingText)) &&
                            parsingText != nullptr)
                        {
                            if (matchesTarget(parsingText))
                            {
                                keep = true;
                                matchedName = parsingText;
                            }
                            ::CoTaskMemFree(parsingText);
                        }
                    }
                }

                if (keep)
                {
                    matchedChildren.items.push_back(child);
                    matchedParsingNames.push_back(std::move(matchedName));
                }
                else
                {
                    ::ILFree(child);
                }
            }

            if (matchedChildren.items.empty())
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Restore",
                    .path = L"Recycle Bin",
                    .detail = L"The selected items could not be found in the Recycle Bin.",
                });
            }

            std::vector<PCUITEMID_CHILD> rawChildren;
            rawChildren.reserve(matchedChildren.items.size());
            for (auto* child : matchedChildren.items)
            {
                rawChildren.push_back(child);
            }

            winrt::com_ptr<IContextMenu> contextMenu;
            winrt::check_hresult(binFolder->GetUIObjectOf(
                ownerWindow,
                static_cast<UINT>(rawChildren.size()),
                rawChildren.data(),
                IID_IContextMenu,
                nullptr,
                contextMenu.put_void()));

            ScopedMenu menu{ ::CreatePopupMenu() };
            if (menu.menu == nullptr)
            {
                winrt::throw_last_error();
            }

            winrt::check_hresult(contextMenu->QueryContextMenu(menu.menu, 0, 1, 0x7FFF, CMF_NORMAL));

            CMINVOKECOMMANDINFOEX invoke{};
            invoke.cbSize = sizeof(invoke);
            invoke.fMask = CMIC_MASK_UNICODE;
            invoke.hwnd = ownerWindow;
            invoke.lpVerb = "undelete";
            invoke.lpVerbW = L"undelete";
            invoke.nShow = SW_SHOWNORMAL;

            winrt::check_hresult(contextMenu->InvokeCommand(
                reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke)));

            if (restoredParsingNamesOut != nullptr)
            {
                *restoredParsingNamesOut = std::move(matchedParsingNames);
            }
            return static_cast<uint32_t>(rawChildren.size());
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Restore",
                .path = L"Recycle Bin",
                .detail = BuildHResultDetail(error),
            });
        }
    }

    uint32_t ShellFileOperationService::RestoreRecycleBinItemsByOriginalPaths(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const originalPaths) const
    {
        if (originalPaths.empty())
        {
            return 0;
        }

        // Standard Windows Recycle Bin displaced property keys (SCID_ORIGINAL_LOCATION / SCID_DATE_DELETED).
        static constexpr PROPERTYKEY kPkeyRecycleDeletedFrom{
            { 0x9B174B33, 0x40FF, 0x11D2, { 0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71 } },
            2
        };
        static constexpr PROPERTYKEY kPkeyRecycleDateDeleted{
            { 0x9B174B33, 0x40FF, 0x11D2, { 0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71 } },
            3
        };

        try
        {
            winrt::com_ptr<IShellItem> recycleBinItem;
            winrt::check_hresult(::SHCreateItemInKnownFolder(
                FOLDERID_RecycleBinFolder,
                0,
                nullptr,
                __uuidof(IShellItem),
                recycleBinItem.put_void()));

            winrt::com_ptr<IEnumShellItems> enumerator;
            winrt::check_hresult(recycleBinItem->BindToHandler(
                nullptr,
                BHID_EnumItems,
                __uuidof(IEnumShellItems),
                enumerator.put_void()));

            // System.FileName ({41CF5AE0-F75A-4806-BD87-59C7D9248EB9}, 100) holds the exact
            // filename including extension even when Explorer hides known file extensions.
            static constexpr PROPERTYKEY kPkeySystemFileName{
                { 0x41CF5AE0, 0xF75A, 0x4806, { 0xBD, 0x87, 0x59, 0xC7, 0xD9, 0x24, 0x8E, 0xB9 } },
                100
            };

            std::vector<FileScoutNative::Domain::RecycleBinCandidate> candidates;
            while (true)
            {
                winrt::com_ptr<IShellItem> child;
                ULONG fetched = 0;
                HRESULT const nextHr = enumerator->Next(1, child.put(), &fetched);
                winrt::check_hresult(nextHr);
                if (nextHr == S_FALSE || fetched == 0 || !child)
                {
                    break;
                }

                std::wstring parsingName;
                PWSTR text = nullptr;
                if (SUCCEEDED(child->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &text)) && text != nullptr)
                {
                    parsingName = text;
                    ::CoTaskMemFree(text);
                }

                std::wstring exactFileName;
                std::wstring deletedFrom;
                uint64_t deletedTime = 0;
                winrt::com_ptr<IShellItem2> child2;
                if (SUCCEEDED(child->QueryInterface(__uuidof(IShellItem2), child2.put_void())))
                {
                    PWSTR fileNameText = nullptr;
                    if (SUCCEEDED(child2->GetString(kPkeySystemFileName, &fileNameText)) && fileNameText != nullptr)
                    {
                        exactFileName = fileNameText;
                        ::CoTaskMemFree(fileNameText);
                    }

                    PWSTR dirText = nullptr;
                    if (SUCCEEDED(child2->GetString(kPkeyRecycleDeletedFrom, &dirText)) && dirText != nullptr)
                    {
                        deletedFrom = dirText;
                        ::CoTaskMemFree(dirText);
                    }

                    FILETIME ft{};
                    if (SUCCEEDED(child2->GetFileTime(kPkeyRecycleDateDeleted, &ft)))
                    {
                        deletedTime = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
                            static_cast<uint64_t>(ft.dwLowDateTime);
                    }
                }

                if (exactFileName.empty() &&
                    SUCCEEDED(child->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &text)) &&
                    text != nullptr)
                {
                    exactFileName = text;
                    ::CoTaskMemFree(text);
                }

                auto origPath = FileScoutNative::Domain::BuildRecycleBinOriginalPath(deletedFrom, exactFileName);
                if (!parsingName.empty() && !origPath.empty())
                {
                    candidates.push_back(FileScoutNative::Domain::RecycleBinCandidate{
                        .parsing_name = std::move(parsingName),
                        .original_path = std::move(origPath),
                        .deleted_time_filetime = deletedTime,
                    });
                }
            }

            auto const matched = FileScoutNative::Domain::SelectNewestRecycleBinMatches(candidates, originalPaths);
            if (matched.empty())
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Undo delete",
                    .path = originalPaths.front().wstring(),
                    .detail = L"Could not find the deleted item(s) in the Recycle Bin.",
                });
            }

            return RestoreRecycleBinItems(ownerWindow, matched);
        }
        catch (FileOperationException const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Undo delete",
                .path = originalPaths.front().wstring(),
                .detail = BuildHResultDetail(error),
            });
        }
    }

    std::optional<uint64_t> ShellFileOperationService::QueryRecycleBinItemCount() const noexcept
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        if (FAILED(::SHQueryRecycleBinW(nullptr, &info)))
        {
            return std::nullopt;
        }
        return info.i64NumItems > 0 ? static_cast<uint64_t>(info.i64NumItems) : 0ULL;
    }

    bool ShellFileOperationService::EmptyRecycleBin(HWND const ownerWindow) const
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        if (SUCCEEDED(::SHQueryRecycleBinW(nullptr, &info)) && info.i64NumItems == 0)
        {
            return false;
        }

        HRESULT const hr = ::SHEmptyRecycleBinW(ownerWindow, nullptr, SHERB_NOCONFIRMATION);
        if (hr == E_UNEXPECTED ||
            hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
            hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        {
            return false;
        }

        if (FAILED(hr))
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Empty Recycle Bin",
                .path = L"Recycle Bin",
                .detail = BuildHResultDetail(winrt::hresult_error{ hr }),
            });
        }

        return true;
    }

    std::filesystem::path ShellFileOperationService::CompressItemsToZip(
        HWND const /*ownerWindow*/,
        std::filesystem::path const& parentDirectory,
        std::span<std::filesystem::path const> const sourcePaths) const
    {
        if (parentDirectory.empty() || sourcePaths.empty())
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = parentDirectory.wstring(),
                .detail = L"Select at least one file or folder to compress.",
            });
        }

        auto const normParent = FileScoutNative::Domain::NormalizeFolderForCompare(parentDirectory);
        std::vector<std::wstring> relativeNames;
        relativeNames.reserve(sourcePaths.size());
        for (auto const& src : sourcePaths)
        {
            auto const normItemParent = FileScoutNative::Domain::NormalizeFolderForCompare(src.parent_path());
            auto const fileName = src.filename().wstring();
            if (fileName.empty() || !FileScoutNative::Domain::PathsEqualIgnoringCase(normItemParent, normParent))
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Compress to ZIP",
                    .path = src.wstring(),
                    .detail = L"All selected items must be in the current folder.",
                });
            }
            relativeNames.push_back(fileName);
        }

        std::error_code dirEc;
        bool const singleItemIsFolder =
            sourcePaths.size() == 1 && std::filesystem::is_directory(sourcePaths.front(), dirEc);

        auto const baseName = FileScoutNative::Domain::BuildZipArchiveBaseName(
            parentDirectory, sourcePaths, singleItemIsFolder);
        auto const initialCandidate = FileScoutNative::Domain::SuggestZipArchivePath(
            parentDirectory,
            sourcePaths,
            [](std::filesystem::path const& candidate)
            {
                std::error_code ec;
                return std::filesystem::exists(candidate, ec);
            },
            singleItemIsFolder);
        if (!initialCandidate.has_value())
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = (parentDirectory / (baseName + L".zip")).wstring(),
                .detail = L"Could not allocate a unique ZIP archive name in this folder.",
            });
        }

        std::wstring sysDir(MAX_PATH, L'\0');
        UINT const sysLen = ::GetSystemDirectoryW(sysDir.data(), static_cast<UINT>(sysDir.size()));
        if (sysLen == 0)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = L"Could not locate Windows System32 directory.",
            });
        }
        if (sysLen >= sysDir.size())
        {
            sysDir.resize(sysLen + 1);
            if (::GetSystemDirectoryW(sysDir.data(), static_cast<UINT>(sysDir.size())) == 0)
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Compress to ZIP",
                    .path = initialCandidate->wstring(),
                    .detail = L"Could not locate Windows System32 directory.",
                });
            }
        }
        sysDir.resize(std::wcslen(sysDir.c_str()));

        std::filesystem::path const tarExePath = std::filesystem::path{ sysDir } / L"tar.exe";
        std::error_code tarEc;
        if (!std::filesystem::exists(tarExePath, tarEc))
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = L"Built-in tar.exe was not found in System32.",
            });
        }

        // Exclusively allocate a temporary `.zip` file in the output directory so tar never
        // truncates another writer's file and failures only clean up the file we own.
        std::filesystem::path tempZipPath;
        DWORD const pid = ::GetCurrentProcessId();
        uint64_t const tick = ::GetTickCount64();
        for (uint32_t attempt = 0; attempt < 256; ++attempt)
        {
            auto candidateTemp = parentDirectory /
                (L".filescout-zip-" + std::to_wstring(pid) + L"-" +
                 std::to_wstring(tick) + L"-" + std::to_wstring(attempt) + L".zip");
            HANDLE const h = ::CreateFileW(
                candidateTemp.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN,
                nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(h);
                tempZipPath = std::move(candidateTemp);
                break;
            }
            if (::GetLastError() != ERROR_FILE_EXISTS)
            {
                DWORD const err = ::GetLastError();
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Compress to ZIP",
                    .path = initialCandidate->wstring(),
                    .detail = BuildHResultDetail(winrt::hresult_error{ HRESULT_FROM_WIN32(err) }),
                });
            }
        }
        if (tempZipPath.empty())
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = L"Could not create a temporary archive file.",
            });
        }

        struct TempFileCleanup
        {
            std::filesystem::path path;
            bool committed{ false };
            ~TempFileCleanup()
            {
                if (!committed && !path.empty())
                {
                    std::error_code ec;
                    std::filesystem::remove(path, ec);
                }
            }
        };
        TempFileCleanup tempCleanup{ tempZipPath };

        auto cmdLine = FileScoutNative::Domain::BuildTarZipCommandLine(
            tarExePath, tempZipPath, relativeNames);
        if (cmdLine.size() >= 32700)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = L"Too many items selected to compress in a single command.",
            });
        }

        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        auto const workDir = parentDirectory.wstring();
        BOOL const created = ::CreateProcessW(
            tarExePath.c_str(),
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workDir.c_str(),
            &si,
            &pi);
        if (!created)
        {
            DWORD const err = ::GetLastError();
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = BuildHResultDetail(winrt::hresult_error{ HRESULT_FROM_WIN32(err) }),
            });
        }

        struct ScopedHandle
        {
            HANDLE handle{ nullptr };
            ~ScopedHandle() { if (handle) { ::CloseHandle(handle); } }
        };
        ScopedHandle procGuard{ pi.hProcess };
        ScopedHandle threadGuard{ pi.hThread };

        ::WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        ::GetExitCodeProcess(pi.hProcess, &exitCode);

        if (exitCode != 0)
        {
            ThrowFileOperationFailure(FileOperationError{
                .action = L"Compress to ZIP",
                .path = initialCandidate->wstring(),
                .detail = L"tar.exe exited with code " + std::to_wstring(exitCode) + L".",
            });
        }

        // Clear the temporary/hidden attributes before publishing.
        ::SetFileAttributesW(tempZipPath.c_str(), FILE_ATTRIBUTE_NORMAL);

        // Publish via MoveFileW (never replaces an existing destination), advancing the suffix
        // if another writer raced us for the same archive name.
        auto tryPublish = [&](std::filesystem::path const& target) -> std::optional<DWORD>
        {
            if (::MoveFileW(tempZipPath.c_str(), target.c_str()))
            {
                tempCleanup.committed = true;
                return std::nullopt;
            }
            return ::GetLastError();
        };

        for (int suffix = 1; suffix < 10000; ++suffix)
        {
            auto const target = suffix == 1
                ? (parentDirectory / (baseName + L".zip"))
                : (parentDirectory / (baseName + L" (" + std::to_wstring(suffix) + L").zip"));
            auto const err = tryPublish(target);
            if (!err.has_value())
            {
                return target;
            }
            if (*err != ERROR_ALREADY_EXISTS && *err != ERROR_FILE_EXISTS)
            {
                ThrowFileOperationFailure(FileOperationError{
                    .action = L"Compress to ZIP",
                    .path = target.wstring(),
                    .detail = BuildHResultDetail(winrt::hresult_error{ HRESULT_FROM_WIN32(*err) }),
                });
            }
        }

        ThrowFileOperationFailure(FileOperationError{
            .action = L"Compress to ZIP",
            .path = (parentDirectory / (baseName + L".zip")).wstring(),
            .detail = L"Could not publish the ZIP archive because all candidate names are already in use.",
        });
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
        std::wstring const& path,
        std::vector<TransferredItemPair>* const transferredOut,
        std::vector<DeletedItemRecord>* const deletedOut) const
    {
        auto const sink = winrt::make_self<ItemFailureSink>();
        sink->record_transfers = (transferredOut != nullptr);
        sink->record_deletions = (deletedOut != nullptr);
        DWORD cookie = 0;
        // Without the sink, refused items would be reported as success or "canceled"; do not run.
        winrt::check_hresult(fileOperation->Advise(sink.get(), &cookie));
        bool const advised = true;

        HRESULT performResult = fileOperation->PerformOperations();

        if (advised)
        {
            fileOperation->Unadvise(cookie);
        }

        // Always surface items that actually completed before throwing on any partial failure,
        // so callers can record history and refresh folders for completed items.
        if (transferredOut != nullptr)
        {
            *transferredOut = std::move(sink->transferred_items);
        }
        if (deletedOut != nullptr)
        {
            *deletedOut = std::move(sink->deleted_items);
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
