#pragma once

#include <algorithm>
#include <cstddef>
#include <exception>
#include <future>
#include <thread>
#include <vector>

namespace FileScoutNative::Domain
{
    // hardware_concurrency() may legally return 0.
    [[nodiscard]] inline std::size_t WorkerCount(std::size_t const cap = 8) noexcept
    {
        auto const hw = static_cast<std::size_t>(std::thread::hardware_concurrency());
        return (std::clamp)(hw, std::size_t{ 1 }, (std::max)(cap, std::size_t{ 1 }));
    }

    template <typename Body>
    void ParallelForRanges(std::size_t const count, Body&& body, std::size_t const minPerWorker = 4096)
    {
        if (count == 0)
        {
            return;
        }
        auto const workers = (std::min)(WorkerCount(), (count + minPerWorker - 1) / minPerWorker);
        if (workers <= 1)
        {
            body(std::size_t{ 0 }, count);
            return;
        }
        auto const chunk = (count + workers - 1) / workers;
        std::vector<std::future<void>> others;
        others.reserve(workers - 1);
        for (std::size_t w = 1; w < workers; ++w)
        {
            auto const begin = w * chunk;
            auto const end = (std::min)(begin + chunk, count);
            if (begin >= end)
            {
                break;
            }
            others.push_back(std::async(std::launch::async, [&body, begin, end] { body(begin, end); }));
        }
        std::exception_ptr failure;
        try
        {
            body(std::size_t{ 0 }, (std::min)(chunk, count));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        for (auto& other : others)
        {
            try { other.get(); }
            catch (...) { if (!failure) { failure = std::current_exception(); } }
        }
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
}
