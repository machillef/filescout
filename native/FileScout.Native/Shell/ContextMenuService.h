#pragma once

#include "..\Domain\ContextMenuModel.h"
#include "..\Domain\ShellItemRef.h"

namespace FileScoutNative::Shell
{
    struct ContextMenuScenario
    {
        std::optional<FileScoutNative::Domain::ShellItemRef> focused_item;
        uint32_t selection_count{ 0 };
        bool can_paste{ false };
        bool can_pin_to_places{ false };
        bool is_read_only_archive_view{ false };
        bool is_recycle_bin_view{ false };
    };

    class ContextMenuService
    {
    public:
        [[nodiscard]] FileScoutNative::Domain::ContextMenuModel BuildMenu(
            ContextMenuScenario const& scenario) const;
        [[nodiscard]] static FileScoutNative::Domain::ExplorerCommand MakeCommand(
            std::wstring id,
            std::wstring label,
            std::wstring iconKey = {},
            std::wstring shortcutText = {},
            bool available = true,
            FileScoutNative::Domain::ExplorerCommandSource source =
                FileScoutNative::Domain::ExplorerCommandSource::App,
            bool isDestructive = false);

    private:
    };
}
