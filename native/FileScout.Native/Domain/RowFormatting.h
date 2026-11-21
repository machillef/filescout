#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    std::wstring FormatFileSize(std::uintmax_t bytes);

    std::wstring BuildDetailText(std::wstring_view kindLabel,
                                  std::uintmax_t sizeBytes,
                                  bool sizeAvailable);
}
