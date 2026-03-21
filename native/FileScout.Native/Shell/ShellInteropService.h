#pragma once

#include "..\Domain\ShellItemRef.h"

namespace FileScoutNative::Shell
{
    struct ResolvedArchivePath
    {
        std::filesystem::path archive_file_path;
        std::filesystem::path virtual_path;
    };

    class ShellInteropService
    {
    public:
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateDirectory(
            std::filesystem::path const& path);

        // Returns one ShellItemRef per attached drive root (e.g. C:\, D:\).
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateDrives();
        [[nodiscard]] FileScoutNative::Domain::IconBitmap GetItemIcon(
            std::filesystem::path const& path,
            bool isFolder) const;
        [[nodiscard]] bool IsArchivePath(std::filesystem::path const& path) const;
        [[nodiscard]] std::optional<ResolvedArchivePath> TryResolveArchivePath(
            std::filesystem::path const& path) const;
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateArchiveDirectory(
            std::filesystem::path const& path) const;

    private:
        [[nodiscard]] FileScoutNative::Domain::IconBitmap GetIconForItem(
            std::filesystem::path const& path,
            bool isFolder) const;
        [[nodiscard]] bool IsSupportedArchiveFile(std::filesystem::path const& path) const;
        [[nodiscard]] bool CanEnumerateShellNamespace(std::filesystem::path const& path) const;
        [[nodiscard]] std::optional<ResolvedArchivePath> ResolveArchivePath(
            std::filesystem::path const& path) const;

        mutable std::mutex m_cacheMutex;
        mutable std::unordered_map<std::wstring, FileScoutNative::Domain::IconBitmap> m_iconCache;
        mutable std::mutex m_archiveCapabilityMutex;
        mutable std::unordered_map<std::wstring, bool> m_archiveCapabilityCache;
    };
}
