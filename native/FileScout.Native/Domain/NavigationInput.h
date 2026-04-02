#pragma once

namespace FileScoutNative::Domain
{
    enum class NavigationInputCommand
    {
        None,
        Back,
        Forward,
    };

    inline NavigationInputCommand ResolveNavigationInputCommand(
        UINT const message,
        WPARAM const wParam,
        LPARAM const lParam)
    {
        if (message == WM_XBUTTONUP)
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
}
