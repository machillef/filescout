#pragma once

#include <string>
#include <string_view>

namespace FileScoutNative::Shell
{
    // Canonical verb of a context-menu command (GCS_VERBW), or empty when the handler has none.
    // Grows the buffer when the handler reports it too small, so long verbs are not truncated.
    [[nodiscard]] std::wstring ReadShellCommandVerb(IContextMenu* contextMenu, UINT commandOffset);

    // Verbs FileScout hides from the shell overflow menu because it has its own commands for them.
    [[nodiscard]] bool IsBlockedShellVerb(std::wstring_view verb) noexcept;
}
