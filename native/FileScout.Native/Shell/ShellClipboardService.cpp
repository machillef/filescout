#include "pch.h"
#include "ShellClipboardService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        CLIPFORMAT PreferredDropEffectFormat()
        {
            static const CLIPFORMAT format = static_cast<CLIPFORMAT>(RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT));
            return format;
        }

        struct PidlCollection
        {
            ~PidlCollection()
            {
                for (auto const pidl : items)
                {
                    if (pidl != nullptr)
                    {
                        ILFree(pidl);
                    }
                }
            }

            std::vector<PIDLIST_ABSOLUTE> items;
        };

        void ThrowClipboardFailure(std::wstring const& message)
        {
            throw std::runtime_error(winrt::to_string(winrt::hstring{ message }));
        }
    }

    void ShellClipboardService::CopyShellItems(std::span<FileScoutNative::Domain::ShellItemRef const> const items)
    {
        SetClipboardItems(items, DROPEFFECT_COPY);
    }

    void ShellClipboardService::CutShellItems(std::span<FileScoutNative::Domain::ShellItemRef const> const items)
    {
        SetClipboardItems(items, DROPEFFECT_MOVE);
    }

    bool ShellClipboardService::CanPasteInto(std::filesystem::path const& directory) const
    {
        if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
        {
            return false;
        }

        try
        {
            auto clipboardDataObject = GetClipboardDataObject();
            return CountClipboardItems(clipboardDataObject.get()) > 0;
        }
        catch (...)
        {
            return false;
        }
    }

    ClipboardPasteResult ShellClipboardService::PasteInto(
        HWND const ownerWindow,
        std::filesystem::path const& directory) const
    {
        auto clipboardDataObject = GetClipboardDataObject();
        auto const itemCount = CountClipboardItems(clipboardDataObject.get());
        if (itemCount == 0)
        {
            ThrowClipboardFailure(L"Clipboard does not contain shell items");
        }

        auto const preferredDropEffect = GetPreferredDropEffect(clipboardDataObject.get());
        auto const isMove = (preferredDropEffect & DROPEFFECT_MOVE) == DROPEFFECT_MOVE;

        m_fileOperationService.PasteItems(ownerWindow, clipboardDataObject.get(), directory, isMove);

        if (isMove)
        {
            ClearClipboard();
        }

        return ClipboardPasteResult{
            .was_move = isMove,
            .item_count = itemCount,
        };
    }

    void ShellClipboardService::SetClipboardItems(
        std::span<FileScoutNative::Domain::ShellItemRef const> const items,
        DWORD const preferredDropEffect) const
    {
        if (items.empty())
        {
            ThrowClipboardFailure(L"Clipboard update requires at least one shell item");
        }

        auto itemArray = CreateShellItemArray(items);
        winrt::com_ptr<IDataObject> dataObject;
        winrt::check_hresult(
            itemArray->BindToHandler(nullptr, BHID_DataObject, __uuidof(IDataObject), dataObject.put_void()));

        SetPreferredDropEffect(dataObject.get(), preferredDropEffect);
        winrt::check_hresult(OleSetClipboard(dataObject.get()));
        winrt::check_hresult(OleFlushClipboard());
    }

    winrt::com_ptr<IShellItemArray> ShellClipboardService::CreateShellItemArray(
        std::span<FileScoutNative::Domain::ShellItemRef const> const items) const
    {
        PidlCollection pidls;
        pidls.items.reserve(items.size());

        std::vector<PCIDLIST_ABSOLUTE> rawPidls;
        rawPidls.reserve(items.size());

        for (auto const& item : items)
        {
            auto const widePath = item.path.wstring();
            auto const pidl = ILCreateFromPathW(widePath.c_str());
            if (!pidl)
            {
                ThrowClipboardFailure(L"Failed to create shell identifier list for " + widePath);
            }

            rawPidls.push_back(pidl);
            pidls.items.push_back(pidl);
        }

        winrt::com_ptr<IShellItemArray> itemArray;
        winrt::check_hresult(
            SHCreateShellItemArrayFromIDLists(
                static_cast<UINT>(rawPidls.size()),
                rawPidls.data(),
                itemArray.put()));
        return itemArray;
    }

    winrt::com_ptr<IDataObject> ShellClipboardService::GetClipboardDataObject() const
    {
        winrt::com_ptr<IDataObject> dataObject;
        winrt::check_hresult(OleGetClipboard(dataObject.put()));
        return dataObject;
    }

    uint32_t ShellClipboardService::CountClipboardItems(IDataObject* const dataObject)
    {
        winrt::com_ptr<IShellItemArray> itemArray;
        winrt::check_hresult(
            SHCreateShellItemArrayFromDataObject(dataObject, __uuidof(IShellItemArray), itemArray.put_void()));

        DWORD count = 0;
        winrt::check_hresult(itemArray->GetCount(&count));
        return count;
    }

    DWORD ShellClipboardService::GetPreferredDropEffect(IDataObject* const dataObject)
    {
        FORMATETC format{
            .cfFormat = PreferredDropEffectFormat(),
            .ptd = nullptr,
            .dwAspect = DVASPECT_CONTENT,
            .lindex = -1,
            .tymed = TYMED_HGLOBAL,
        };

        STGMEDIUM storage{};
        if (FAILED(dataObject->GetData(&format, &storage)))
        {
            return DROPEFFECT_COPY;
        }

        DWORD effect = DROPEFFECT_COPY;
        if (auto const data = static_cast<DWORD const*>(GlobalLock(storage.hGlobal)); data != nullptr)
        {
            effect = *data;
            GlobalUnlock(storage.hGlobal);
        }

        ReleaseStgMedium(&storage);
        return effect;
    }

    void ShellClipboardService::SetPreferredDropEffect(IDataObject* const dataObject, DWORD const effect)
    {
        FORMATETC format{
            .cfFormat = PreferredDropEffectFormat(),
            .ptd = nullptr,
            .dwAspect = DVASPECT_CONTENT,
            .lindex = -1,
            .tymed = TYMED_HGLOBAL,
        };

        STGMEDIUM storage{};
        storage.tymed = TYMED_HGLOBAL;
        storage.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (storage.hGlobal == nullptr)
        {
            ThrowClipboardFailure(L"Failed to allocate preferred drop effect clipboard payload");
        }

        auto* value = static_cast<DWORD*>(GlobalLock(storage.hGlobal));
        if (value == nullptr)
        {
            GlobalFree(storage.hGlobal);
            ThrowClipboardFailure(L"Failed to lock preferred drop effect clipboard payload");
        }

        *value = effect;
        GlobalUnlock(storage.hGlobal);

        HRESULT const setDataResult = dataObject->SetData(&format, &storage, TRUE);
        if (FAILED(setDataResult))
        {
            ReleaseStgMedium(&storage);
            winrt::check_hresult(setDataResult);
        }
    }

    void ShellClipboardService::ClearClipboard()
    {
        winrt::check_hresult(OleSetClipboard(nullptr));
    }
}
