#pragma once

#include "ShellItemRef.h"

namespace FileScoutNative::Domain
{
    class ClipboardService
    {
    public:
        virtual ~ClipboardService() = default;

        virtual void CopyShellItems(std::span<ShellItemRef const> items) = 0;
        virtual void CutShellItems(std::span<ShellItemRef const> items) = 0;
        virtual bool CanPasteInto(std::filesystem::path const& directory) const = 0;
    };
}
