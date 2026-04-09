#include "pch.h"
#include <gtest/gtest.h>

namespace FileScoutNative::detail
{
    std::optional<std::filesystem::path> ResolveLaunchPathArgument(std::span<wchar_t const* const> args);
}

TEST(StartupArgumentParsing, LeadingDashPathIsAcceptedAsLaunchPath)
{
    constexpr wchar_t const* args[]{ L"-reports" };

    auto const launchPath = FileScoutNative::detail::ResolveLaunchPathArgument(args);

    ASSERT_TRUE(launchPath.has_value());
    EXPECT_EQ(launchPath->wstring(), L"-reports");
}
