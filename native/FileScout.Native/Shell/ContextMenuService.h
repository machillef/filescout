#pragma once

#include "..\Domain\ContextMenuModel.h"
#include "..\Domain\ShellItemRef.h"

namespace FileScoutNative::Shell
{
    struct ContextMenuScenario
    {
        std::optional<FileScoutNative::Domain::ShellItemRef> focused_item;
        bool can_paste{ false };
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
                FileScoutNative::Domain::ExplorerCommandSource::App);

    private:
    };
}
