#pragma once

#include <cstdint>

namespace FileScoutNative::Domain
{
    enum class NavigationInputCommand
    {
        None,
        Back,
        Forward,
    };

    inline NavigationInputCommand ResolveNonClientXButtonCommand(WPARAM const wParam) noexcept
    {
        switch (GET_XBUTTON_WPARAM(wParam))
        {
        case XBUTTON1:
            return NavigationInputCommand::Back;
        case XBUTTON2:
            return NavigationInputCommand::Forward;
        default:
            return NavigationInputCommand::None;
        }
    }

    inline NavigationInputCommand ResolveNavigationInputCommand(
        UINT const message,
        WPARAM const wParam,
        LPARAM const lParam)
    {
        if (message == WM_XBUTTONUP || message == WM_NCXBUTTONUP)
        {
            return ResolveNonClientXButtonCommand(wParam);
        }

        if (message == WM_APPCOMMAND)
        {
            switch (GET_APPCOMMAND_LPARAM(lParam))
            {
            case APPCOMMAND_BROWSER_BACKWARD:
                return NavigationInputCommand::Back;
            case APPCOMMAND_BROWSER_FORWARD:
                return NavigationInputCommand::Forward;
            default:
                return NavigationInputCommand::None;
            }
        }

        return NavigationInputCommand::None;
    }

    // Coordinates mouse Back/Forward navigation between WinUI routed pointer events (client area,
    // delivered via WinUI's child InputSite HWND) and the top-level Win32 window subclass
    // (non-client titlebar clicks and hardware WM_APPCOMMAND keys) so a single physical mouse
    // click never triggers two navigations even if a driver bubbles both.
    class MouseNavigationDeduplicator
    {
    public:
        static constexpr std::uint64_t kReleaseSuppressWindowMs = 150;

        void OnPointerPressed(NavigationInputCommand const command) noexcept
        {
            if (command == NavigationInputCommand::Back)
            {
                m_pressedMask |= kBackBit;
            }
            else if (command == NavigationInputCommand::Forward)
            {
                m_pressedMask |= kForwardBit;
            }
        }

        void OnPointerReleased(NavigationInputCommand const command, std::uint64_t const nowMs) noexcept
        {
            if (command == NavigationInputCommand::Back && (m_pressedMask & kBackBit) != 0)
            {
                m_pressedMask &= static_cast<std::uint8_t>(~kBackBit);
                m_lastBackReleaseMs = nowMs;
                m_hasBackRelease = true;
            }
            else if (command == NavigationInputCommand::Forward && (m_pressedMask & kForwardBit) != 0)
            {
                m_pressedMask &= static_cast<std::uint8_t>(~kForwardBit);
                m_lastForwardReleaseMs = nowMs;
                m_hasForwardRelease = true;
            }
        }

        // A new WM_NCXBUTTONDOWN begins an independent non-client click (e.g. on the titlebar),
        // so clear any stale client-area suppression state for that button only.
        void OnNonClientButtonDown(NavigationInputCommand const command) noexcept
        {
            if (command == NavigationInputCommand::Back)
            {
                m_pressedMask &= static_cast<std::uint8_t>(~kBackBit);
                m_hasBackRelease = false;
            }
            else if (command == NavigationInputCommand::Forward)
            {
                m_pressedMask &= static_cast<std::uint8_t>(~kForwardBit);
                m_hasForwardRelease = false;
            }
        }

        void ResetPressedState() noexcept
        {
            m_pressedMask = 0;
        }

        [[nodiscard]] bool ShouldSuppressWin32Command(
            UINT const message,
            LPARAM const lParam,
            NavigationInputCommand const command,
            std::uint64_t const nowMs) noexcept
        {
            if (command == NavigationInputCommand::None)
            {
                return false;
            }

            // Hardware keyboard/OEM media Back/Forward keys never originate from mouse pointer events.
            if (message == WM_APPCOMMAND && GET_DEVICE_LPARAM(lParam) != FAPPCOMMAND_MOUSE)
            {
                return false;
            }

            if (command == NavigationInputCommand::Back)
            {
                if ((m_pressedMask & kBackBit) != 0)
                {
                    m_pressedMask &= static_cast<std::uint8_t>(~kBackBit);
                    m_lastBackReleaseMs = nowMs;
                    m_hasBackRelease = true;
                    return true;
                }
                return m_hasBackRelease &&
                    nowMs >= m_lastBackReleaseMs &&
                    (nowMs - m_lastBackReleaseMs) <= kReleaseSuppressWindowMs;
            }

            if ((m_pressedMask & kForwardBit) != 0)
            {
                m_pressedMask &= static_cast<std::uint8_t>(~kForwardBit);
                m_lastForwardReleaseMs = nowMs;
                m_hasForwardRelease = true;
                return true;
            }
            return m_hasForwardRelease &&
                nowMs >= m_lastForwardReleaseMs &&
                (nowMs - m_lastForwardReleaseMs) <= kReleaseSuppressWindowMs;
        }

    private:
        static constexpr std::uint8_t kBackBit = 0x1;
        static constexpr std::uint8_t kForwardBit = 0x2;

        std::uint8_t m_pressedMask{ 0 };
        bool m_hasBackRelease{ false };
        bool m_hasForwardRelease{ false };
        std::uint64_t m_lastBackReleaseMs{ 0 };
        std::uint64_t m_lastForwardReleaseMs{ 0 };
    };
}
