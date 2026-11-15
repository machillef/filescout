#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/ParallelFor.h"

#include <atomic>
#include <stdexcept>

using FileScoutNative::Domain::ParallelForRanges;
using FileScoutNative::Domain::WorkerCount;

TEST(ParallelFor, WorkerCountIsNeverZero)
{
    EXPECT_GE(WorkerCount(), 1u);
    EXPECT_EQ(WorkerCount(1), 1u);
    EXPECT_GE(WorkerCount(0), 1u);
}

TEST(ParallelFor, CoversEveryIndexExactlyOnce)
{
    for (std::size_t const count : { 0u, 1u, 4095u, 4096u, 100'000u, 100'003u })
    {
        std::vector<std::atomic<int>> hits(count);
        ParallelForRanges(count, [&](std::size_t begin, std::size_t end)
            {
                for (auto i = begin; i < end; ++i) { hits[i].fetch_add(1); }
            }, 1024);
        for (std::size_t i = 0; i < count; ++i)
        {
            ASSERT_EQ(hits[i].load(), 1) << "count=" << count << " index=" << i;
        }
    }
}

TEST(ParallelFor, RethrowsAfterAllRangesFinish)
{
    std::atomic<std::size_t> done{ 0 };
    EXPECT_THROW(ParallelForRanges(50'000, [&](std::size_t begin, std::size_t end)
        {
            if (begin == 0) { throw std::runtime_error("boom"); }
            done.fetch_add(end - begin);
        }, 1000), std::runtime_error);
    EXPECT_GT(done.load(), 0u);
}
