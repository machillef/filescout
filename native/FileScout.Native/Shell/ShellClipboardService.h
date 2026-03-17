#pragma once

#include "..\Domain\ClipboardService.h"
#include "ShellFileOperationService.h"

namespace FileScoutNative::Shell
{
    struct ClipboardPasteResult
    {
        bool was_move{ false };
        uint32_t item_count{ 0 };
    };

    class ShellClipboardService final : public FileScoutNative::Domain::ClipboardService
    {
    public:
        void CopyShellItems(std::span<FileScoutNative::Domain::ShellItemRef const> items) override;
        void CutShellItems(std::span<FileScoutNative::Domain::ShellItemRef const> items) override;
        [[nodiscard]] bool CanPasteInto(std::filesystem::path const& directory) const override;
        [[nodiscard]] ClipboardPasteResult PasteInto(
            HWND ownerWindow,
            std::filesystem::path const& directory) const;

    private:
        void SetClipboardItems(
            std::span<FileScoutNative::Domain::ShellItemRef const> items,
            DWORD preferredDropEffect) const;
        [[nodiscard]] winrt::com_ptr<IShellItemArray> CreateShellItemArray(
            std::span<FileScoutNative::Domain::ShellItemRef const> items) const;
        [[nodiscard]] winrt::com_ptr<IDataObject> GetClipboardDataObject() const;
        [[nodiscard]] static uint32_t CountClipboardItems(IDataObject* dataObject);
        [[nodiscard]] static DWORD GetPreferredDropEffect(IDataObject* dataObject);
        static void SetPreferredDropEffect(IDataObject* dataObject, DWORD effect);
        static void ClearClipboard();

        ShellFileOperationService m_fileOperationService;
    };
}
