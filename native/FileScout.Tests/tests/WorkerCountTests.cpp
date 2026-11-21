#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/WorkerCount.h"

using FileScoutNative::Domain::ComputeIconWorkerCount;

TEST(WorkerCountTests, UnknownHardwareConcurrencyStillGivesOneWorker)
{
    EXPECT_EQ(ComputeIconWorkerCount(0, 1000), 1u);
}

TEST(WorkerCountTests, CapsAtEightWorkers)
{
    EXPECT_EQ(ComputeIconWorkerCount(32, 1000), 8u);
    EXPECT_EQ(ComputeIconWorkerCount(4, 1000), 4u);
}

TEST(WorkerCountTests, NeverMoreWorkersThanItems)
{
    EXPECT_EQ(ComputeIconWorkerCount(16, 3), 3u);
    EXPECT_EQ(ComputeIconWorkerCount(16, 0), 1u);
}
