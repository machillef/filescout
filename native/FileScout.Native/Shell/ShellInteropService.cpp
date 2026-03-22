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

        std::optional<std::wstring> TryGetArchiveKindLabel(std::filesystem::path const& path)
        {
            auto name = path.filename().wstring();
            if (name.empty())
            {
                return std::nullopt;
            }

            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t const ch)
                {
                    return static_cast<wchar_t>(towlower(ch));
                });

            static constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 14> archiveSuffixes{{
                { L".tar.gz",  L"TAR.GZ"  },
                { L".tar.bz2", L"TAR.BZ2" },
                { L".tar.xz",  L"TAR.XZ"  },
                { L".tgz",     L"TGZ"     },
                { L".tbz",     L"TBZ"     },
                { L".tbz2",    L"TBZ2"    },
                { L".txz",     L"TXZ"     },
                { L".zip",     L"ZIP"     },
                { L".tar",     L"TAR"     },
                { L".7z",      L"7Z"      },
                { L".rar",     L"RAR"     },
                { L".gz",      L"GZ"      },
                { L".bz2",     L"BZ2"     },
                { L".xz",      L"XZ"      },
            }};

            for (auto const& [suffix, label] : archiveSuffixes)
            {
                if (name.size() >= suffix.size() &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix.data()) == 0)
                {
                    return std::wstring{ label } + L" archive";
                }
            }

            return std::nullopt;
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
            DWORD const fileAttributes,
            bool const useLiteralPath = false)
        {
            // COM is already initialized by winrt::init_apartment in main.cpp.
            SHFILEINFOW shellInfo{};
            DWORD const flags = useLiteralPath
                ? SHGFI_ICON | SHGFI_SMALLICON
                : SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
            const auto result = SHGetFileInfoW(
                shellLookupName.c_str(),
                fileAttributes,
                &shellInfo,
                sizeof(shellInfo),
                flags);

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

        std::optional<std::wstring> TryGetShellString(
            IShellItem* const shellItem,
            SIGDN const displayNameKind)
        {
            PWSTR value{};
            if (FAILED(shellItem->GetDisplayName(displayNameKind, &value)) || value == nullptr)
            {
                return std::nullopt;
            }

            std::wstring result{ value };
            CoTaskMemFree(value);
            return result;
        }

        std::optional<std::wstring> TryGetShellPropertyString(
            IShellItem2* const shellItem,
            PROPERTYKEY const& key)
        {
            PWSTR value{};
            if (FAILED(shellItem->GetString(key, &value)) || value == nullptr)
            {
                return std::nullopt;
            }

            std::wstring result{ value };
            CoTaskMemFree(value);
            return result;
        }

        std::optional<std::wstring> FormatShellDateTime(FILETIME const& ft)
        {
            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (!FileTimeToSystemTime(&ft, &utc) ||
                !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            {
                return std::nullopt;
            }

            wchar_t dateBuf[64]{};
            wchar_t timeBuf[64]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
                            &local, nullptr, dateBuf, 64, nullptr);
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
                            &local, nullptr, timeBuf, 64);
            return std::wstring{ dateBuf } + L"  " + std::wstring{ timeBuf };
        }

        std::filesystem::file_time_type FileTimeToChrono(FILETIME const& ft)
        {
            ULARGE_INTEGER ticks{};
            ticks.LowPart = ft.dwLowDateTime;
            ticks.HighPart = ft.dwHighDateTime;
            return std::filesystem::file_time_type{
                std::filesystem::file_time_type::duration{
                    static_cast<std::filesystem::file_time_type::rep>(ticks.QuadPart) } };
        }
    }

    FileScoutNative::Domain::IconBitmap ShellInteropService::GetIconForItem(
        std::filesystem::path const& path,
        bool const isFolder) const
    {
        const auto cacheKey = BuildIconCacheKey(path, isFolder);
        {
            std::lock_guard lock{ m_cacheMutex };
            if (auto const existing = m_iconCache.find(cacheKey); existing != m_iconCache.end())
            {
                return existing->second;
            }
        }

        std::wstring lookupName;
        DWORD fileAttributes = FILE_ATTRIBUTE_NORMAL;
        bool useLiteralPath = false;
        if (isFolder)
        {
            // Drive roots (e.g. C:\) need the actual path so the shell returns the
            // drive-specific icon instead of the generic folder icon.
            if (path == path.root_path() && !path.root_name().empty())
            {
                lookupName = path.wstring();
                useLiteralPath = true;
            }
            else
            {
                lookupName = L"folder";
                fileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            }
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

        // Extract icon outside the lock — GDI/SHGetFileInfoW can be slow.
        auto icon = ExtractShellIconBitmap(lookupName, fileAttributes, useLiteralPath).value_or(FileScoutNative::Domain::IconBitmap{});
        {
            std::lock_guard lock{ m_cacheMutex };
            m_iconCache.emplace(cacheKey, icon);
        }
        return icon;
    }

    FileScoutNative::Domain::ShellItemRef ShellInteropService::BuildShellItemRef(
        std::filesystem::directory_entry const& entry) const
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
            if (auto const archiveKindLabel = TryGetArchiveKindLabel(item.path);
                archiveKindLabel.has_value())
            {
                item.is_archive_candidate = true;
                item.kind_label = *archiveKindLabel;
                item.is_supported_archive = CanEnumerateShellNamespace(item.path);
            }
        }

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

        // Populate creation time, file attributes, and image flag via GetFileAttributesExW.
        WIN32_FILE_ATTRIBUTE_DATA attrData{};
        auto const itemPathStr = item.path.wstring();
        if (GetFileAttributesExW(itemPathStr.c_str(), GetFileExInfoStandard, &attrData))
        {
            item.file_attributes = static_cast<uint32_t>(attrData.dwFileAttributes);
            item.file_attributes_available = true;

            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (FileTimeToSystemTime(&attrData.ftCreationTime, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            {
                wchar_t dateBuf[64]{};
                wchar_t timeBuf[64]{};
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
                                &local, nullptr, dateBuf, 64, nullptr);
                GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
                                &local, nullptr, timeBuf, 64);
                item.date_created_str = std::wstring{ dateBuf } + L"  " + std::wstring{ timeBuf };
                item.date_created_available = true;
            }
        }

        // Detect image files by extension for the preview panel.
        if (!item.is_folder)
        {
            auto ext = item.path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t const ch)
                { return static_cast<wchar_t>(towlower(ch)); });
            static constexpr std::array<std::wstring_view, 9> imgExts{
                L".jpg", L".jpeg", L".png", L".gif", L".bmp",
                L".ico", L".webp", L".tiff", L".tif",
            };
            item.is_image = std::ranges::any_of(imgExts, [&ext](std::wstring_view e)
                { return ext == e; });
        }

        return item;
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateDirectory(
        std::filesystem::path const& path)
    {
        std::vector<FileScoutNative::Domain::ShellItemRef> items;
        for (auto const& entry : std::filesystem::directory_iterator(path))
            items.push_back(BuildShellItemRef(entry));
        // NOTE: Sorting is intentionally omitted here — callers apply the user-selected
        // sort column and direction after enumeration.
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

    FileScoutNative::Domain::IconBitmap ShellInteropService::GetItemIcon(
        std::filesystem::path const& path,
        bool const isFolder) const
    {
        return GetIconForItem(path, isFolder);
    }

    bool ShellInteropService::CanEnumerateShellNamespace(
        std::filesystem::path const& path) const
    {
        std::error_code absoluteError;
        auto const absolute = std::filesystem::absolute(path, absoluteError).lexically_normal();
        if (absoluteError)
        {
            return false;
        }

        auto const cacheKey = absolute.wstring();
        {
            std::scoped_lock lock(m_archiveCapabilityMutex);
            if (auto const cached = m_archiveCapabilityCache.find(cacheKey);
                cached != m_archiveCapabilityCache.end())
            {
                return cached->second;
            }
        }

        HRESULT const initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool const shouldUninitialize = SUCCEEDED(initHr);
        bool supported = false;

        if (SUCCEEDED(initHr) || initHr == RPC_E_CHANGED_MODE)
        {
            winrt::com_ptr<IShellItem> shellItem;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                cacheKey.c_str(),
                nullptr,
                IID_PPV_ARGS(shellItem.put()))))
            {
                winrt::com_ptr<IEnumShellItems> enumerator;
                supported = SUCCEEDED(shellItem->BindToHandler(
                    nullptr,
                    BHID_EnumItems,
                    IID_PPV_ARGS(enumerator.put())));
            }
        }

        if (shouldUninitialize)
        {
            CoUninitialize();
        }

        {
            std::scoped_lock lock(m_archiveCapabilityMutex);
            m_archiveCapabilityCache.insert_or_assign(cacheKey, supported);
        }

        return supported;
    }

    bool ShellInteropService::IsSupportedArchiveFile(std::filesystem::path const& path) const
    {
        std::error_code fileError;
        return std::filesystem::is_regular_file(path, fileError) &&
               !fileError &&
               CanEnumerateShellNamespace(path);
    }

    std::optional<ResolvedArchivePath> ShellInteropService::ResolveArchivePath(
        std::filesystem::path const& path) const
    {
        std::error_code absoluteError;
        auto const absolute = std::filesystem::absolute(path, absoluteError).lexically_normal();
        if (absoluteError)
        {
            return std::nullopt;
        }

        for (auto cursor = absolute;;)
        {
            if (IsSupportedArchiveFile(cursor))
            {
                return ResolvedArchivePath{
                    cursor,
                    absolute,
                };
            }

            auto const parent = cursor.parent_path();
            if (parent.empty() || parent == cursor)
            {
                break;
            }

            cursor = parent;
        }

        return std::nullopt;
    }

    bool ShellInteropService::IsArchivePath(std::filesystem::path const& path) const
    {
        return ResolveArchivePath(path).has_value();
    }

    std::optional<ResolvedArchivePath> ShellInteropService::TryResolveArchivePath(
        std::filesystem::path const& path) const
    {
        return ResolveArchivePath(path);
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateArchiveDirectory(
        std::filesystem::path const& path) const
    {
        auto const resolved = ResolveArchivePath(path);
        if (!resolved.has_value())
        {
            throw std::runtime_error("Unsupported archive path.");
        }

        HRESULT const initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool const shouldUninitialize = SUCCEEDED(initHr);

        std::vector<FileScoutNative::Domain::ShellItemRef> items;
        try
        {
            winrt::com_ptr<IShellItem> folderItem;
            winrt::check_hresult(SHCreateItemFromParsingName(
                resolved->virtual_path.wstring().c_str(),
                nullptr,
                IID_PPV_ARGS(folderItem.put())));

            winrt::com_ptr<IEnumShellItems> enumerator;
            winrt::check_hresult(folderItem->BindToHandler(
                nullptr,
                BHID_EnumItems,
                IID_PPV_ARGS(enumerator.put())));

            while (true)
            {
                winrt::com_ptr<IShellItem> child;
                ULONG fetched{};
                auto const nextHr = enumerator->Next(1, child.put(), &fetched);
                if (nextHr == S_FALSE || fetched == 0)
                {
                    break;
                }
                winrt::check_hresult(nextHr);

                auto parsingName = TryGetShellString(child.get(), SIGDN_DESKTOPABSOLUTEPARSING);
                if (!parsingName.has_value())
                {
                    continue;
                }

                auto displayName = TryGetShellString(child.get(), SIGDN_NORMALDISPLAY)
                    .value_or(std::filesystem::path{ *parsingName }.filename().wstring());

                SFGAOF attributes = SFGAO_FOLDER;
                child->GetAttributes(SFGAO_FOLDER, &attributes);

                FileScoutNative::Domain::ShellItemRef item;
                item.path = std::filesystem::path{ *parsingName };
                item.parsing_name = *parsingName;
                item.display_name = std::move(displayName);
                item.is_folder = (attributes & SFGAO_FOLDER) != 0;
                item.kind_label = BuildKindLabel(item.path, item.is_folder);
                item.icon = GetIconForItem(item.path, item.is_folder);

                winrt::com_ptr<IShellItem2> child2;
                if (SUCCEEDED(child->QueryInterface(IID_PPV_ARGS(child2.put()))))
                {
                    if (auto kindLabel = TryGetShellPropertyString(child2.get(), PKEY_ItemTypeText);
                        kindLabel.has_value() && !kindLabel->empty())
                    {
                        item.kind_label = *kindLabel;
                    }

                    ULONGLONG sizeBytes{};
                    if (SUCCEEDED(child2->GetUInt64(PKEY_Size, &sizeBytes)))
                    {
                        item.size_available = true;
                        item.size_bytes = sizeBytes;
                    }

                    FILETIME modified{};
                    if (SUCCEEDED(child2->GetFileTime(PKEY_DateModified, &modified)))
                    {
                        item.date_modified_available = true;
                        item.date_modified = FileTimeToChrono(modified);
                        item.date_modified_str = FormatShellDateTime(modified).value_or(L"");
                    }
                }

                items.push_back(std::move(item));
            }
        }
        catch (...)
        {
            if (shouldUninitialize)
            {
                CoUninitialize();
            }
            throw;
        }

        if (shouldUninitialize)
        {
            CoUninitialize();
        }

        return items;
    }
}
