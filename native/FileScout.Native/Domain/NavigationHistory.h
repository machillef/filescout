#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace FileScoutNative::Domain
{
    // The location the sidebar's Recycle Bin row navigates to. std::filesystem::absolute() treats it
    // as a relative path and prefixes the working directory, so recognise it by its last component.
    inline constexpr std::wstring_view kRecycleBinLocation = L"shell:RecycleBinFolder";

    [[nodiscard]] inline bool PathsEqualIgnoringCase(
        std::filesystem::path const& left,
        std::filesystem::path const& right)
    {
        auto const& a = left.native();
        auto const& b = right.native();
        return ::CompareStringOrdinal(
                   a.c_str(), static_cast<int>(a.size()),
                   b.c_str(), static_cast<int>(b.size()),
                   TRUE) == CSTR_EQUAL;
    }

    [[nodiscard]] inline bool IsRecycleBinLocation(std::filesystem::path const& path)
    {
        return !path.empty() && PathsEqualIgnoringCase(path.filename(), std::filesystem::path{ kRecycleBinLocation });
    }

    // History rule for a user navigation from `current` to `target`: the folder being left goes onto
    // the back stack and the forward stack is dropped. Nothing is recorded when there is no current
    // folder (first load, drives view) or when the target is the current folder (refresh, reload
    // after rename/create/delete), so Back always leaves the folder.
    inline void RecordNavigation(
        std::vector<std::filesystem::path>& backStack,
        std::vector<std::filesystem::path>& forwardStack,
        std::filesystem::path const& current,
        std::filesystem::path const& target)
    {
        if (current.empty() ||
            PathsEqualIgnoringCase(current.lexically_normal(), target.lexically_normal()))
        {
            return;
        }

        backStack.push_back(current);
        forwardStack.clear();
    }
}
