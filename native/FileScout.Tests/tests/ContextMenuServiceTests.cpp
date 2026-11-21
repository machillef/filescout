#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/ContextMenuService.h"

using namespace FileScoutNative::Shell;
using namespace FileScoutNative::Domain;

namespace
{
    ShellItemRef MakeFile(std::wstring name = L"file.txt")
    {
        ShellItemRef item;
        item.display_name = std::move(name);
        item.is_folder = false;
        return item;
    }

    ShellItemRef MakeFolder(std::wstring name = L"folder")
    {
        ShellItemRef item;
        item.display_name = std::move(name);
        item.is_folder = true;
        return item;
    }

    bool HasPrimaryCommand(ContextMenuModel const& model, std::wstring_view id)
    {
        for (auto const& cmd : model.primary_commands)
            if (cmd.id == id) return true;
        return false;
    }

    std::wstring GetPrimaryCommandLabel(ContextMenuModel const& model, std::wstring_view id)
    {
        for (auto const& cmd : model.primary_commands)
            if (cmd.id == id) return cmd.label;
        return {};
    }

    bool HasGroupCommand(ContextMenuModel const& model, std::wstring_view id)
    {
        for (auto const& group : model.groups)
            for (auto const& cmd : group.commands)
                if (cmd.id == id) return true;
        return false;
    }
}

TEST(ContextMenuServiceTest, EmptyScenario_ReturnsWorkspaceGroupOnly)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{};

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(model.primary_commands.empty());
    EXPECT_FALSE(model.has_overflow_menu);
    ASSERT_EQ(model.groups.size(), 1u);
    EXPECT_TRUE(HasGroupCommand(model, L"newFolder"));
    EXPECT_TRUE(HasGroupCommand(model, L"refresh"));
}

TEST(ContextMenuServiceTest, FileSelected_HasOpenRenameDelete)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasPrimaryCommand(model, L"open"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"openWith"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"rename"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"delete"));
    EXPECT_FALSE(HasGroupCommand(model, L"pinToPlaces"));
    EXPECT_TRUE(model.has_overflow_menu);
}

TEST(ContextMenuServiceTest, FolderSelected_NoOpenWith)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFolder() };
    scenario.can_pin_to_places = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasPrimaryCommand(model, L"open"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"openInNewTab"));
    EXPECT_FALSE(HasPrimaryCommand(model, L"openWith"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"rename"));
    EXPECT_TRUE(HasPrimaryCommand(model, L"delete"));
    EXPECT_TRUE(HasGroupCommand(model, L"copyPath"));
    EXPECT_TRUE(HasGroupCommand(model, L"openInTerminal"));
    EXPECT_TRUE(HasGroupCommand(model, L"newFile"));
    EXPECT_TRUE(HasGroupCommand(model, L"pinToPlaces"));
}

TEST(ContextMenuServiceTest, CanPasteNoSelection_ClipboardGroupHasPasteOnly)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{};
    scenario.can_paste = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasGroupCommand(model, L"paste"));
    EXPECT_FALSE(HasGroupCommand(model, L"copy"));
    EXPECT_FALSE(HasGroupCommand(model, L"cut"));
}

TEST(ContextMenuServiceTest, NoSelectionCanPinCurrentFolder_ShowsWorkspacePinCommand)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{};
    scenario.can_pin_to_places = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasGroupCommand(model, L"pinToPlaces"));
    EXPECT_TRUE(HasGroupCommand(model, L"newFolder"));
    EXPECT_TRUE(HasGroupCommand(model, L"refresh"));
}

TEST(ContextMenuServiceTest, SelectionAndCanPaste_ClipboardHasCopyCutPaste)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };
    scenario.can_paste = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasGroupCommand(model, L"copy"));
    EXPECT_TRUE(HasGroupCommand(model, L"cut"));
    EXPECT_TRUE(HasGroupCommand(model, L"paste"));
}

TEST(ContextMenuServiceTest, SelectionNoPaste_NoPasteCommand)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };
    scenario.can_paste = false;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasGroupCommand(model, L"copy"));
    EXPECT_TRUE(HasGroupCommand(model, L"cut"));
    EXPECT_FALSE(HasGroupCommand(model, L"paste"));
}

