#pragma once

#include <filesystem>

namespace FileScoutNative::Domain
{
    enum class DragStorageResolutionMode
    {
        Immediate,
        DeferredProvider,
    };

    inline DragStorageResolutionMode ResolveExternalDragStorageResolutionMode()
    {
        return DragStorageResolutionMode::DeferredProvider;
    }

    template <typename IsLocalDrivePathFn>
    bool ShouldStageExternalDragFile(
        std::filesystem::path const& path,
        bool const isFolder,
        IsLocalDrivePathFn&& isLocalDrivePathFn)
    {
        if (isFolder)
        {
            return false;
        }

        return !isLocalDrivePathFn(path);
    }
}
