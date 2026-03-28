#include "pch.h"
#include "SidebarIconInfo.h"

namespace FileScoutNative::Domain
{
    namespace
    {
        bool EqualsIgnoreCase(
            std::wstring_view const left,
            std::wstring_view const right)
        {
            return left.size() == right.size() &&
                _wcsnicmp(left.data(), right.data(), left.size()) == 0;
        }
    }

    std::wstring_view KnownFolderGlyph(std::wstring_view const displayName)
    {
        using namespace std::literals;

        if (EqualsIgnoreCase(displayName, L"Home"sv) ||
            EqualsIgnoreCase(displayName, L"Quick access"sv))
        {
            return L"\uE80F";
        }

        if (EqualsIgnoreCase(displayName, L"Desktop"sv))
        {
            return L"\uEC64";
        }

        if (EqualsIgnoreCase(displayName, L"Documents"sv))
        {
            return L"\uE8A5";
        }

        if (EqualsIgnoreCase(displayName, L"Downloads"sv))
        {
            return L"\uE896";
        }

        if (EqualsIgnoreCase(displayName, L"Pictures"sv))
        {
            return L"\uEB9F";
        }

        if (EqualsIgnoreCase(displayName, L"Music"sv))
        {
            return L"\uEC4F";
        }

        if (EqualsIgnoreCase(displayName, L"Videos"sv))
        {
            return L"\uE8B2";
        }

        if (EqualsIgnoreCase(displayName, L"This PC"sv))
        {
            return L"\uEDA2";
        }

        if (EqualsIgnoreCase(displayName, L"Recycle Bin"sv))
        {
            return L"\uE74D";
        }

        return {};
    }

    std::wstring_view DriveTypeGlyph(std::uint32_t const driveType)
    {
        switch (driveType)
        {
        case DRIVE_REMOVABLE:
            return L"\uE88E"; // USB
        case DRIVE_REMOTE:
            return L"\uEDA3"; // NetworkAdapter
        case DRIVE_FIXED:
        case DRIVE_RAMDISK:
        case DRIVE_CDROM:
        default:
            return L"\uEDA2"; // HardDrive
        }
    }
}
