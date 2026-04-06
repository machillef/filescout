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
            std::filesystem::path const& path,
            bool includeHidden = false);
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateShellDirectory(
            std::filesystem::path const& path) const;

        // Returns one ShellItemRef per attached drive root (e.g. C:\, D:\).
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateDrives();
        [[nodiscard]] FileScoutNative::Domain::IconBitmap GetItemIcon(
            std::filesystem::path const& path,
            bool isFolder) const;
        [[nodiscard]] bool CanEnumerateShellNamespace(std::filesystem::path const& path) const;
        [[nodiscard]] bool IsArchivePath(std::filesystem::path const& path) const;
        [[nodiscard]] std::optional<ResolvedArchivePath> TryResolveArchivePath(
            std::filesystem::path const& path) const;
        [[nodiscard]] std::vector<FileScoutNative::Domain::ShellItemRef> EnumerateArchiveDirectory(
            std::filesystem::path const& path) const;

    private:
        [[nodiscard]] FileScoutNative::Domain::ShellItemRef BuildShellItemRef(
            std::filesystem::directory_entry const& entry) const;
        // S6: fast path — builds a ShellItemRef from WIN32_FIND_DATAW without any shell calls.
        // Icon is set to a two-bucket fallback (folder or generic file).
        // S7 will backfill real per-extension icons asynchronously.
        [[nodiscard]] FileScoutNative::Domain::ShellItemRef BuildFastShellItemRef(
            WIN32_FIND_DATAW const& findData,
            std::filesystem::path const& dirPath) const;
        [[nodiscard]] FileScoutNative::Domain::IconBitmap GetIconForItem(
            std::filesystem::path const& path,
            bool isFolder) const;
        [[nodiscard]] bool IsSupportedArchiveFile(std::filesystem::path const& path) const;
        [[nodiscard]] std::optional<ResolvedArchivePath> ResolveArchivePath(
            std::filesystem::path const& path) const;

        mutable std::mutex m_cacheMutex;
        mutable std::unordered_map<std::wstring, FileScoutNative::Domain::IconBitmap> m_iconCache;
        mutable std::mutex m_archiveCapabilityMutex;
        mutable std::unordered_map<std::wstring, bool> m_archiveCapabilityCache;
    };
}
