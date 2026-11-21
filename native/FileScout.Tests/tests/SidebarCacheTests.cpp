#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/SidebarCache.h"

#include <Dbt.h>

#include <string>
#include <vector>

using FileScoutNative::Domain::CachedValue;
using FileScoutNative::Domain::IsDriveListChange;

TEST(SidebarCacheTests, ValueIsLoadedOnceAcrossRebuilds)
{
    CachedValue<std::vector<int>> drives;
    int loads = 0;
    auto const load = [&] { ++loads; return std::vector<int>{ 1, 2 }; };

    EXPECT_EQ(drives.Get(load).size(), 2u);
    EXPECT_EQ(drives.Get(load).size(), 2u);
    EXPECT_EQ(drives.Get(load).size(), 2u);
    EXPECT_EQ(loads, 1);
}

TEST(SidebarCacheTests, InvalidateReloadsOnNextGet)
{
    CachedValue<int> value;
    int loads = 0;
    auto const load = [&] { return ++loads; };

    EXPECT_EQ(value.Get(load), 1);
    value.Invalidate();
    EXPECT_EQ(value.Get(load), 2);
    EXPECT_EQ(value.Get(load), 2);
}

TEST(SidebarCacheTests, VolumeArrivalAndRemovalChangeTheDriveList)
{
    EXPECT_TRUE(IsDriveListChange(WM_DEVICECHANGE, DBT_DEVICEARRIVAL));
    EXPECT_TRUE(IsDriveListChange(WM_DEVICECHANGE, DBT_DEVICEREMOVECOMPLETE));
    EXPECT_FALSE(IsDriveListChange(WM_DEVICECHANGE, DBT_DEVNODES_CHANGED));
    EXPECT_FALSE(IsDriveListChange(WM_CLOSE, DBT_DEVICEARRIVAL));
}
