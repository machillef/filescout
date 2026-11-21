#pragma once

#include <algorithm>
#include <cstddef>

namespace FileScoutNative::Domain
{
    // hardware_concurrency() may legally return 0.
    [[nodiscard]] constexpr std::size_t ComputeIconWorkerCount(
        std::size_t const hardwareConcurrency,
        std::size_t const itemCount,
        std::size_t const maxWorkers = 8) noexcept
    {
        auto const workers = (std::min)({ hardwareConcurrency, maxWorkers, itemCount });
        return (std::max)(workers, std::size_t{ 1 });
    }
}
