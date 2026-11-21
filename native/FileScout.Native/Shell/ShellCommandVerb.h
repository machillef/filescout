#pragma once

#include <string>
#include <string_view>

namespace FileScoutNative::Shell
{
    [[nodiscard]] std::wstring ReadShellCommandVerb(IContextMenu* contextMenu, UINT commandOffset);

    [[nodiscard]] bool IsBlockedShellVerb(std::wstring_view verb) noexcept;
}
