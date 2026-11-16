#pragma once

#include "pch.h"

// CLSID for the FileScout shell context menu extension. Generated once (2026-11-15); keep it
// stable, registrations on users' machines refer to it.
// {9722A202-6B0E-4C00-8493-B602331141AD}
static constexpr CLSID CLSID_FileScoutContextMenu = {
    0x9722A202, 0x6B0E, 0x4C00,
    { 0x84, 0x93, 0xB6, 0x02, 0x33, 0x11, 0x41, 0xAD }
};

namespace FileScoutShellExt
{
    inline constexpr wchar_t FileScoutExeName[] = L"FileScoutNative.exe";
    inline constexpr std::array<std::wstring_view, 2> ContextMenuHandlerRegistryKeyValues{
        L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\FileScout",
        L"Software\\Classes\\Drive\\shellex\\ContextMenuHandlers\\FileScout",
    };

    inline constexpr wchar_t ClsidString[] = L"{9722A202-6B0E-4C00-8493-B602331141AD}";

    // Where FileScoutNative.exe can be, best first:
    //   1. installed: next to the DLL;
    //   2. development build: DLL in native\bin\<platform>\<config>\ShellExt\,
    //      exe in native\<platform>\<config>\FileScout.Native\.
    inline std::vector<std::wstring> BuildFileScoutPathCandidates(std::wstring const& modulePath)
    {
        std::vector<std::wstring> candidates;
        auto const path = std::filesystem::path{ modulePath };
        auto const shellExtDir = path.parent_path();
        if (modulePath.empty() || shellExtDir.empty())
        {
            return candidates;
        }

        candidates.push_back((shellExtDir / FileScoutExeName).wstring());

        auto const configurationDir = shellExtDir.parent_path();
        auto const platformDir = configurationDir.parent_path();
        auto const binDir = platformDir.parent_path();
        auto const nativeDir = binDir.parent_path();
        if (!binDir.has_filename() || !configurationDir.has_filename() || !platformDir.has_filename())
        {
            return candidates;
        }

        candidates.push_back(
            (nativeDir / platformDir.filename() / configurationDir.filename() / L"FileScout.Native" / FileScoutExeName)
                .wstring());
        return candidates;
    }

    // First candidate that exists; otherwise the development path (or the bare exe name) so the
    // error a user sees names a plausible location.
    template <typename IsFile>
    std::wstring ResolveFileScoutPathFromShellExtensionPath(std::wstring const& modulePath, IsFile&& isFile)
    {
        auto const candidates = BuildFileScoutPathCandidates(modulePath);
        for (auto const& candidate : candidates)
        {
            if (isFile(std::filesystem::path{ candidate }))
            {
                return candidate;
            }
        }

        return candidates.empty() ? std::wstring{ FileScoutExeName } : candidates.back();
    }

    inline std::wstring BuildFileScoutPathFromShellExtensionPath(std::wstring const& modulePath)
    {
        return ResolveFileScoutPathFromShellExtensionPath(modulePath, [](std::filesystem::path const& candidate)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(candidate, ec);
        });
    }

    // Reads a Win32 string whose API takes (buffer, capacity) and returns the written length, or
    // the capacity when it truncated (GetModuleFileNameW). Grows the buffer instead of MAX_PATH.
    template <typename Read>
    std::wstring ReadGrowingString(Read&& read)
    {
        for (DWORD capacity = 260; capacity <= 32768 * 2; capacity *= 2)
        {
            std::wstring buffer(capacity, L'\0');
            auto const length = read(buffer.data(), capacity);
            if (length == 0)
            {
                return {};
            }

            if (length < capacity)
            {
                buffer.resize(length);
                return buffer;
            }
        }

        return {};
    }

    inline auto const& ContextMenuHandlerRegistryKeys()
    {
        return ContextMenuHandlerRegistryKeyValues;
    }

    inline std::wstring ResolveFileScoutPath(
        std::wstring const& storedPath,
        std::wstring const& fallbackPath)
    {
        if (!storedPath.empty() && std::filesystem::is_regular_file(storedPath))
        {
            return storedPath;
        }

        return fallbackPath;
    }

    inline std::wstring QuoteCommandLineArgument(std::wstring const& argument)
    {
        std::wstring quoted;
        quoted.push_back(L'"');

        size_t backslashCount = 0;
        for (wchar_t ch : argument)
        {
            if (ch == L'\\')
            {
                ++backslashCount;
                continue;
            }

            if (ch == L'"')
            {
                quoted.append(backslashCount * 2 + 1, L'\\');
                quoted.push_back(ch);
                backslashCount = 0;
                continue;
            }

            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
            quoted.push_back(ch);
        }

        quoted.append(backslashCount * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    inline bool DidShellExecuteSucceed(HINSTANCE result)
    {
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    inline void PrepareForInitialize(std::vector<std::wstring>& selectedPaths, size_t count)
    {
        selectedPaths.clear();
        selectedPaths.reserve(count);
    }
}

class DECLSPEC_UUID("9722A202-6B0E-4C00-8493-B602331141AD")
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
