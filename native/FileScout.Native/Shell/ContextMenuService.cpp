#include "pch.h"
#include "ContextMenuService.h"
#include "Domain/Wave1FeatureGate.h"

namespace FileScoutNative::Shell
{
    namespace
    {
        void AppendCommand(
            std::vector<FileScoutNative::Domain::ExplorerCommand>& commands,
            std::wstring id,
            std::wstring label,
            std::wstring iconKey = {},
            std::wstring shortcutText = {},
            bool available = true,
            FileScoutNative::Domain::ExplorerCommandSource source =
                FileScoutNative::Domain::ExplorerCommandSource::App,
            bool isDestructive = false)
        {
            commands.push_back(ContextMenuService::MakeCommand(
                std::move(id),
                std::move(label),
                std::move(iconKey),
                std::move(shortcutText),
                available,
                source,
                isDestructive));
        }
    }

    FileScoutNative::Domain::ContextMenuModel ContextMenuService::BuildMenu(
        ContextMenuScenario const& scenario) const
    {
        FileScoutNative::Domain::ContextMenuModel model;
        const auto hasSelection = scenario.focused_item.has_value();
        auto const selectionCount =
            scenario.selection_count != 0 ? scenario.selection_count : (hasSelection ? 1u : 0u);

        if (scenario.is_read_only_archive_view)
        {
            if (hasSelection)
            {
                AppendCommand(
                    model.primary_commands,
                    L"open",
                    L"Open",
                    L"open",
                    scenario.focused_item->is_folder ? L"Enter" : L"");

                FileScoutNative::Domain::ContextMenuGroup extractGroup;
                AppendCommand(
                    extractGroup.commands,
                    L"extractToArchiveFolder",
                    L"Extract to archive folder",
                    L"copy");
                AppendCommand(
                    extractGroup.commands,
                    L"extractReveal",
                    L"Extract and reveal",
                    L"open");
                model.groups.push_back(std::move(extractGroup));
            }

            FileScoutNative::Domain::ContextMenuGroup workspaceGroup;
            AppendCommand(workspaceGroup.commands, L"refresh", L"Refresh", L"refresh", L"F5");
            model.groups.push_back(std::move(workspaceGroup));
            return model;
        }

        if (hasSelection)
        {
            if (selectionCount == 1)
            {
                AppendCommand(
                    model.primary_commands,
                    L"open",
                    L"Open",
                    L"open",
                    scenario.focused_item->is_folder ? L"Enter" : L"");

                if (scenario.focused_item->is_folder)
                {
                    AppendCommand(
                        model.primary_commands,
                        L"openInNewTab",
                        L"Open in new tab",
                        L"openInNewTab");
                }
                else
                {
                    AppendCommand(model.primary_commands, L"openWith", L"Open with...", L"openWith");
                }
            }

            if (selectionCount == 1 ||
                FileScoutNative::Domain::IsWave1FeatureEnabled(
                    FileScoutNative::Domain::Wave1Feature::BatchRename))
            {
                AppendCommand(
                    model.primary_commands,
                    L"rename",
                    selectionCount > 1 ? L"Batch rename..." : L"Rename",
                    L"rename",
                    L"F2");
            }
            AppendCommand(model.primary_commands, L"delete", L"Delete", L"delete", L"Del",
                true, FileScoutNative::Domain::ExplorerCommandSource::App, true);
        }

        if (hasSelection || scenario.can_paste)
        {
            FileScoutNative::Domain::ContextMenuGroup clipboardGroup;
            if (hasSelection)
            {
                AppendCommand(clipboardGroup.commands, L"copy", L"Copy", L"copy", L"Ctrl+C");
                AppendCommand(clipboardGroup.commands, L"copyPath", L"Copy path", L"copyPath", L"Ctrl+Shift+C");
                AppendCommand(clipboardGroup.commands, L"cut", L"Cut", L"cut", L"Ctrl+X");
            }

            if (scenario.can_paste)
            {
                AppendCommand(clipboardGroup.commands, L"paste", L"Paste", L"paste", L"Ctrl+V");
            }

            if (!clipboardGroup.commands.empty())
            {
                model.groups.push_back(std::move(clipboardGroup));
            }
        }

        if (hasSelection && selectionCount == 1)
        {
            FileScoutNative::Domain::ContextMenuGroup inspectGroup;
            if (scenario.can_pin_to_places)
            {
                AppendCommand(inspectGroup.commands, L"pinToPlaces", L"Pin to Bookmarks", L"pin");
            }
            AppendCommand(inspectGroup.commands, L"properties", L"Properties", L"properties", L"Alt+Enter");
            AppendCommand(
                inspectGroup.commands,
                L"moreWindowsActions",
                L"More Windows actions...",
                L"overflow",
                {},
                true,
                FileScoutNative::Domain::ExplorerCommandSource::Overflow);

            model.groups.push_back(std::move(inspectGroup));
            model.has_overflow_menu = true;
        }

        FileScoutNative::Domain::ContextMenuGroup workspaceGroup;
        if (scenario.can_pin_to_places && !hasSelection)
        {
            AppendCommand(workspaceGroup.commands, L"pinToPlaces", L"Pin current folder to Bookmarks", L"pin");
        }
        if (!hasSelection)
        {
            AppendCommand(workspaceGroup.commands, L"copyPath", L"Copy folder path", L"copyPath", L"Ctrl+Shift+C");
        }
        AppendCommand(workspaceGroup.commands, L"openInTerminal", L"Open in Terminal", L"terminal");
        AppendCommand(workspaceGroup.commands, L"newFolder", L"New folder", L"newFolder");
        AppendCommand(workspaceGroup.commands, L"newFile", L"New file...", L"newFile");
        AppendCommand(workspaceGroup.commands, L"refresh", L"Refresh", L"refresh", L"F5");
        model.groups.push_back(std::move(workspaceGroup));

        return model;
    }

    FileScoutNative::Domain::ExplorerCommand ContextMenuService::MakeCommand(
        std::wstring id,
        std::wstring label,
        std::wstring iconKey,
        std::wstring shortcutText,
        bool const available,
        FileScoutNative::Domain::ExplorerCommandSource const source,
        bool const isDestructive)
    {
        return FileScoutNative::Domain::ExplorerCommand{
            std::move(id),
            std::move(label),
            std::move(iconKey),
            std::move(shortcutText),
            available,
            isDestructive,
            source,
        };
    }
}
