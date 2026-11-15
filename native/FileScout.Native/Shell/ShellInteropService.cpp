#include "pch.h"
#include "Domain/ParallelFor.h"
#include "ShellInteropService.h"
#include "ComApartment.h"
#include "Domain/IconCacheKey.h"
#include "TraceHelper.h"

// PKEY_DateDeleted is not defined in propkey.h — define it manually.
// {EC28F96D-2F6B-4187-9565-17519122E2C1} 10
static const PROPERTYKEY PKEY_DateDeleted = {
    { 0xEC28F96D, 0x2F6B, 0x4187, { 0x95, 0x65, 0x17, 0x51, 0x91, 0x22, 0xE2, 0xC1 } },
    10
};

namespace FileScoutNative::Shell
{
    namespace
    {
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

        std::filesystem::path NormalizeShellLookupPath(std::filesystem::path const& path)
        {
            if (path.empty())
            {
                return {};
            }

            auto const native = path.native();
            if (native.rfind(L"\\\\", 0) == 0 || native.rfind(L"//", 0) == 0)
            {
                return path.lexically_normal();
            }

            std::error_code absoluteError;
            auto const absolute = std::filesystem::absolute(path, absoluteError);
            if (absoluteError)
            {
                return path.lexically_normal();
            }

            return absolute.lexically_normal();
        }

        std::optional<FileScoutNative::Domain::IconBitmap> ExtractShellIconBitmap(
            std::wstring const& shellLookupName,
            DWORD const fileAttributes,
            bool const useLiteralPath = false)
        {
            // Callers on background threads hold a ComApartment (see EnumerateDirectory and the
            // icon backfill workers); the UI thread is the app's STA.
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
                std::make_shared<std::vector<uint8_t> const>(std::move(pixels)),
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
        auto const cacheKey = ::FileScoutNative::Domain::BuildFileIconCacheKey(path, isFolder);
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
            if (::FileScoutNative::Domain::UsesLiteralIconLookup(path, true))
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
            if (::FileScoutNative::Domain::UsesLiteralIconLookup(path, false))
            {
                lookupName = NormalizeShellLookupPath(path).wstring();
                useLiteralPath = true;
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
        }

        // Extract icon outside the lock — GDI/SHGetFileInfoW can be slow.
        // For literal-path lookups (e.g. per-item .exe), SHGetFileInfoW without SHGFI_USEFILEATTRIBUTES
        // may fail on MTA background threads when shell extensions require an STA context. If the
        // literal-path extraction yields an empty bitmap, fall back to the extension-based lookup
        // (SHGFI_USEFILEATTRIBUTES) so the item still shows at least the generic type icon.
        auto icon = ExtractShellIconBitmap(lookupName, fileAttributes, useLiteralPath).value_or(FileScoutNative::Domain::IconBitmap{});
        if (icon.IsEmpty() && useLiteralPath && !isFolder)
        {
            auto const ext = path.extension().wstring();
            auto const fallbackName = ext.empty() ? std::wstring{ L"file" } : L"file" + ext;
            icon = ExtractShellIconBitmap(fallbackName, FILE_ATTRIBUTE_NORMAL, false)
                .value_or(FileScoutNative::Domain::IconBitmap{});
        }
        {
            std::lock_guard lock{ m_cacheMutex };
            m_iconCache.emplace(cacheKey, icon);
        }
        return icon;
    }

