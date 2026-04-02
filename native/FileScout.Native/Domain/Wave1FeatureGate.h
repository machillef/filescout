#pragma once

#include <cstddef>
#include <string_view>

namespace FileScoutNative::Domain
{
    enum class Wave1Feature
    {
        CommandPalette,
        ArchiveBrowsing,
        BatchRename,
    };

    inline constexpr bool IsWave1FeatureEnabled(Wave1Feature const feature) noexcept
    {
        switch (feature)
        {
        case Wave1Feature::CommandPalette:
        case Wave1Feature::ArchiveBrowsing:
        case Wave1Feature::BatchRename:
            return false;
        }

        return false;
    }

    inline constexpr bool Wave1UsesSinglePaneFirstLaunch() noexcept
    {
        return true;
    }

    inline constexpr bool ShouldShowWave1ContextCommand(
        std::wstring_view const commandId,
        std::size_t const selectionCount,
        bool const isArchiveView) noexcept
    {
        if (!IsWave1FeatureEnabled(Wave1Feature::BatchRename) &&
            commandId == L"rename" &&
            selectionCount > 1)
        {
            return false;
        }

        if (!IsWave1FeatureEnabled(Wave1Feature::ArchiveBrowsing) &&
            (commandId == L"extractToArchiveFolder" ||
             commandId == L"extractReveal" ||
             (isArchiveView && commandId == L"open")))
        {
            return false;
        }

        return true;
    }

    inline constexpr std::wstring_view Wave1FeatureUnavailableStatus(
        Wave1Feature const feature) noexcept
    {
        switch (feature)
        {
        case Wave1Feature::CommandPalette:
            return L"Command palette is not part of the closed beta yet.";
        case Wave1Feature::ArchiveBrowsing:
            return L"Archive browsing is not part of the closed beta yet.";
        case Wave1Feature::BatchRename:
            return L"Batch rename is not part of the closed beta yet.";
        }

        return L"That feature is not part of the closed beta yet.";
    }
}
