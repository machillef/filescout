#include "pch.h"
#include "FileScoutContextMenu.h"

// Get the FileScout executable path.
// Tries registry first (set by installer or DllRegisterServer),
// then falls back to the DLL's own directory.
static std::wstring GetFileScoutPath()
{
    // Try registry: HKCU\Software\FileScout\InstallPath
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\FileScout", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t path[MAX_PATH]{};
        DWORD size = sizeof(path);
        if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return path;
        }
        RegCloseKey(hKey);
    }

    // Fallback: assume FileScout.Native.exe is next to this DLL.
    wchar_t modulePath[MAX_PATH]{};
    HMODULE hModule;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetFileScoutPath), &hModule))
    {
        GetModuleFileNameW(hModule, modulePath, MAX_PATH);
        auto* lastSlash = wcsrchr(modulePath, L'\\');
        if (lastSlash)
        {
            *(lastSlash + 1) = L'\0';
        }
        return std::wstring{ modulePath } + L"FileScout.Native.exe";
    }

    return L"FileScout.Native.exe";
}

ULONG FileScoutContextMenu::s_refCount = 0;

FileScoutContextMenu::FileScoutContextMenu()
{
    InterlockedIncrement(&s_refCount);
}

IFACEMETHODIMP FileScoutContextMenu::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(FileScoutContextMenu, IShellExtInit),
        QITABENT(FileScoutContextMenu, IContextMenu),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) FileScoutContextMenu::AddRef()
{
    return InterlockedIncrement(&s_refCount);
}

IFACEMETHODIMP_(ULONG) FileScoutContextMenu::Release()
{
    auto const count = InterlockedDecrement(&s_refCount);
    if (count == 0)
    {
        delete this;
    }
    return count;
}

IFACEMETHODIMP FileScoutContextMenu::Initialize(
    PCIDLIST_ABSOLUTE folder,
    IDataObject* dataObj,
    HKEY progID)
{
    if (!dataObj)
    {
        return E_INVALIDARG;
    }

    FORMATETC fmt{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg{};
    if (FAILED(dataObj->GetData(&fmt, &stg)))
    {
        return E_INVALIDARG;
    }

    auto const hDrop = static_cast<HDROP>(stg.hGlobal);
    auto const count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    m_selectedPaths.reserve(count);

    for (UINT i = 0; i < count; ++i)
    {
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0)
        {
            m_selectedPaths.push_back(path);
        }
    }

    ReleaseStgMedium(&stg);
    return m_selectedPaths.empty() ? E_FAIL : S_OK;
}

IFACEMETHODIMP FileScoutContextMenu::QueryContextMenu(
    HMENU hMenu,
    UINT indexMenu,
    UINT idCmdFirst,
    UINT idCmdLast,
    UINT uFlags)
{
    if (uFlags & CMF_DEFAULTONLY)
    {
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
    }

    InsertMenuW(hMenu, indexMenu, MF_STRING | MF_BYPOSITION,
        idCmdFirst, L"Open in FileScout");

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 1);
}

IFACEMETHODIMP FileScoutContextMenu::InvokeCommand(CMINVOKECOMMANDINFO* pici)
{
    if (HIWORD(pici->lpVerb) != 0)
    {
        return E_FAIL;
    }

    auto const cmdIndex = LOWORD(pici->lpVerb);
    if (cmdIndex != 0)
    {
        return E_FAIL;
    }

    auto const fileScoutPath = GetFileScoutPath();

    for (auto const& path : m_selectedPaths)
    {
        ShellExecuteW(pici->hwnd, L"open", fileScoutPath.c_str(),
            (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }

    return S_OK;
}

IFACEMETHODIMP FileScoutContextMenu::GetCommandString(
    UINT_PTR idCmd,
    UINT uType,
    UINT* pReserved,
    LPSTR pszName,
    UINT cchMax)
{
    if (idCmd != 0)
    {
        return E_INVALIDARG;
    }

    if (uType == GCS_HELPTEXTA)
    {
        strncpy_s(pszName, cchMax, "Open in FileScout file manager", _TRUNCATE);
        return S_OK;
    }

    if (uType == GCS_HELPTEXTW)
    {
        wcsncpy_s(reinterpret_cast<LPWSTR>(pszName), cchMax, L"Open in FileScout file manager", _TRUNCATE);
        return S_OK;
    }

    return E_INVALIDARG;
}

// DLL exports for COM registration
extern "C"
{
    STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
    {
        if (rclsid != CLSID_FileScoutContextMenu)
        {
            return CLASS_E_CLASSNOTAVAILABLE;
        }

        class Factory : public IClassFactory
        {
        public:
            IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
            {
                if (riid == IID_IUnknown || riid == IID_IClassFactory)
                {
                    *ppv = static_cast<IClassFactory*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }
            IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }
            IFACEMETHODIMP_(ULONG) Release() override { return 1; }
            IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
            {
                if (pUnkOuter) return CLASS_E_NOAGGREGATION;
                auto* ext = new FileScoutContextMenu();
                auto const hr = ext->QueryInterface(riid, ppv);
                if (FAILED(hr))
                {
                    delete ext;
                }
                return hr;
            }
            IFACEMETHODIMP LockServer(BOOL fLock) override { return S_OK; }
        };

        *ppv = new Factory();
        return *ppv ? S_OK : E_OUTOFMEMORY;
    }

    STDAPI DllCanUnloadNow()
    {
        return FileScoutContextMenu::s_refCount == 0 ? S_OK : S_FALSE;
    }

    STDAPI DllRegisterServer()
    {
        wchar_t modulePath[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH))
        {
            return SELFREG_E_CLASS;
        }

        auto const clsidStr = L"{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}";

        HKEY hKey;
        auto const createKey = [&](std::wstring const& subKey) -> bool
        {
            return RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS;
        };

        auto const setValue = [&](std::wstring const& value) -> bool
        {
            auto const result = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return result == ERROR_SUCCESS;
        };

        // Directory background (right-click on empty space in a folder)
        if (!createKey(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\FileScout"))
            return SELFREG_E_CLASS;
        if (!setValue(clsidStr)) return SELFREG_E_CLASS;

        // Directory (right-click on a folder)
        if (!createKey(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\FileScout"))
            return SELFREG_E_CLASS;
        if (!setValue(clsidStr)) return SELFREG_E_CLASS;

        // Drive (right-click on a drive letter)
        if (!createKey(L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers\\FileScout"))
            return SELFREG_E_CLASS;
        if (!setValue(clsidStr)) return SELFREG_E_CLASS;

        // Store install path for GetFileScoutPath()
        if (createKey(L"Software\\FileScout"))
        {
            auto* lastSlash = wcsrchr(modulePath, L'\\');
            if (lastSlash)
            {
                *(lastSlash + 1) = L'\0';
            }
            RegSetValueExW(hKey, L"InstallPath", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(modulePath),
                static_cast<DWORD>((wcslen(modulePath) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        return S_OK;
    }

    STDAPI DllUnregisterServer()
    {
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\FileScout");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\FileScout");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers\\FileScout");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\FileScout");
        return S_OK;
    }
}
