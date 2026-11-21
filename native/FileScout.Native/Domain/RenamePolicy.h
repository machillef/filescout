#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    // A rename is a no-op only when the new name is exactly the current one. The compare is
    // case-sensitive on purpose: NTFS keeps the case of a name, so foo.txt -> Foo.txt is a real
    // rename (Explorer allows it), even though the file system treats both names as the same file.
    [[nodiscard]] inline bool IsRenameNoOp(std::wstring const& newName, std::wstring const& currentName)
    {
        return newName == currentName;
    }

    [[nodiscard]] inline int32_t ComputeRenameSelectionLength(
        std::wstring_view const displayName,
        bool const isFolder) noexcept
    {
        if (isFolder || displayName.empty())
        {
            return static_cast<int32_t>(displayName.size());
        }

        auto const dotPos = displayName.rfind(L'.');
        if (dotPos == std::wstring_view::npos || dotPos == 0)
        {
            return static_cast<int32_t>(displayName.size());
        }

        return static_cast<int32_t>(dotPos);
    }
}
