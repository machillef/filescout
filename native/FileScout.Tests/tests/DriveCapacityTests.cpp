#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/DriveCapacity.h"

using namespace FileScoutNative::Domain;

TEST(DriveCapacity, ZeroTotal_ReturnsZero)
{
    EXPECT_DOUBLE_EQ(DriveUsedRatio(0, 0), 0.0);
}

TEST(DriveCapacity, FullDrive_ReturnsOne)
{
    constexpr std::uintmax_t total = 100ull * 1024 * 1024 * 1024;
    EXPECT_DOUBLE_EQ(DriveUsedRatio(total, 0), 1.0);
}

TEST(DriveCapacity, EmptyDrive_ReturnsZero)
{
    constexpr std::uintmax_t total = 500ull * 1024 * 1024 * 1024;
    EXPECT_DOUBLE_EQ(DriveUsedRatio(total, total), 0.0);
}

TEST(DriveCapacity, HalfFull_ReturnsHalf)
{
    constexpr std::uintmax_t total = 200ull * 1024 * 1024 * 1024;
    constexpr std::uintmax_t free = 100ull * 1024 * 1024 * 1024;
    EXPECT_NEAR(DriveUsedRatio(total, free), 0.5, 1e-9);
}

TEST(DriveCapacity, QuarterFree_ReturnsThreeQuarters)
{
    constexpr std::uintmax_t total = 400ull * 1024 * 1024 * 1024;
    constexpr std::uintmax_t free  = 100ull * 1024 * 1024 * 1024;
    EXPECT_NEAR(DriveUsedRatio(total, free), 0.75, 1e-9);
}

TEST(DriveCapacity, FreeExceedsTotal_ClampsToZero)
{
    constexpr std::uintmax_t total = 100ull * 1024 * 1024 * 1024;
    constexpr std::uintmax_t free  = 200ull * 1024 * 1024 * 1024;
    EXPECT_DOUBLE_EQ(DriveUsedRatio(total, free), 0.0);
}
