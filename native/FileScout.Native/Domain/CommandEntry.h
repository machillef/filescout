#pragma once
#include <string>

namespace FileScoutNative::Domain
{
    // A single entry in the command palette — pure data, no WinRT dependencies.
    struct CommandEntry
    {
        std::wstring id;            // Stable identifier used for dispatch (e.g. L"newTab")
        std::wstring display_name;  // Human-readable label shown in the palette list
        std::wstring shortcut_text; // Optional shortcut hint (e.g. L"Ctrl+T")
        std::wstring category;      // Grouping label (e.g. L"Navigation", L"File")
    };
}
