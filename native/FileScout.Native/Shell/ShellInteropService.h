#pragma once

#include "..\Domain\ShellItemRef.h"

namespace FileScoutNative::Shell
{
    class ShellInteropService
    {
    public:
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateDirectory(
            std::filesystem::path const& path);

        // Returns one ShellItemRef per attached drive root (e.g. C:\, D:\).
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateDrives();

    private:
        [[nodiscard]] FileScoutNative::Domain::IconBitmap GetIconForItem(
            std::filesystem::path const& path,
            bool isFolder);

        std::unordered_map<std::wstring, FileScoutNative::Domain::IconBitmap> m_iconCache;
    };
}
