#include "pch.h"
#include "ShellInteropService.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        constexpr wchar_t const* folderIconCacheKey = L"__folder__";
        constexpr wchar_t const* genericFileIconCacheKey = L"__file__";

        std::wstring BuildKindLabel(std::filesystem::path const& path, bool const isFolder)
        {
            if (isFolder)
            {
                return L"Folder";
            }

            auto extension = path.extension().wstring();
            if (extension.empty())
            {
                return L"File";
            }

            if (!extension.empty() && extension.front() == L'.')
            {
                extension.erase(0, 1);
            }

            if (extension.empty())
            {
                return L"File";
            }

            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t const ch)
                {
                    return static_cast<wchar_t>(towupper(ch));
                });
            return extension + L" file";
        }

        std::wstring BuildIconCacheKey(std::filesystem::path const& path, bool const isFolder)
        {
            if (isFolder)
            {
                return folderIconCacheKey;
            }

            auto extension = path.extension().wstring();
            if (extension.empty())
            {
                return genericFileIconCacheKey;
            }

            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t const ch)
                {
                    return static_cast<wchar_t>(towlower(ch));
                });
            return extension;
        }

        std::optional<FileScoutNative::Domain::IconBitmap> ExtractShellIconBitmap(
            std::wstring const& shellLookupName,
            DWORD const fileAttributes)
        {
            // COM is already initialized by winrt::init_apartment in main.cpp.
            SHFILEINFOW shellInfo{};
            const auto result = SHGetFileInfoW(
                shellLookupName.c_str(),
                fileAttributes,
                &shellInfo,
                sizeof(shellInfo),
                SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);

            if (result == 0 || shellInfo.hIcon == nullptr)
            {
                return std::nullopt;
            }

            ICONINFO iconInfo{};
            if (!GetIconInfo(shellInfo.hIcon, &iconInfo))
            {
                DestroyIcon(shellInfo.hIcon);
                return std::nullopt;
            }

            BITMAP bitmap{};
            const auto bitmapResult = GetObjectW(
                iconInfo.hbmColor,
                sizeof(bitmap),
                &bitmap);

            if (bitmapResult == 0 || bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0)
            {
                if (iconInfo.hbmColor != nullptr)
                {
                    DeleteObject(iconInfo.hbmColor);
                }
                if (iconInfo.hbmMask != nullptr)
                {
                    DeleteObject(iconInfo.hbmMask);
                }
                DestroyIcon(shellInfo.hIcon);
                return std::nullopt;
            }

            const auto width = static_cast<uint32_t>(bitmap.bmWidth);
            const auto height = static_cast<uint32_t>(bitmap.bmHeight);

            auto deviceContext = CreateCompatibleDC(nullptr);
            if (deviceContext == nullptr)
            {
                if (iconInfo.hbmColor != nullptr)
                {
                    DeleteObject(iconInfo.hbmColor);
                }
                if (iconInfo.hbmMask != nullptr)
                {
                    DeleteObject(iconInfo.hbmMask);
                }
                DestroyIcon(shellInfo.hIcon);
                return std::nullopt;
            }

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(width);
            info.bmiHeader.biHeight = -static_cast<LONG>(height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            std::vector<uint8_t> pixels(width * height * 4);
            const auto rowsCopied = GetDIBits(
                deviceContext,
                iconInfo.hbmColor,
                0,
                height,
                pixels.data(),
                &info,
                DIB_RGB_COLORS);

            DeleteDC(deviceContext);
            if (iconInfo.hbmColor != nullptr)
            {
                DeleteObject(iconInfo.hbmColor);
            }
            if (iconInfo.hbmMask != nullptr)
            {
                DeleteObject(iconInfo.hbmMask);
            }
            DestroyIcon(shellInfo.hIcon);

            if (rowsCopied == 0)
            {
                return std::nullopt;
            }

            const auto hasAlpha = std::any_of(
                pixels.begin(),
                pixels.end(),
                [index = std::size_t{ 0 }](uint8_t const channel) mutable
                {
                    const auto isAlphaChannel = (index++ % 4) == 3;
                    return isAlphaChannel && channel != 0;
                });

            for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
            {
                std::swap(pixels[index], pixels[index + 2]);
                if (!hasAlpha)
                {
                    pixels[index + 3] = 255;
                }
            }

            return FileScoutNative::Domain::IconBitmap{
                width,
                height,
                std::move(pixels),
            };
        }
    }

    FileScoutNative::Domain::IconBitmap ShellInteropService::GetIconForItem(
        std::filesystem::path const& path,
        bool const isFolder)
    {
        const auto cacheKey = BuildIconCacheKey(path, isFolder);
        if (auto const existing = m_iconCache.find(cacheKey); existing != m_iconCache.end())
        {
            return existing->second;
        }

        std::wstring lookupName;
        DWORD fileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (isFolder)
        {
            lookupName = L"folder";
            fileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        }
        else
        {
            auto extension = path.extension().wstring();
            if (extension.empty())
            {
                lookupName = L"file";
            }
            else
            {
                lookupName = L"file" + extension;
            }
        }

        auto icon = ExtractShellIconBitmap(lookupName, fileAttributes).value_or(FileScoutNative::Domain::IconBitmap{});
        m_iconCache.emplace(cacheKey, icon);
        return icon;
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateDirectory(
        std::filesystem::path const& path)
    {
        std::vector<FileScoutNative::Domain::ShellItemRef> items;

        for (auto const& entry : std::filesystem::directory_iterator(path))
        {
            FileScoutNative::Domain::ShellItemRef item;
            item.path = entry.path();
            item.display_name = entry.path().filename().wstring();
            item.parsing_name = entry.path().wstring();
            item.is_folder = entry.is_directory();
            item.kind_label = BuildKindLabel(entry.path(), item.is_folder);
            item.icon = GetIconForItem(item.path, item.is_folder);

            if (!item.is_folder)
            {
                std::error_code sizeError;
                const auto size = entry.file_size(sizeError);
                if (!sizeError)
                {
                    item.size_available = true;
                    item.size_bytes = size;
                }
            }

            // Populate date modified for sorting and display.
            std::error_code timeError;
            auto const writeTime = entry.last_write_time(timeError);
            if (!timeError)
            {
                item.date_modified = writeTime;
                item.date_modified_available = true;

                // Convert file_time_type → FILETIME → SYSTEMTIME for locale-aware formatting.
                // On Windows, file_time_type epoch matches FILETIME (100-ns ticks since 1601-01-01).
                // Cast to ULONGLONG before splitting to avoid implementation-defined signed right-shift.
                auto const ticks = static_cast<ULONGLONG>(writeTime.time_since_epoch().count());
                FILETIME ft{};
                ft.dwLowDateTime  = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
                ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

                SYSTEMTIME utc{};
                SYSTEMTIME local{};
                if (FileTimeToSystemTime(&ft, &utc) &&
                    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
                {
                    wchar_t dateBuf[64]{};
                    wchar_t timeBuf[64]{};
                    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
                                    &local, nullptr, dateBuf, 64, nullptr);
                    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
                                    &local, nullptr, timeBuf, 64);
                    item.date_modified_str = std::wstring{ dateBuf } + L"  " + std::wstring{ timeBuf };
                }
            }

            items.push_back(std::move(item));
        }

        // NOTE: Sorting is intentionally omitted here — callers (e.g. LoadDirectory)
        // apply the user-selected sort column and direction after enumeration.
        return items;
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateDrives()
    {
        std::vector<FileScoutNative::Domain::ShellItemRef> drives;

        wchar_t driveBuf[MAX_PATH]{};
        const DWORD length = GetLogicalDriveStringsW(MAX_PATH - 1, driveBuf);
        if (length == 0 || length > MAX_PATH)
        {
            return drives;
        }

        for (wchar_t const* cursor = driveBuf; *cursor != L'\0'; cursor += wcslen(cursor) + 1)
        {
            std::filesystem::path drivePath{ cursor };

            const UINT driveType = GetDriveTypeW(cursor);

            // Skip drive letters that have no accessible root.
            if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR)
            {
                continue;
            }

            std::wstring kindLabel;
            switch (driveType)
            {
            case DRIVE_REMOVABLE: kindLabel = L"Removable Drive"; break;
            case DRIVE_CDROM:     kindLabel = L"CD/DVD Drive";     break;
            case DRIVE_REMOTE:    kindLabel = L"Network Drive";    break;
            case DRIVE_RAMDISK:   kindLabel = L"RAM Disk";         break;
            default:              kindLabel = L"Local Disk";       break;
            }

            // Use the volume label as the display name when available.
            wchar_t volLabel[MAX_PATH]{};
            GetVolumeInformationW(cursor, volLabel, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);

            std::wstring displayName;
            if (volLabel[0] != L'\0')
            {
                displayName = std::wstring{ volLabel } + L" (" + drivePath.root_name().wstring() + L")";
            }
            else
            {
                displayName = kindLabel + L" (" + drivePath.root_name().wstring() + L")";
            }

            FileScoutNative::Domain::ShellItemRef item;
            item.path = drivePath;
            item.display_name = displayName;
            item.parsing_name = drivePath.wstring();
            item.kind_label = kindLabel;
            item.is_folder = true;
            item.icon = GetIconForItem(drivePath, true);

            drives.push_back(std::move(item));
        }

        return drives;
    }
}