TEST(ContextMenuServiceTest, MultiSelection_HidesRenameWhenBatchRenameIsOutsideClosedBeta)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };
    scenario.selection_count = 3;

    auto model = svc.BuildMenu(scenario);

    EXPECT_FALSE(HasPrimaryCommand(model, L"open"));
    EXPECT_FALSE(HasPrimaryCommand(model, L"openWith"));
    EXPECT_FALSE(HasPrimaryCommand(model, L"rename"));
    EXPECT_FALSE(HasGroupCommand(model, L"properties"));
    EXPECT_FALSE(HasGroupCommand(model, L"moreWindowsActions"));
    EXPECT_TRUE(HasGroupCommand(model, L"copy"));
    EXPECT_TRUE(HasGroupCommand(model, L"cut"));
}

TEST(ContextMenuServiceTest, ArchiveView_NoSelection_RefreshOnly)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{};
    scenario.is_read_only_archive_view = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(model.primary_commands.empty());
    EXPECT_FALSE(model.has_overflow_menu);
    EXPECT_FALSE(HasGroupCommand(model, L"newFolder"));
    EXPECT_TRUE(HasGroupCommand(model, L"refresh"));
}

TEST(ContextMenuServiceTest, ArchiveView_FileSelected_HasOpenAndExtract)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };
    scenario.is_read_only_archive_view = true;

    auto model = svc.BuildMenu(scenario);

    EXPECT_TRUE(HasPrimaryCommand(model, L"open"));
    EXPECT_FALSE(HasPrimaryCommand(model, L"rename"));
    EXPECT_FALSE(HasPrimaryCommand(model, L"delete"));
    EXPECT_TRUE(HasGroupCommand(model, L"extractToArchiveFolder"));
    EXPECT_TRUE(HasGroupCommand(model, L"extractReveal"));
}

TEST(ContextMenuServiceTest, MakeCommand_SetsAllFields)
{
    auto cmd = ContextMenuService::MakeCommand(
        L"myId", L"My Label", L"myIcon", L"Ctrl+Z",
        false, ExplorerCommandSource::Overflow);

    EXPECT_EQ(cmd.id, L"myId");
    EXPECT_EQ(cmd.label, L"My Label");
    EXPECT_EQ(cmd.icon_key, L"myIcon");
    EXPECT_EQ(cmd.shortcut_text, L"Ctrl+Z");
    EXPECT_FALSE(cmd.available);
    EXPECT_EQ(cmd.source, ExplorerCommandSource::Overflow);
}

TEST(ContextMenuServiceTest, MakeCommand_DefaultsToAppSource)
{
    auto cmd = ContextMenuService::MakeCommand(L"id", L"label");

    EXPECT_TRUE(cmd.available);
    EXPECT_EQ(cmd.source, ExplorerCommandSource::App);
    EXPECT_TRUE(cmd.icon_key.empty());
    EXPECT_TRUE(cmd.shortcut_text.empty());
}

TEST(ContextMenuServiceTest, DeleteCommand_IsMarkedDestructive)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };

    auto model = svc.BuildMenu(scenario);

    bool foundDelete = false;
    for (auto const& cmd : model.primary_commands)
    {
        if (cmd.id == L"delete")
        {
            EXPECT_TRUE(cmd.is_destructive);
            foundDelete = true;
        }
    }
    EXPECT_TRUE(foundDelete);
}

TEST(ContextMenuServiceTest, OpenRenameCommands_NotMarkedDestructive)
{
    ContextMenuService svc;
    ContextMenuScenario scenario{ MakeFile() };

    auto model = svc.BuildMenu(scenario);

    for (auto const& cmd : model.primary_commands)
    {
        if (cmd.id == L"open" || cmd.id == L"rename" || cmd.id == L"openWith")
        {
            EXPECT_FALSE(cmd.is_destructive) << "Command '" << cmd.id.c_str() << "' should not be destructive";
        }
    }
}

TEST(ContextMenuServiceTest, MakeCommand_DefaultsIsDestructiveToFalse)
{
    auto cmd = ContextMenuService::MakeCommand(L"id", L"label");
    EXPECT_FALSE(cmd.is_destructive);
}