    // S6: BuildFastShellItemRef — all data from WIN32_FIND_DATAW, no per-item shell calls.
    // Icon: two-bucket fallback (folder or generic file); at most 2 SHGetFileInfoW calls total
    // per enumeration (both cached after the first call to each bucket).
    // Archive detection: extension-based heuristic; no CanEnumerateShellNamespace call.
    FileScoutNative::Domain::ShellItemRef ShellInteropService::BuildFastShellItemRef(
        WIN32_FIND_DATAW const& findData,
        std::filesystem::path const& dirPath,
        LocaleDateTimeFormatter& dateFormatter) const
    {
        FileScoutNative::Domain::ShellItemRef item;
        item.display_name = findData.cFileName;
        item.is_folder = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        item.path = dirPath / findData.cFileName;
        item.parsing_name = item.path.wstring();
        item.file_attributes = findData.dwFileAttributes;
        item.file_attributes_available = true;
        item.kind_label = BuildKindLabel(item.path, item.is_folder);

        // Two-bucket icon: synthetic paths route to the "__folder__" or "__file__" cache entry.
        item.icon = GetIconForItem(
            item.is_folder ? std::filesystem::path{ L"folder" } : std::filesystem::path{ L"file" },
            item.is_folder);

        // Size from WIN32_FIND_DATA (files only; folders report no size).
        if (!item.is_folder)
        {
            ULARGE_INTEGER size{};
            size.LowPart = findData.nFileSizeLow;
            size.HighPart = findData.nFileSizeHigh;
            item.size_bytes = size.QuadPart;
            item.size_available = true;
        }

        // Date modified from WIN32_FIND_DATA, formatted in the user's locale (cached per day/minute).
        if (findData.ftLastWriteTime.dwLowDateTime != 0 || findData.ftLastWriteTime.dwHighDateTime != 0)
        {
            item.date_modified = FileTimeToChrono(findData.ftLastWriteTime);
            item.date_modified_available = true;
            item.date_modified_str = dateFormatter.FormatFileTime(findData.ftLastWriteTime);
        }

        // Date created from WIN32_FIND_DATA — fast path (same approach).
        if (findData.ftCreationTime.dwLowDateTime != 0 || findData.ftCreationTime.dwHighDateTime != 0)
        {
            item.date_created_available = true;
            item.date_created_str = dateFormatter.FormatFileTime(findData.ftCreationTime);
        }

        // Archive detection: extension-based, no shell call.
        // is_supported_archive is a heuristic: .zip and .tar have confirmed Windows shell
        // namespace support. Other formats are not whitelisted — if the user navigates into one
        // and it turns out to be supported, the existing fallback path handles it.
        if (!item.is_folder)
        {
            if (auto const archiveKindLabel = TryGetArchiveKindLabel(item.path); archiveKindLabel.has_value())
            {
                item.is_archive_candidate = true;
                item.kind_label = *archiveKindLabel;
                auto ext = item.path.extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t const ch)
                    { return static_cast<wchar_t>(towlower(ch)); });
                item.is_supported_archive = (ext == L".zip" || ext == L".tar");
            }

            // Image detection by extension.
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
        std::filesystem::path const& path,
        bool const includeHidden)
    {
        // S6: Use FindFirstFileW/FindNextFileW instead of std::filesystem::directory_iterator.
        // WIN32_FIND_DATA already contains name, size, attributes, write time, and creation time —
        // no additional stat calls. BuildFastShellItemRef uses a two-bucket icon fallback and
        // extension-based archive detection, eliminating all per-item shell calls.
        ComApartment apartment; // SHGetFileInfoW for the two icon buckets, and the shell fallback.
        auto const searchPattern = (path / L"*").wstring();
        WIN32_FIND_DATAW findData{};
        // FindExInfoBasic skips the 8.3 short name and FIND_FIRST_EX_LARGE_FETCH asks for bigger
        // directory buffers per kernel call: fewer round trips on 100k-entry folders.
        HANDLE const findHandle = FindFirstFileExW(searchPattern.c_str(), FindExInfoBasic, &findData,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            if (CanEnumerateShellNamespace(path))
            {
                return EnumerateShellDirectory(path);
            }
            return {};
        }

        // Phase 1 (sequential, I/O): collect the raw records. Phase 2 (CPU): build the items in
        // parallel ranges; on win1 building 100k items took ~400 ms on one thread.
        std::vector<WIN32_FIND_DATAW> records;
        do
        {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            {
                continue;
            }

            if (!includeHidden && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            {
                continue;
            }

            records.push_back(findData);
        }
        while (FindNextFileW(findHandle, &findData));

        FindClose(findHandle);

        // Warm the two icon buckets once so the parallel builders only read the cache.
        (void)GetIconForItem(std::filesystem::path{ L"folder" }, true);
        (void)GetIconForItem(std::filesystem::path{ L"file" }, false);

        std::vector<FileScoutNative::Domain::ShellItemRef> items(records.size());
        ::FileScoutNative::Domain::ParallelForRanges(records.size(), [&](std::size_t begin, std::size_t end)
            {
                // The formatter caches per day/minute and is not thread-safe: one per range.
                LocaleDateTimeFormatter dateFormatter;
                for (auto i = begin; i < end; ++i)
                {
                    items[i] = BuildFastShellItemRef(records[i], path, dateFormatter);
                }
            });
        // NOTE: Sorting is intentionally omitted here — callers apply the user-selected
        // sort column and direction after enumeration.
        return items;
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateShellDirectory(
        std::filesystem::path const& path) const
    {
        auto const targetPath = NormalizeShellLookupPath(path);
        if (targetPath.empty())
        {
            throw std::runtime_error("Unsupported shell namespace path.");
        }

        LocaleDateTimeFormatter dateFormatter;
        // One apartment for the whole walk; COM objects below are released before it ends.
        ComApartment apartment;

        std::vector<FileScoutNative::Domain::ShellItemRef> items;
        {
            winrt::com_ptr<IShellItem> folderItem;
            winrt::check_hresult(SHCreateItemFromParsingName(
                targetPath.wstring().c_str(),
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
                        item.date_modified_str = dateFormatter.FormatFileTime(modified);
                    }
                }

                items.push_back(std::move(item));
            }
        }

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
            item.drive_type = driveType;
            item.is_folder = true;
            item.icon = GetIconForItem(drivePath, true);

            drives.push_back(std::move(item));
        }

        return drives;
    }

