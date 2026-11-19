#include "pch.h"
#include "SidebarRecents.h"
#include "TextFold.h"
#include <algorithm>
#include <cwctype>

namespace FileScoutNative::Domain
{
    std::wstring SidebarPathKey(std::filesystem::path const& path)
    {
        std::error_code ec;
        auto const absolute = std::filesystem::absolute(path, ec);
        return FoldCase((ec ? path : absolute).lexically_normal().wstring());
    }

    bool IsSameOrDescendantPathKey(std::wstring const& key, std::wstring const& rootKey)
    {
        if (rootKey.empty() || key.size() < rootKey.size() || key.compare(0, rootKey.size(), rootKey) != 0)
        {
            return false;
        }
        if (key.size() == rootKey.size() || rootKey.back() == L'\\' || rootKey.back() == L'/')
        {
            return true;
        }
        auto const next = key[rootKey.size()];
        return next == L'\\' || next == L'/';
    }

    std::vector<std::filesystem::path> SelectRecentPaths(
        std::vector<std::filesystem::path> const& candidates,
        std::unordered_set<std::wstring> const& excluded_keys,
        std::size_t max)
    {
        std::vector<std::filesystem::path> results;
        results.reserve(max);
        std::unordered_set<std::wstring> seen{ excluded_keys };

        // Iterate newest-first (end of back_stack = most recent).
        for (auto it = candidates.rbegin(); it != candidates.rend() && results.size() < max; ++it)
        {
            if (it->empty())
                continue;
            if (seen.insert(SidebarPathKey(*it)).second)
                results.push_back(*it);
        }
        return results;
    }

    std::vector<std::wstring> VisibleRecentPathKeys(
        std::vector<std::filesystem::path> const& candidates,
        std::unordered_set<std::wstring> const& excluded_keys,
        std::size_t const max)
    {
        auto const paths = SelectRecentPaths(candidates, excluded_keys, max);
        std::vector<std::wstring> keys;
        keys.reserve(paths.size());
        for (auto const& path : paths)
        {
            keys.push_back(SidebarPathKey(path));
        }
        return keys;
    }

    void RecordRecentFolder(
        std::vector<std::filesystem::path>& recents,
        std::filesystem::path const& folder,
        std::size_t const capacity)
    {
        if (folder.empty() || capacity == 0)
            return;

        auto const key = SidebarPathKey(folder);
        std::erase_if(recents, [&](std::filesystem::path const& entry) { return SidebarPathKey(entry) == key; });
        recents.push_back(folder);
        if (recents.size() > capacity)
            recents.erase(recents.begin(), recents.begin() + static_cast<std::ptrdiff_t>(recents.size() - capacity));
    }
}
