#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ContextMenuAnchor.h"

using namespace FileScoutNative::Domain;

namespace
{
    constexpr ScreenRect kWindow{ 100, 100, 1100, 800 };
}

TEST(ContextMenuAnchor, ClickPoint_WinsWhenPresent)
{
    auto const anchor = ChooseContextMenuAnchor(ScreenPoint{ 640, 420 }, ScreenRect{ 120, 300, 900, 332 }, kWindow);
    EXPECT_EQ(anchor.x, 640);
    EXPECT_EQ(anchor.y, 420);
}

TEST(ContextMenuAnchor, NoClickPoint_AnchorsAtTheFocusedRow)
{
    auto const anchor = ChooseContextMenuAnchor(std::nullopt, ScreenRect{ 120, 300, 900, 332 }, kWindow);
    EXPECT_EQ(anchor.x, 120 + kRowAnchorInset);
    EXPECT_EQ(anchor.y, 332);
}

TEST(ContextMenuAnchor, RowScrolledOutOfView_IsClampedIntoTheWindow)
{
    auto const anchor = ChooseContextMenuAnchor(std::nullopt, ScreenRect{ 120, 900, 900, 932 }, kWindow);
    EXPECT_EQ(anchor.x, 120 + kRowAnchorInset);
    EXPECT_EQ(anchor.y, 800);
}

TEST(ContextMenuAnchor, NoClickAndNoRow_UsesTheWindowCentre)
{
    auto const anchor = ChooseContextMenuAnchor(std::nullopt, std::nullopt, kWindow);
    EXPECT_EQ(anchor.x, 600);
    EXPECT_EQ(anchor.y, 450);
}

TEST(ContextMenuAnchor, EmptyRowRect_IsIgnored)
{
    auto const anchor = ChooseContextMenuAnchor(std::nullopt, ScreenRect{ 0, 0, 0, 0 }, kWindow);
    EXPECT_EQ(anchor.x, 600);
    EXPECT_EQ(anchor.y, 450);
}
