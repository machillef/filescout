#pragma once

#include "..\Domain\ClipboardService.h"
#include "ShellFileOperationService.h"

namespace FileScoutNative::Shell
{
    struct ClipboardPasteResult
    {
        bool succeeded{ false };
        bool was_move{ false };
        uint32_t item_count{ 0 };
        std::filesystem::path destination;
        std::wstring attempted_action{ L"Paste copy" };
        std::wstring failure_detail;
    };

    struct ClipboardSnapshot
    {
        std::vector<std::shared_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>> items;
        bool is_move{ false };
        DWORD sequence_number{ 0 };
        std::wstring failure_detail;
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
        [[nodiscard]] ClipboardSnapshot CaptureClipboard() const;
        [[nodiscard]] ClipboardPasteResult PasteCaptured(
            HWND ownerWindow,
            std::filesystem::path const& directory,
            ClipboardSnapshot const& snapshot) const;
        static bool ClearClipboardIfUnchanged(DWORD sequenceNumber);
        // Requires a valid application-owned window handle so EmptyClipboard + SetClipboardData succeed.
        static bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text);

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

        ShellFileOperationService m_fileOperationService;
    };
}
