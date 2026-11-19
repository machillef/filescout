#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "SidebarRecents.h"

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

    // lexically_normal() keeps a trailing separator ("C:\\Users\\Docs\\"), which made a reload of the
    // same folder look like a navigation. Drop it, except for a root ("C:\\").
    [[nodiscard]] inline std::filesystem::path NormalizeFolderForCompare(std::filesystem::path const& path)
    {
        auto normal = path.lexically_normal();
        if (!normal.has_filename() && normal != normal.root_path())
        {
            normal = normal.parent_path();
        }
        return normal;
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
            PathsEqualIgnoringCase(NormalizeFolderForCompare(current), NormalizeFolderForCompare(target)))
        {
            return;
        }

        backStack.push_back(current);
        forwardStack.clear();
    }

    // Starts a navigation: records history (unless it is a Back/Forward step) and makes `target` the
    // pane's current folder right away. A second navigation started before the first one's listing
    // arrives then records the pending target (e.g. the Recycle Bin), not the folder left earlier.
    inline void BeginNavigation(
        std::vector<std::filesystem::path>& backStack,
        std::vector<std::filesystem::path>& forwardStack,
        std::filesystem::path& current,
        std::filesystem::path const& target,
        bool const isHistoryStep)
    {
        if (!isHistoryStep)
        {
            RecordNavigation(backStack, forwardStack, current, target);
        }
        current = target;
    }

    // The folder a navigation from `leaving` to `target` adds to the recents: the folder actually
    // left (Back/Forward included), never the target, nothing for a reload of the same folder or a
    // first load, and never the Recycle Bin (it has its own sidebar row).
    [[nodiscard]] inline std::optional<std::filesystem::path> FolderToRecordOnLeave(
        std::filesystem::path const& leaving,
        std::filesystem::path const& target)
    {
        if (leaving.empty() || IsRecycleBinLocation(leaving) ||
            PathsEqualIgnoringCase(NormalizeFolderForCompare(leaving), NormalizeFolderForCompare(target)))
        {
            return std::nullopt;
        }
        return leaving;
    }

    // The whole navigation step LoadDirectory performs on history and recents: capture the folder
    // being left, start the navigation (history + pending current folder), then add the folder left
    // to the recents. Returns true when the recents changed.
    inline bool NavigateAndRecordRecent(
        std::vector<std::filesystem::path>& backStack,
        std::vector<std::filesystem::path>& forwardStack,
        std::filesystem::path& current,
        std::vector<std::filesystem::path>& recents,
        std::filesystem::path const& target,
        bool const isHistoryStep,
        std::size_t const recentCapacity)
    {
        auto const leaving = current;
        BeginNavigation(backStack, forwardStack, current, target, isHistoryStep);
        if (auto const left = FolderToRecordOnLeave(leaving, target))
        {
            RecordRecentFolder(recents, *left, recentCapacity);
            return true;
        }
        return false;
    }

    // Two-batch directory publication policy: small and medium folders publish in one ReplaceAll
    // without waiting for two extra CompositionTarget::Rendering frames; very large folders
    // (e.g. 100k items) publish the first batch immediately and stage the full list after present.
    inline constexpr std::size_t kStagedLoadFirstBatch = 150;
    inline constexpr std::size_t kStagedLoadThreshold = 2000;

    [[nodiscard]] inline bool ShouldStageDirectoryLoad(std::size_t const totalItems) noexcept
    {
        return totalItems > kStagedLoadThreshold;
    }
}
