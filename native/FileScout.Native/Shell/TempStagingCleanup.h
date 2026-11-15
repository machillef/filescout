#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace FileScoutNative::Shell
{
    // %TEMP%\FileScout\DragStage\<token>: copies made for drags to external targets.
    [[nodiscard]] std::filesystem::path DragStagingRoot(std::filesystem::path const& tempRoot);
    // %TEMP%\FileScoutNative\ArchiveOpen\<archive>\<session>: files extracted to open from archives.
    [[nodiscard]] std::filesystem::path ArchiveOpenRoot(std::filesystem::path const& tempRoot);

    // Deletes staging directories under the two roots above whose last write is older than maxAge,
    // and per-archive folders left empty. Nothing outside those roots is touched. Errors are
    // ignored (a file still in use stays until a later run). Returns the number of staging
    // directories deleted.
    std::size_t CleanupStaleStagingDirectories(
        std::filesystem::path const& tempRoot,
        std::filesystem::file_time_type now,
        std::chrono::hours maxAge = std::chrono::hours{ 24 });
}
