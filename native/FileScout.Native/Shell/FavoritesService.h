#pragma once

#include "..\Domain\FavoriteEntry.h"

namespace FileScoutNative::Shell
{
    // Resolves shell known-folder paths and builds the sidebar favorites list.
    class FavoritesService
    {
    public:
        // Returns the full sidebar list: built-in places followed by user-pinned paths.
        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetSidebarFavorites() const;

        // Returns the standard pinned locations for the sidebar Places section.
        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetBuiltinFavorites() const;

        // Returns the user's home folder path (FOLDERID_Profile).
        [[nodiscard]] std::filesystem::path GetUserHomePath() const;

        // Adds a folder path to the persisted custom Places section.
        [[nodiscard]] bool AddPinnedFavorite(std::filesystem::path const& path) const;

        // Returns true when the path is already present in either the built-in or pinned sidebar list.
        [[nodiscard]] bool IsSidebarFavorite(std::filesystem::path const& path) const;

    private:
        [[nodiscard]] static std::filesystem::path ResolveKnownFolder(KNOWNFOLDERID const& id);
        [[nodiscard]] static std::filesystem::path GetPinnedFavoritesStoragePath();
        [[nodiscard]] static std::vector<std::filesystem::path> LoadPinnedFavoritePaths();
        static void SavePinnedFavoritePaths(std::vector<std::filesystem::path> const& paths);
    };
}
