#pragma once

#include <string>

namespace FileScoutNative::Domain
{
    // A rename is a no-op only when the new name is exactly the current one. The compare is
    // case-sensitive on purpose: NTFS keeps the case of a name, so foo.txt -> Foo.txt is a real
    // rename (Explorer allows it), even though the file system treats both names as the same file.
    [[nodiscard]] inline bool IsRenameNoOp(std::wstring const& newName, std::wstring const& currentName)
    {
        return newName == currentName;
    }
}
