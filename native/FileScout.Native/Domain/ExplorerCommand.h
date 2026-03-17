#pragma once

namespace FileScoutNative::Domain
{
    enum class ExplorerCommandSource
    {
        App,
        Shell,
        Overflow,
    };

    struct ExplorerCommand
    {
        std::wstring id;
        std::wstring label;
        std::wstring icon_key;
        std::wstring shortcut_text;
        bool available{ true };
        ExplorerCommandSource source{ ExplorerCommandSource::App };
    };
}
