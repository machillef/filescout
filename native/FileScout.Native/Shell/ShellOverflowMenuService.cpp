#include "pch.h"
#include "ShellOverflowMenuService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        constexpr UINT firstCommandId = 1;
        constexpr UINT lastCommandId = 0x7FFF;
        constexpr UINT_PTR menuSubclassId = 0x4653434D;

        bool IsBlockedVerb(std::string const& verb)
        {
            return _stricmp(verb.c_str(), "copy") == 0
                || _stricmp(verb.c_str(), "cut") == 0
                || _stricmp(verb.c_str(), "paste") == 0;
        }

        struct CoTaskMemPidlDeleter
        {
            void operator()(ITEMIDLIST* pidl) const noexcept
            {
                if (pidl != nullptr)
                {
                    CoTaskMemFree(pidl);
                }
            }
        };

        using UniquePidl = std::unique_ptr<ITEMIDLIST, CoTaskMemPidlDeleter>;

        struct MenuHandle
        {
            HMENU value{ nullptr };

            ~MenuHandle()
            {
                if (value != nullptr)
                {
                    DestroyMenu(value);
                }
            }
        };

        struct MenuMessageState
        {
            winrt::com_ptr<IContextMenu2> context_menu2;
            winrt::com_ptr<IContextMenu3> context_menu3;
        };

        LRESULT CALLBACK ShellContextMenuSubclassProc(
            HWND hwnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam,
            UINT_PTR,
            DWORD_PTR refData)
        {
            auto* state = reinterpret_cast<MenuMessageState*>(refData);
            if (state == nullptr)
            {
                return DefSubclassProc(hwnd, message, wParam, lParam);
            }

            switch (message)
            {
            case WM_INITMENUPOPUP:
            case WM_DRAWITEM:
            case WM_MEASUREITEM:
            case WM_MENUCHAR:
                if (state->context_menu3)
                {
                    LRESULT result{};
                    if (SUCCEEDED(state->context_menu3->HandleMenuMsg2(message, wParam, lParam, &result)))
                    {
                        return result;
                    }
                }

                if (state->context_menu2
                    && SUCCEEDED(state->context_menu2->HandleMenuMsg(message, wParam, lParam)))
                {
                    return 0;
                }

                break;
            default:
                break;
            }

            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        class MenuMessageSubclass
        {
        public:
            MenuMessageSubclass(HWND ownerWindow, winrt::com_ptr<IContextMenu> const& contextMenu)
                : m_ownerWindow(ownerWindow)
            {
                auto state = std::make_unique<MenuMessageState>();
                state->context_menu2 = contextMenu.try_as<IContextMenu2>();
                state->context_menu3 = contextMenu.try_as<IContextMenu3>();
                m_state = state.release();

                if (!SetWindowSubclass(
                        ownerWindow,
                        ShellContextMenuSubclassProc,
                        menuSubclassId,
                        reinterpret_cast<DWORD_PTR>(m_state)))
                {
                    delete m_state;
                    m_state = nullptr;
                    throw std::runtime_error("SetWindowSubclass failed for shell overflow menu");
                }
            }

            ~MenuMessageSubclass()
            {
                if (m_ownerWindow != nullptr)
                {
                    RemoveWindowSubclass(m_ownerWindow, ShellContextMenuSubclassProc, menuSubclassId);
                }

                delete m_state;
            }

            MenuMessageSubclass(MenuMessageSubclass const&) = delete;
            MenuMessageSubclass& operator=(MenuMessageSubclass const&) = delete;

        private:
            HWND m_ownerWindow{ nullptr };
            MenuMessageState* m_state{ nullptr };
        };

        UniquePidl ParseShellPath(std::filesystem::path const& path)
        {
            ITEMIDLIST* pidl = nullptr;
            auto widePath = path.wstring();
            winrt::check_hresult(SHParseDisplayName(widePath.c_str(), nullptr, &pidl, 0, nullptr));
            if (pidl == nullptr)
            {
                throw std::runtime_error("SHParseDisplayName returned a null PIDL");
            }

            return UniquePidl{ pidl };
        }

        winrt::com_ptr<IContextMenu> CreateContextMenu(HWND ownerWindow, std::filesystem::path const& path)
        {
            auto absolutePidl = ParseShellPath(path);

            winrt::com_ptr<IShellFolder> parentFolder;
            PCUITEMID_CHILD childPidl = nullptr;
            winrt::check_hresult(
                SHBindToParent(absolutePidl.get(), __uuidof(IShellFolder), parentFolder.put_void(), &childPidl));

            if (childPidl == nullptr)
            {
                throw std::runtime_error("SHBindToParent returned a null child PIDL");
            }

            PCUITEMID_CHILD children[]{ childPidl };
            winrt::com_ptr<IContextMenu> contextMenu;
            winrt::check_hresult(parentFolder->GetUIObjectOf(
                ownerWindow,
                1,
                children,
                __uuidof(IContextMenu),
                nullptr,
                contextMenu.put_void()));

            return contextMenu;
        }

        std::string GetCommandVerb(IContextMenu* contextMenu, UINT commandOffset)
        {
            std::array<char, 256> buffer{};
            if (FAILED(contextMenu->GetCommandString(
                    commandOffset,
                    GCS_VERB,
                    nullptr,
                    reinterpret_cast<LPSTR>(buffer.data()),
                    static_cast<UINT>(buffer.size()))))
            {
                return {};
            }

            return std::string(buffer.data());
        }

        void FilterBlockedVerbs(HMENU menu, IContextMenu* contextMenu)
        {
            auto const itemCount = GetMenuItemCount(menu);
            if (itemCount <= 0)
            {
                return;
            }

            for (int position = itemCount - 1; position >= 0; --position)
            {
                auto const menuId = GetMenuItemID(menu, position);
                if (menuId < firstCommandId || menuId > lastCommandId)
                {
                    continue;
                }

                auto const verb = GetCommandVerb(contextMenu, menuId - firstCommandId);
                if (!verb.empty() && IsBlockedVerb(verb))
                {
                    DeleteMenu(menu, static_cast<UINT>(position), MF_BYPOSITION);
                }
            }
        }
    }

    ShellOverflowMenuOutcome ShellOverflowMenuService::ShowForPath(
        HWND const ownerWindow,
        std::filesystem::path const& path,
        POINT const screenPoint) const
    {
        if (ownerWindow == nullptr)
        {
            throw std::runtime_error("Shell overflow menu requires a valid window handle");
        }

        auto contextMenu = CreateContextMenu(ownerWindow, path);
        MenuHandle popupMenu{ CreatePopupMenu() };
        if (popupMenu.value == nullptr)
        {
            throw std::runtime_error("CreatePopupMenu failed for shell overflow menu");
        }

        auto const queryFlags = static_cast<UINT>(CMF_NORMAL | CMF_EXPLORE);
        winrt::check_hresult(contextMenu->QueryContextMenu(
            popupMenu.value,
            0,
            firstCommandId,
            lastCommandId,
            queryFlags));

        FilterBlockedVerbs(popupMenu.value, contextMenu.get());
        MenuMessageSubclass subclass(ownerWindow, contextMenu);

        auto const selectedCommandId = static_cast<UINT>(TrackPopupMenu(
            popupMenu.value,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            0,
            ownerWindow,
            nullptr));

        if (selectedCommandId == 0)
        {
            return ShellOverflowMenuOutcome::Dismissed;
        }

        CMINVOKECOMMANDINFOEX invoke{};
        invoke.cbSize = sizeof(invoke);
        invoke.fMask = CMIC_MASK_PTINVOKE;
        invoke.hwnd = ownerWindow;
        invoke.lpVerb = MAKEINTRESOURCEA(selectedCommandId - firstCommandId);
        invoke.nShow = SW_SHOWNORMAL;
        invoke.ptInvoke = screenPoint;

        winrt::check_hresult(contextMenu->InvokeCommand(
            reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke)));

        return ShellOverflowMenuOutcome::Invoked;
    }
}
