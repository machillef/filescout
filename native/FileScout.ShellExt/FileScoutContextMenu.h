#pragma once

#include "pch.h"

// CLSID for the FileScout shell context menu extension.
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
static constexpr CLSID CLSID_FileScoutContextMenu = {
    0xA1B2C3D4, 0xE5F6, 0x7890,
    { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90 }
};

namespace FileScoutShellExt
{
    inline constexpr wchar_t FileScoutExeName[] = L"FileScoutNative.exe";
    inline constexpr std::array<std::wstring_view, 2> ContextMenuHandlerRegistryKeyValues{
        L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\FileScout",
        L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers\\FileScout",
    };

    inline std::wstring BuildFileScoutPathFromShellExtensionPath(std::wstring const& modulePath)
    {
        auto path = std::filesystem::path{ modulePath };
        auto shellExtDir = path.parent_path();
        auto configurationDir = shellExtDir.parent_path();
        auto platformDir = configurationDir.parent_path();
        auto binDir = platformDir.parent_path();
        auto nativeDir = binDir.parent_path();

        if (shellExtDir.empty() || configurationDir.empty() || platformDir.empty() || binDir.empty() || nativeDir.empty())
        {
            return FileScoutExeName;
        }

        return (nativeDir / platformDir.filename() / configurationDir.filename() / L"FileScout.Native" / FileScoutExeName).wstring();
    }

    inline auto const& ContextMenuHandlerRegistryKeys()
    {
        return ContextMenuHandlerRegistryKeyValues;
    }
}

class DECLSPEC_UUID("A1B2C3D4-E5F6-7890-ABCD-EF1234567890")
    FileScoutContextMenu
    : public IShellExtInit
    , public IContextMenu
{
public:
    FileScoutContextMenu();
    ~FileScoutContextMenu();

    static bool CanUnloadNow();
    static void UpdateServerLock(bool lock);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IShellExtInit
    IFACEMETHODIMP Initialize(
        PCIDLIST_ABSOLUTE folder,
        IDataObject* dataObj,
        HKEY progID) override;

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(
        HMENU hMenu,
        UINT indexMenu,
        UINT idCmdFirst,
        UINT idCmdLast,
        UINT uFlags) override;
    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) override;
    IFACEMETHODIMP GetCommandString(
        UINT_PTR idCmd,
        UINT uType,
        UINT* pReserved,
        LPSTR pszName,
        UINT cchMax) override;

private:
    LONG m_refCount{ 1 };
    static LONG s_objectCount;
    static LONG s_lockCount;
    std::vector<std::wstring> m_selectedPaths;
};
