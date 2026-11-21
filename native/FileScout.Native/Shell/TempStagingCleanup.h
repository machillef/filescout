#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace FileScoutNative::Shell
{
    [[nodiscard]] std::filesystem::path DragStagingRoot(std::filesystem::path const& tempRoot);
    [[nodiscard]] std::filesystem::path ArchiveOpenRoot(std::filesystem::path const& tempRoot);

    std::size_t CleanupStaleStagingDirectories(
        std::filesystem::path const& tempRoot,
        std::filesystem::file_time_type now,
        std::chrono::hours maxAge = std::chrono::hours{ 24 });
}
