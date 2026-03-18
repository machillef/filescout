#pragma once

#include "..\Domain\FavoriteEntry.h"

namespace FileScoutNative::Shell
{
    // Resolves shell known-folder paths and builds the sidebar favorites list.
    class FavoritesService
    {
    public:
        // Returns the standard pinned locations for the sidebar Places section.
        [[nodiscard]] std::vector<FileScoutNative::Domain::FavoriteEntry> GetBuiltinFavorites() const;

        // Returns the user's home folder path (FOLDERID_Profile).
        [[nodiscard]] std::filesystem::path GetUserHomePath() const;

    private:
        [[nodiscard]] static std::filesystem::path ResolveKnownFolder(KNOWNFOLDERID const& id);
    };
}
