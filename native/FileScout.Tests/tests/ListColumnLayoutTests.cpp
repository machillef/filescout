#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ListColumnLayout.h"
#include <limits>

using namespace FileScoutNative::Domain;

TEST(ListColumnLayout, ResizeListColumn_AppliesDeltaAndClampsToMinAndMax)
{
    ListColumnWidths const initial{};

    auto const wider = ResizeListColumn(initial, ListColumnIndex::Name, 80.0);
    EXPECT_DOUBLE_EQ(wider.name, ListColumnWidths::kDefaultName + 80.0);
    EXPECT_DOUBLE_EQ(wider.type, initial.type);
    EXPECT_DOUBLE_EQ(wider.size, initial.size);
    EXPECT_DOUBLE_EQ(wider.date, initial.date);

    auto const tooNarrow = ResizeListColumn(initial, ListColumnIndex::Size, -500.0);
    EXPECT_DOUBLE_EQ(tooNarrow.size, ListColumnWidths::kMinSize);

    auto const tooWide = ResizeListColumn(initial, ListColumnIndex::DateModified, 5000.0);
    EXPECT_DOUBLE_EQ(tooWide.date, ListColumnWidths::kMaxWidth);
}

TEST(ListColumnLayout, ResizeListColumn_IgnoresNonFiniteDelta)
{
    ListColumnWidths const initial{};
    auto const nanRes = ResizeListColumn(initial, ListColumnIndex::Type, std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(nanRes.type, initial.type);
    auto const infRes = ResizeListColumn(initial, ListColumnIndex::Type, std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(infRes.type, initial.type);
}

TEST(ListColumnLayout, ResetListColumn_RestoresOnlyTargetedColumnDefault)
{
    auto widths = ResizeListColumn(ListColumnWidths{}, ListColumnIndex::Name, 120.0);
    widths = ResizeListColumn(widths, ListColumnIndex::Type, 50.0);

    auto const resetName = ResetListColumn(widths, ListColumnIndex::Name);
    EXPECT_DOUBLE_EQ(resetName.name, ListColumnWidths::kDefaultName);
    EXPECT_DOUBLE_EQ(resetName.type, ListColumnWidths::kDefaultType + 50.0);
}
