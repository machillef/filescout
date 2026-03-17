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
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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

            items.push_back(std::move(item));
        }

        std::sort(items.begin(), items.end(), [](auto const& left, auto const& right)
            {
                if (left.is_folder != right.is_folder)
                {
                    return left.is_folder > right.is_folder;
                }

                return _wcsicmp(left.display_name.c_str(), right.display_name.c_str()) < 0;
            });

        return items;
    }
}
