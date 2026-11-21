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
    inline constexpr std::wstring_view kDrivesViewLocation = L"shell:MyComputerFolder";

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

    [[nodiscard]] inline bool IsDrivesViewLocation(std::filesystem::path const& path)
    {
        return !path.empty() && PathsEqualIgnoringCase(path.filename(), std::filesystem::path{ kDrivesViewLocation });
    }

    // lexically_normal() keeps a trailing separator ("C:\\Users\\Docs\\"); drop it, except for a root ("C:\\").
    [[nodiscard]] inline std::filesystem::path NormalizeFolderForCompare(std::filesystem::path const& path)
    {
        auto normal = path.lexically_normal();
        if (!normal.has_filename() && normal != normal.root_path())
        {
            normal = normal.parent_path();
        }
        return normal;
    }

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

    [[nodiscard]] inline std::optional<std::filesystem::path> FolderToRecordOnLeave(
        std::filesystem::path const& leaving,
        std::filesystem::path const& target)
    {
        if (leaving.empty() || IsRecycleBinLocation(leaving) || IsDrivesViewLocation(leaving) ||
            PathsEqualIgnoringCase(NormalizeFolderForCompare(leaving), NormalizeFolderForCompare(target)))
        {
            return std::nullopt;
        }
        return leaving;
    }

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

    enum class ParentNavigationKind
    {
        None,
        Folder,
        DrivesView,
    };

    struct ParentNavigationTarget
    {
        ParentNavigationKind kind{ ParentNavigationKind::None };
        std::filesystem::path path;
    };

    [[nodiscard]] inline ParentNavigationTarget ResolveParentNavigationTarget(
        std::filesystem::path const& currentPath,
        bool const isDrivesView,
        bool const isRecycleBinView)
    {
        if (isDrivesView || currentPath.empty())
        {
            return {};
        }

        if (isRecycleBinView || IsRecycleBinLocation(currentPath))
        {
            return { ParentNavigationKind::DrivesView, {} };
        }

        auto const normalized = NormalizeFolderForCompare(currentPath);
        auto const parent = normalized.parent_path();
        if (parent.empty() || PathsEqualIgnoringCase(normalized, parent))
        {
            return { ParentNavigationKind::DrivesView, {} };
        }

        return { ParentNavigationKind::Folder, parent };
    }

    inline constexpr std::size_t kStagedLoadFirstBatch = 150;
    inline constexpr std::size_t kStagedLoadThreshold = 2000;

    [[nodiscard]] inline bool ShouldStageDirectoryLoad(std::size_t const totalItems) noexcept
    {
        return totalItems > kStagedLoadThreshold;
    }
}