    std::vector<FileScoutNative::Domain::ShellItemRef> ShellInteropService::EnumerateRecycleBin() const
    {
        LocaleDateTimeFormatter dateFormatter;
        // One apartment for the whole walk; COM objects below are released before it ends.
        ComApartment apartment;

        std::vector<FileScoutNative::Domain::ShellItemRef> items;
        {
            winrt::com_ptr<IShellItem> recycleBinItem;
            winrt::check_hresult(SHCreateItemInKnownFolder(
                FOLDERID_RecycleBinFolder,
                0,
                nullptr,
                IID_PPV_ARGS(recycleBinItem.put())));

            winrt::com_ptr<IEnumShellItems> enumerator;
            winrt::check_hresult(recycleBinItem->BindToHandler(
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
                auto displayName = TryGetShellString(child.get(), SIGDN_NORMALDISPLAY)
                    .value_or(parsingName.has_value()
                        ? std::filesystem::path{ *parsingName }.filename().wstring()
                        : L"Unknown");

                SFGAOF attributes = SFGAO_FOLDER;
                child->GetAttributes(SFGAO_FOLDER, &attributes);

                FileScoutNative::Domain::ShellItemRef item;
                item.path = parsingName.has_value() ? std::filesystem::path{ *parsingName } : std::filesystem::path{};
                item.parsing_name = parsingName.value_or(L"");
                item.display_name = std::move(displayName);
                item.is_folder = (attributes & SFGAO_FOLDER) != 0;
                item.kind_label = item.is_folder ? L"Folder" : L"Deleted file";
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

                    FILETIME deletedTime{};
                    if (SUCCEEDED(child2->GetFileTime(PKEY_DateDeleted, &deletedTime)))
                    {
                        item.date_modified_available = true;
                        item.date_modified = FileTimeToChrono(deletedTime);
                        item.date_modified_str = dateFormatter.FormatFileTime(deletedTime);
                    }
                }

                items.push_back(std::move(item));
            }
        }

        return items;
    }

    FileScoutNative::Domain::IconBitmap ShellInteropService::GetItemIcon(
        std::filesystem::path const& path,
        bool const isFolder) const
    {
        if (!isFolder || path.empty())
        {
            return GetIconForItem(path, isFolder);
        }

        // Use the real path so SHGetFileInfoW returns the per-folder system icon
        // (e.g. the Desktop monitor icon, the Downloads arrow) rather than the
        // generic yellow folder that GetIconForItem returns for all non-drive-root
        // folders via SHGFI_USEFILEATTRIBUTES.  A path-keyed cache entry is used
        // so each folder keeps its own icon independently of the shared __folder__ slot.
        auto const cacheKey = L"__sidebar_folder__:" +
            ::FileScoutNative::Domain::LowercaseCopy(path.lexically_normal().wstring());
        std::optional<FileScoutNative::Domain::IconBitmap> cached;
        {
            std::lock_guard lock{ m_cacheMutex };
            if (auto const existing = m_iconCache.find(cacheKey); existing != m_iconCache.end())
                cached = existing->second;
        }
        if (cached.has_value())
        {
            TraceLog(L"[icon-cache] HIT: " + path.wstring());
            return *cached;
        }

        TraceLog(L"[icon-cache] MISS: " + path.wstring());
        auto icon = ExtractShellIconBitmap(path.wstring(), FILE_ATTRIBUTE_NORMAL, /*useLiteralPath=*/true)
            .value_or(FileScoutNative::Domain::IconBitmap{});
        if (icon.IsEmpty())
        {
            icon = GetIconForItem(path, /*isFolder=*/true);
        }

        {
            std::lock_guard lock{ m_cacheMutex };
            m_iconCache.emplace(cacheKey, icon);
        }
        return icon;
    }

    bool ShellInteropService::CanEnumerateShellNamespace(
        std::filesystem::path const& path) const
    {
        auto const normalized = NormalizeShellLookupPath(path);
        if (normalized.empty())
        {
            return false;
        }

        auto const cacheKey = normalized.wstring();
        {
            std::scoped_lock lock(m_archiveCapabilityMutex);
            if (auto const cached = m_archiveCapabilityCache.find(cacheKey);
                cached != m_archiveCapabilityCache.end())
            {
                return cached->second;
            }
        }

        bool supported = false;
        if (ComApartment apartment; apartment.IsUsable())
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
        return EnumerateShellDirectory(resolved->virtual_path);
    }
}
