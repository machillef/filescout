#include "pch.h"
#include "FileScoutContextMenu.h"

// Get the FileScout executable path.
// Tries registry first (set by installer or DllRegisterServer),
// then falls back to the DLL's own directory.
namespace
{
    constexpr auto const& kClsidString = FileScoutShellExt::ClsidString;
    constexpr wchar_t kInstallPathKey[] = L"Software\\FileScout";
    constexpr wchar_t kInstallPathValue[] = L"InstallPath";

    std::wstring GetShellExtModulePath()
    {
        HMODULE module{};
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GetShellExtModulePath),
                &module))
        {
            return {};
        }

        return FileScoutShellExt::ReadGrowingString([module](wchar_t* buffer, DWORD capacity)
        {
            return GetModuleFileNameW(module, buffer, capacity);
        });
    }

    std::wstring BuildDefaultFileScoutPath()
    {
        return FileScoutShellExt::BuildFileScoutPathFromShellExtensionPath(GetShellExtModulePath());
    }
}

static std::wstring GetFileScoutPath()
{
    auto const fallbackPath = BuildDefaultFileScoutPath();

    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kInstallPathKey, kInstallPathValue, RRF_RT_REG_SZ,
            nullptr, nullptr, &size) == ERROR_SUCCESS && size > sizeof(wchar_t))
    {
        std::wstring path(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(HKEY_CURRENT_USER, kInstallPathKey, kInstallPathValue, RRF_RT_REG_SZ,
                nullptr, path.data(), &size) == ERROR_SUCCESS)
        {
            path.resize(wcsnlen(path.c_str(), path.size()));
            return FileScoutShellExt::ResolveFileScoutPath(path, fallbackPath);
        }
    }

    return fallbackPath;
}

LONG FileScoutContextMenu::s_objectCount = 0;
LONG FileScoutContextMenu::s_lockCount = 0;

FileScoutContextMenu::FileScoutContextMenu()
{
    InterlockedIncrement(&s_objectCount);
}

FileScoutContextMenu::~FileScoutContextMenu()
{
    InterlockedDecrement(&s_objectCount);
}

bool FileScoutContextMenu::CanUnloadNow()
{
    return s_objectCount == 0 && s_lockCount == 0;
}

