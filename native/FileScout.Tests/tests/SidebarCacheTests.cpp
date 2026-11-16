#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/SidebarCache.h"

#include <string>
#include <vector>

using FileScoutNative::Domain::CachedValue;
using FileScoutNative::Domain::IsDriveListChange;

// UB-10: the sidebar is rebuilt on every navigation that records a recent folder. The drive list
// (with GetDiskFreeSpaceEx per drive) must be read once and reused, not re-read on each rebuild,
// or navigation stalls on slow and network drives.
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

// WM_DEVICECHANGE (0x0219) is broadcast to top-level windows when a volume arrives or leaves.
TEST(SidebarCacheTests, VolumeArrivalAndRemovalChangeTheDriveList)
{
    constexpr std::uint32_t wmDeviceChange = 0x0219;
    EXPECT_TRUE(IsDriveListChange(wmDeviceChange, 0x8000));   // DBT_DEVICEARRIVAL
    EXPECT_TRUE(IsDriveListChange(wmDeviceChange, 0x8004));   // DBT_DEVICEREMOVECOMPLETE
    EXPECT_FALSE(IsDriveListChange(wmDeviceChange, 0x0007));  // DBT_DEVNODES_CHANGED
    EXPECT_FALSE(IsDriveListChange(0x0010, 0x8000));          // WM_CLOSE
}
