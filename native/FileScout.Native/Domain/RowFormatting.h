#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    // Format a raw byte count to a human-readable size string.
    // Examples: 0 → "0 B", 1024 → "1.0 KB", 1048576 → "1.0 MB"
    // Threshold: values >= 10.x use 0 decimal places; others use 1.
    std::wstring FormatFileSize(std::uintmax_t bytes);

    // Build the detail chip text used in card-mode rows.
    // With size:    "TypeLabel  |  FormattedSize"   (e.g. "PDF Document  |  1.2 MB")
    // Without size: "TypeLabel"                     (e.g. "Folder")
    std::wstring BuildDetailText(std::wstring_view kindLabel,
                                  std::uintmax_t sizeBytes,
                                  bool sizeAvailable);
}
