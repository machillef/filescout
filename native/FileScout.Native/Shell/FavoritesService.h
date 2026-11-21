#pragma once

#include "..\Domain\FavoriteEntry.h"

namespace FileScoutNative::Shell
{
    class FavoritesService
    {
    public:
        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetSidebarFavorites() const;

        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetBuiltinFavorites() const;

        [[nodiscard]] std::filesystem::path GetUserHomePath() const;

        [[nodiscard]] bool AddPinnedFavorite(std::filesystem::path const& path) const;

        [[nodiscard]] bool RemovePinnedFavorite(std::filesystem::path const& path) const;

        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetPinnedFavorites() const;

        [[nodiscard]] bool IsSidebarFavorite(std::filesystem::path const& path) const;

    private:
        [[nodiscard]] static std::filesystem::path ResolveKnownFolder(KNOWNFOLDERID const& id);
        [[nodiscard]] static std::filesystem::path GetPinnedFavoritesStoragePath();
        [[nodiscard]] static std::vector<std::filesystem::path> LoadPinnedFavoritePaths();
        static void SavePinnedFavoritePaths(std::vector<std::filesystem::path> const& paths);
    };
}
