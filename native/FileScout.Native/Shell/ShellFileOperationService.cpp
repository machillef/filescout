#include "pch.h"
#include "ShellFileOperationService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        constexpr DWORD baseMutationFlags =
            FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | FOFX_ADDUNDORECORD;
        constexpr DWORD deleteMutationFlags =
            baseMutationFlags | FOF_NOCONFIRMATION | FOFX_RECYCLEONDELETE;

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

        void ThrowFileOperationFailure(std::wstring const& message)
        {
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

    std::filesystem::path ShellFileOperationService::CreateFolder(
        HWND const ownerWindow,
        std::filesystem::path const& parentDirectory,
        std::wstring const& name) const
    {
        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
        auto destinationFolder = CreateShellItem(parentDirectory);

        winrt::check_hresult(fileOperation->NewItem(
            destinationFolder.get(),
            FILE_ATTRIBUTE_DIRECTORY,
            name.c_str(),
            nullptr,
            nullptr));
        PerformFileOperation(fileOperation.get());
        return parentDirectory / name;
    }

    std::filesystem::path ShellFileOperationService::RenameItem(
        HWND const ownerWindow,
        std::filesystem::path const& path,
        std::wstring const& newName) const
    {
        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
        auto item = CreateShellItem(path);

        winrt::check_hresult(fileOperation->RenameItem(item.get(), newName.c_str(), nullptr));
        PerformFileOperation(fileOperation.get());
        return path.parent_path() / newName;
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
                ThrowFileOperationFailure(L"Batch rename requests need a target name");
            }

            auto item = CreateShellItem(request.path);
            winrt::check_hresult(fileOperation->RenameItem(item.get(), request.new_name.c_str(), nullptr));
            renamedPaths.push_back(request.path.parent_path() / request.new_name);
        }

        PerformFileOperation(fileOperation.get());
        return renamedPaths;
    }

    void ShellFileOperationService::DeleteItem(HWND const ownerWindow, std::filesystem::path const& path) const
    {
        auto fileOperation = CreateFileOperation(ownerWindow, deleteMutationFlags);
        auto item = CreateShellItem(path);

        winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
        PerformFileOperation(fileOperation.get());
    }

    void ShellFileOperationService::DeleteItems(
        HWND const ownerWindow,
        std::span<std::filesystem::path const> const paths) const
    {
        if (paths.empty())
        {
            return;
        }

        auto fileOperation = CreateFileOperation(ownerWindow, deleteMutationFlags);
        for (auto const& path : paths)
        {
            auto item = CreateShellItem(path);
            winrt::check_hresult(fileOperation->DeleteItem(item.get(), nullptr));
        }

        PerformFileOperation(fileOperation.get());
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

        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
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

        PerformFileOperation(fileOperation.get());
    }

    void ShellFileOperationService::CopyItemFromPath(
        HWND const ownerWindow,
        std::filesystem::path const& sourcePath,
        std::filesystem::path const& destinationDirectory,
        std::wstring const& newName) const
    {
        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
        auto destinationFolder = CreateShellItem(destinationDirectory);
        auto sourceItem = CreateShellItem(sourcePath);

        auto const* targetName = newName.empty() ? nullptr : newName.c_str();
        winrt::check_hresult(fileOperation->CopyItem(
            sourceItem.get(),
            destinationFolder.get(),
            targetName,
            nullptr));

        PerformFileOperation(fileOperation.get());
    }

    void ShellFileOperationService::PasteItems(
        HWND const ownerWindow,
        IDataObject* const clipboardDataObject,
        std::filesystem::path const& destinationDirectory,
        bool const move) const
    {
        auto fileOperation = CreateFileOperation(ownerWindow, baseMutationFlags);
        auto destinationFolder = CreateShellItem(destinationDirectory);

        if (move)
        {
            winrt::check_hresult(fileOperation->MoveItems(clipboardDataObject, destinationFolder.get()));
        }
        else
        {
            winrt::check_hresult(fileOperation->CopyItems(clipboardDataObject, destinationFolder.get()));
        }

        PerformFileOperation(fileOperation.get());
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

    void ShellFileOperationService::PerformFileOperation(IFileOperation* const fileOperation) const
    {
        winrt::check_hresult(fileOperation->PerformOperations());

        BOOL operationsAborted = FALSE;
        winrt::check_hresult(fileOperation->GetAnyOperationsAborted(&operationsAborted));
        if (operationsAborted)
        {
            ThrowFileOperationFailure(L"Windows file operation was aborted");
        }
    }
}
