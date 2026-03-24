#pragma once
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace FileScoutNative::Domain
{
    // Returns the case-insensitive normalized key for a path, for use in SelectRecentPaths
    // excluded_keys sets and for building the shown-path exclusion set in BuildSidebarFavorites.
    [[nodiscard]] std::wstring SidebarPathKey(std::filesystem::path const& path);

    // Returns up to `max` unique recent paths from `candidates` (oldest-first order, i.e.
    // back_stack order) that do not appear in `excluded_keys`. Newest candidates are returned first.
    [[nodiscard]] std::vector<std::filesystem::path> SelectRecentPaths(
        std::vector<std::filesystem::path> const& candidates,
        std::unordered_set<std::wstring> const& excluded_keys,
        std::size_t max);
}
