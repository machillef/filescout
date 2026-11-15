#include "pch.h"
#include "Shell/TempStagingCleanup.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        namespace fs = std::filesystem;

        bool IsStale(fs::path const& dir, fs::file_time_type const now, std::chrono::hours const maxAge)
        {
            std::error_code ec;
            auto const written = fs::last_write_time(dir, ec);
            return !ec && now - written > maxAge;
        }

        // Deletes the stale subdirectories of parent; returns how many were deleted.
        std::size_t DeleteStaleChildren(
            fs::path const& parent,
            fs::file_time_type const now,
            std::chrono::hours const maxAge)
        {
            std::size_t deleted = 0;
            std::error_code ec;
            for (fs::directory_iterator it{ parent, ec }, end; !ec && it != end; it.increment(ec))
            {
                std::error_code typeError;
                if (!it->is_directory(typeError) || typeError || !IsStale(it->path(), now, maxAge))
                {
                    continue;
                }

                std::error_code removeError;
                fs::remove_all(it->path(), removeError);
                if (!removeError)
                {
                    ++deleted;
                }
            }

            return deleted;
        }
    }

    fs::path DragStagingRoot(fs::path const& tempRoot)
    {
        return tempRoot / L"FileScout" / L"DragStage";
    }

    fs::path ArchiveOpenRoot(fs::path const& tempRoot)
    {
        return tempRoot / L"FileScoutNative" / L"ArchiveOpen";
    }

    std::size_t CleanupStaleStagingDirectories(
        fs::path const& tempRoot,
        fs::file_time_type const now,
        std::chrono::hours const maxAge)
    {
        auto deleted = DeleteStaleChildren(DragStagingRoot(tempRoot), now, maxAge);

        std::error_code ec;
        for (fs::directory_iterator it{ ArchiveOpenRoot(tempRoot), ec }, end; !ec && it != end; it.increment(ec))
        {
            std::error_code typeError;
            if (!it->is_directory(typeError) || typeError)
            {
                continue;
            }

            deleted += DeleteStaleChildren(it->path(), now, maxAge);

            std::error_code emptyError;
            if (fs::is_empty(it->path(), emptyError) && !emptyError)
            {
                std::error_code removeError;
                fs::remove(it->path(), removeError);
            }
        }

        return deleted;
    }
}