void FileScoutContextMenu::UpdateServerLock(bool lock)
{
    if (lock)
    {
        InterlockedIncrement(&s_lockCount);
        return;
    }

    InterlockedDecrement(&s_lockCount);
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
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

IFACEMETHODIMP_(ULONG) FileScoutContextMenu::Release()
{
    auto const count = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
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
    FileScoutShellExt::PrepareForInitialize(m_selectedPaths, count);

    for (UINT i = 0; i < count; ++i)
    {
        auto const length = DragQueryFileW(hDrop, i, nullptr, 0);
        if (length == 0)
        {
            continue;
        }

        std::wstring path(length + 1, L'\0');
        if (DragQueryFileW(hDrop, i, path.data(), length + 1) > 0)
        {
            path.resize(length);
            m_selectedPaths.push_back(std::move(path));
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
        auto const argument = FileScoutShellExt::QuoteCommandLineArgument(path);
        auto const result = ShellExecuteW(
            pici->hwnd,
            L"open",
            fileScoutPath.c_str(),
            argument.c_str(),
            nullptr,
            SW_SHOWNORMAL);
        if (!FileScoutShellExt::DidShellExecuteSucceed(result))
        {
            return E_FAIL;
        }
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
        if (!ppv)
        {
            return E_POINTER;
        }

        *ppv = nullptr;

        if (rclsid != CLSID_FileScoutContextMenu)
        {
            return CLASS_E_CLASSNOTAVAILABLE;
        }

        class Factory : public IClassFactory
        {
        public:
            Factory()
            {
                FileScoutContextMenu::UpdateServerLock(true);
            }

            ~Factory()
            {
                FileScoutContextMenu::UpdateServerLock(false);
            }

            IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
            {
                if (!ppv)
                {
                    return E_POINTER;
                }

                *ppv = nullptr;

                if (riid == IID_IUnknown || riid == IID_IClassFactory)
                {
                    *ppv = static_cast<IClassFactory*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }
            IFACEMETHODIMP_(ULONG) AddRef() override
            {
                return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
            }

            IFACEMETHODIMP_(ULONG) Release() override
            {
                auto const count = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
                if (count == 0)
                {
                    delete this;
                }
                return count;
            }

            IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
            {
                if (!ppv)
                {
                    return E_POINTER;
                }

                *ppv = nullptr;

                if (pUnkOuter)
                {
                    return CLASS_E_NOAGGREGATION;
                }

                auto* ext = new (std::nothrow) FileScoutContextMenu();
                if (!ext)
                {
                    return E_OUTOFMEMORY;
                }

                auto const hr = ext->QueryInterface(riid, ppv);
                ext->Release();
                return hr;
            }

            IFACEMETHODIMP LockServer(BOOL fLock) override
            {
                FileScoutContextMenu::UpdateServerLock(fLock != FALSE);
                return S_OK;
            }

        private:
            LONG m_refCount{ 1 };
        };

        auto* factory = new (std::nothrow) Factory();
        if (!factory)
        {
            return E_OUTOFMEMORY;
        }

        auto const hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }

    STDAPI DllCanUnloadNow()
    {
        return FileScoutContextMenu::CanUnloadNow() ? S_OK : S_FALSE;
    }

    STDAPI DllRegisterServer()
    {
        auto const modulePath = GetShellExtModulePath();
        if (modulePath.empty())
        {
            return SELFREG_E_CLASS;
        }

        auto const fileScoutPath = BuildDefaultFileScoutPath();

        HKEY hKey{};
        auto const createKey = [&](std::wstring const& subKey) -> bool
        {
            return RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
                KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS;
        };

        auto const setDefaultValue = [&](std::wstring const& value) -> bool
        {
            auto const result = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return result == ERROR_SUCCESS;
        };

        auto const setNamedValue = [&](wchar_t const* name, std::wstring const& value) -> bool
        {
            auto const result = RegSetValueExW(hKey, name, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return result == ERROR_SUCCESS;
        };

        for (auto const& subKey : FileScoutShellExt::ContextMenuHandlerRegistryKeys())
        {
            if (!createKey(std::wstring{ subKey }))
                return SELFREG_E_CLASS;
            if (!setDefaultValue(kClsidString)) return SELFREG_E_CLASS;
        }

        if (!createKey(std::wstring{ L"Software\\Classes\\CLSID\\" } + kClsidString))
            return SELFREG_E_CLASS;
        if (!setDefaultValue(L"FileScout Context Menu")) return SELFREG_E_CLASS;

        if (!createKey(std::wstring{ L"Software\\Classes\\CLSID\\" } + kClsidString + L"\\InprocServer32"))
            return SELFREG_E_CLASS;
        if (!setDefaultValue(modulePath)) return SELFREG_E_CLASS;

        if (!createKey(std::wstring{ L"Software\\Classes\\CLSID\\" } + kClsidString + L"\\InprocServer32"))
            return SELFREG_E_CLASS;
        if (!setNamedValue(L"ThreadingModel", L"Apartment")) return SELFREG_E_CLASS;

        // Store install path for GetFileScoutPath()
        if (!createKey(kInstallPathKey))
            return SELFREG_E_CLASS;
        if (!setNamedValue(kInstallPathValue, fileScoutPath)) return SELFREG_E_CLASS;

        return S_OK;
    }

    STDAPI DllUnregisterServer()
    {
        for (auto const& subKey : FileScoutShellExt::ContextMenuHandlerRegistryKeys())
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, std::wstring{ subKey }.c_str());
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring{ L"Software\\Classes\\CLSID\\" } + kClsidString).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, kInstallPathKey);
        return S_OK;
    }
}
