#pragma once

#include "pch.h"

// CLSID for the FileScout shell context menu extension.
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
static constexpr CLSID CLSID_FileScoutContextMenu = {
    0xA1B2C3D4, 0xE5F6, 0x7890,
    { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90 }
};

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
