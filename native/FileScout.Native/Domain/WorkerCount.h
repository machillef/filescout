#pragma once

#include <algorithm>
#include <cstddef>

namespace FileScoutNative::Domain
{
    // Number of parallel icon-extraction workers: hardware_concurrency capped at maxWorkers and at
    // the item count, and never 0 (hardware_concurrency may return 0, and callers divide by it).
    [[nodiscard]] constexpr std::size_t ComputeIconWorkerCount(
        std::size_t const hardwareConcurrency,
        std::size_t const itemCount,
        std::size_t const maxWorkers = 8) noexcept
    {
        auto const workers = (std::min)({ hardwareConcurrency, maxWorkers, itemCount });
        return (std::max)(workers, std::size_t{ 1 });
    }
}
