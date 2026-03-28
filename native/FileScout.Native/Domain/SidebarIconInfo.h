#pragma once

#include <cstdint>
#include <string_view>

namespace FileScoutNative::Domain
{
    [[nodiscard]] std::wstring_view KnownFolderGlyph(std::wstring_view displayName);
    [[nodiscard]] std::wstring_view DriveTypeGlyph(std::uint32_t driveType);
}
