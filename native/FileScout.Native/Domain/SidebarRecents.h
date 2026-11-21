#pragma once
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace FileScoutNative::Domain
{
    [[nodiscard]] std::wstring SidebarPathKey(std::filesystem::path const& path);

    [[nodiscard]] bool IsSameOrDescendantPathKey(std::wstring const& key, std::wstring const& rootKey);

    [[nodiscard]] std::vector<std::filesystem::path> SelectRecentPaths(
        std::vector<std::filesystem::path> const& candidates,
        std::unordered_set<std::wstring> const& excluded_keys,
        std::size_t max);

    [[nodiscard]] std::vector<std::wstring> VisibleRecentPathKeys(
        std::vector<std::filesystem::path> const& candidates,
        std::unordered_set<std::wstring> const& excluded_keys,
        std::size_t max);

    void RecordRecentFolder(
        std::vector<std::filesystem::path>& recents,
        std::filesystem::path const& folder,
        std::size_t capacity);
}
