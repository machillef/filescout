#pragma once
#include <string>

namespace FileScoutNative::Domain
{
    struct CommandEntry
    {
        std::wstring id;
        std::wstring display_name;
        std::wstring shortcut_text;
        std::wstring category;
    };
}
