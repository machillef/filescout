#pragma once

#include "ExplorerCommand.h"

namespace FileScoutNative::Domain
{
    struct ContextMenuGroup
    {
        std::vector<ExplorerCommand> commands;
    };

    struct ContextMenuModel
    {
        std::vector<ExplorerCommand> primary_commands;
        std::vector<ContextMenuGroup> groups;
        bool has_overflow_menu{ false };
    };
}
