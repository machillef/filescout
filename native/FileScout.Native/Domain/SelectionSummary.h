#pragma once

#include "RowFormatting.h"
#include "ShellItemRef.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace FileScoutNative::Domain
{
    struct SelectionSummary
    {
        std::size_t total_count{ 0 };
        std::size_t file_count{ 0 };
        std::size_t folder_count{ 0 };
        std::uintmax_t known_size_bytes{ 0 };
        std::size_t files_with_size{ 0 };
    };

    [[nodiscard]] inline SelectionSummary SummarizeSelection(std::span<ShellItemRef const> const items) noexcept
    {
        SelectionSummary summary{};
        summary.total_count = items.size();
        for (auto const& item : items)
        {
            if (item.is_folder)
            {
                ++summary.folder_count;
            }
            else
            {
                ++summary.file_count;
                if (item.size_available)
                {
                    summary.known_size_bytes += item.size_bytes;
                    ++summary.files_with_size;
                }
            }
        }
        return summary;
    }

    // Builds the inspector subtitle for multi-selection (e.g. "17 files  |  2 folders  |  4.82 GB").
    [[nodiscard]] inline std::wstring FormatSelectionMetaText(SelectionSummary const& summary)
    {
        std::wstring text;
        if (summary.file_count > 0)
        {
            text += std::to_wstring(summary.file_count) + (summary.file_count == 1 ? L" file" : L" files");
        }
        if (summary.folder_count > 0)
        {
            if (!text.empty())
            {
                text += L"  |  ";
            }
            text += std::to_wstring(summary.folder_count) + (summary.folder_count == 1 ? L" folder" : L" folders");
        }
        if (summary.files_with_size > 0)
        {
            if (!text.empty())
            {
                text += L"  |  ";
            }
            text += FormatFileSize(summary.known_size_bytes);
        }
        return text;
    }

    // Builds the status bar text for multi-selection (e.g. "19 items selected  |  4.82 GB").
    [[nodiscard]] inline std::wstring FormatSelectionStatusText(SelectionSummary const& summary)
    {
        std::wstring text = std::to_wstring(summary.total_count) +
            (summary.total_count == 1 ? L" item selected" : L" items selected");
        if (summary.files_with_size > 0)
        {
            text += L"  |  " + FormatFileSize(summary.known_size_bytes);
        }
        return text;
    }

    // Formats selected item paths (or the current directory if none are selected) for Copy Path.
    [[nodiscard]] inline std::wstring FormatCopyPathsPayload(
        std::span<ShellItemRef const> const items,
        std::filesystem::path const& fallbackDirectory)
    {
        std::wstring result;
        if (items.empty())
        {
            return fallbackDirectory.wstring();
        }
        for (auto const& item : items)
        {
            auto const pathStr = item.path.wstring();
            if (pathStr.empty())
            {
                continue;
            }
            if (!result.empty())
            {
                result += L"\r\n";
            }
            result += pathStr;
        }
        return result;
    }
}
