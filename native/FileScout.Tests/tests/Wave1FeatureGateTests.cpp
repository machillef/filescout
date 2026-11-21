#include "pch.h"

#include <gtest/gtest.h>

#include "Domain\Wave1FeatureGate.h"

#include <string>

namespace
{
    using FileScoutNative::Domain::IsWave1FeatureEnabled;
    using FileScoutNative::Domain::ShouldShowWave1ContextCommand;
    using FileScoutNative::Domain::Wave1Feature;
    using FileScoutNative::Domain::Wave1FeatureUnavailableStatus;
    using FileScoutNative::Domain::Wave1UsesSinglePaneFirstLaunch;
}

TEST(Wave1FeatureGate, FirstLaunchDefaultsToSinglePane)
{
    EXPECT_TRUE(Wave1UsesSinglePaneFirstLaunch());
}

TEST(Wave1FeatureGate, CommandPaletteIsEnabledForClosedBeta)
{
    EXPECT_TRUE(IsWave1FeatureEnabled(Wave1Feature::CommandPalette));
    EXPECT_FALSE(IsWave1FeatureEnabled(Wave1Feature::ArchiveBrowsing));
    EXPECT_FALSE(IsWave1FeatureEnabled(Wave1Feature::BatchRename));
}

TEST(Wave1FeatureGate, MultiSelectRenameIsHiddenFromWave1ContextMenus)
{
    EXPECT_FALSE(ShouldShowWave1ContextCommand(L"rename", 2, false));
    EXPECT_TRUE(ShouldShowWave1ContextCommand(L"rename", 1, false));
}

TEST(Wave1FeatureGate, ArchiveOnlyCommandsAreHiddenWhileArchiveBrowsingIsDisabled)
{
    EXPECT_FALSE(ShouldShowWave1ContextCommand(L"extractToArchiveFolder", 1, true));
    EXPECT_FALSE(ShouldShowWave1ContextCommand(L"extractReveal", 1, true));
    EXPECT_FALSE(ShouldShowWave1ContextCommand(L"open", 1, true));
    EXPECT_TRUE(ShouldShowWave1ContextCommand(L"open", 1, false));
}

TEST(Wave1FeatureGate, StatusMessagesExplainWhyAFeatureIsUnavailable)
{
    EXPECT_EQ(
        std::wstring{ L"Command palette is not available yet." },
        std::wstring{ Wave1FeatureUnavailableStatus(Wave1Feature::CommandPalette) });
    EXPECT_EQ(
        std::wstring{ L"Archive browsing is not available yet." },
        std::wstring{ Wave1FeatureUnavailableStatus(Wave1Feature::ArchiveBrowsing) });
    EXPECT_EQ(
        std::wstring{ L"Batch rename is not available yet." },
        std::wstring{ Wave1FeatureUnavailableStatus(Wave1Feature::BatchRename) });
}

TEST(Wave1FeatureGate, UnknownFeaturesUseClosedBetaFallbackStatus)
{
    EXPECT_EQ(
        std::wstring{ L"That feature is not available yet." },
        std::wstring{ Wave1FeatureUnavailableStatus(static_cast<Wave1Feature>(999)) });
}
