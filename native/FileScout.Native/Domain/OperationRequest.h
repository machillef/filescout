#pragma once

#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    enum class OperationKind
    {
        Open,
        OpenWith,
        Rename,
        Delete,
        Copy,
        Cut,
        Paste,
        Properties,
    };

    struct OperationRequest
    {
        OperationKind kind;
        std::vector<ShellItemRef> items;
        std::optional<std::filesystem::path> target_directory;
        std::optional<std::wstring> rename_to;
    };

    struct OperationResult
    {
        bool succeeded{ false };
        std::wstring message;
    };
}
