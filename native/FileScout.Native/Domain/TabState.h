#pragma once

#include "ShellItemRef.h"
#include "PaneSelectionState.h"

namespace FileScoutNative::Domain
{
    struct TabState
    {
        std::filesystem::path path;
        std::vector<ShellItemRef> items;
        PaneSelectionState selection;
        int32_t selected_item_index{ -1 };

        [[nodiscard]] std::wstring display_name() const
        {
            if (path.empty())
            {
                return L"New Tab";
            }
            auto const fn = path.filename().wstring();
            return fn.empty() ? path.wstring() : fn;
        }
    };
}
