#pragma once

#include <cwctype>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "Domain/ShellItemRef.h"
#include "Domain/TextFold.h"
#include "Search/SearchLimits.h"
#include "Search/SearchService.h"

namespace FileScoutNative::Search
{
    struct SearchOpenTarget
    {
        std::filesystem::path folder_path;
        std::filesystem::path select_path;
    };

    inline std::wstring BuildSearchProgressStatus(std::size_t const count)
    {
        return std::to_wstring(count) + (count == 1 ? L" match" : L" matches");
    }

    inline std::wstring BuildSearchCompleteStatus(
        std::size_t const count,
        std::filesystem::path const& root,
        bool const cap_reached)
    {
        if (cap_reached)
        {
            return L"Showing top " + std::to_wstring(k_max_search_results) +
                L" matches - refine your query to narrow results";
        }

        return BuildSearchProgressStatus(count) + L" in " + root.wstring();
    }

    inline std::wstring BuildSearchResultSubtitle(
        std::filesystem::path const& result_path,
        std::filesystem::path const& root)
    {
        std::error_code ec;
        auto const relative_parent = std::filesystem::relative(
            result_path.parent_path(),
            root,
            ec);
        if (ec || relative_parent.empty() || relative_parent == std::filesystem::path{ L"." })
        {
            return {};
        }

        auto subtitle = relative_parent.wstring();
        if (!subtitle.empty() &&
            subtitle.back() != std::filesystem::path::preferred_separator)
        {
            subtitle.push_back(std::filesystem::path::preferred_separator);
        }

        return subtitle;
    }

    inline std::vector<std::size_t> FindSearchHighlightPositions(
        std::wstring_view const query,
        std::wstring_view const filename)
    {
        if (query.size() < 2)
        {
            return {};
        }

        std::vector<std::size_t> positions;
        positions.reserve(query.size());

        std::size_t filename_index = 0;
        for (wchar_t const query_ch : query)
        {
            bool matched = false;
            for (; filename_index < filename.size(); ++filename_index)
            {
                if (::FileScoutNative::Domain::FoldChar(filename[filename_index]) ==
                    ::FileScoutNative::Domain::FoldChar(query_ch))
                {
                    positions.push_back(filename_index);
                    ++filename_index;
                    matched = true;
                    break;
                }
            }

            if (!matched)
            {
                return {};
            }
        }

        return positions;
    }

    inline std::optional<SearchOpenTarget> TryBuildSearchOpenTarget(
        std::filesystem::path const& result_path)
    {
        if (result_path.empty())
        {
            return std::nullopt;
        }

        auto const parent = result_path.parent_path();
        if (parent.empty())
        {
            return std::nullopt;
        }

        return SearchOpenTarget{
            .folder_path = parent,
            .select_path = result_path,
        };
    }

    inline std::vector<::FileScoutNative::Domain::ShellItemRef> BuildSearchResultItems(
        std::span<SearchResult const> results,
        std::filesystem::path const& root)
    {
        std::vector<::FileScoutNative::Domain::ShellItemRef> items;
        items.reserve(results.size());

        for (auto const& result : results)
        {
            ::FileScoutNative::Domain::ShellItemRef item;
            item.path = std::filesystem::path{ result.full_path };
            item.display_name = result.filename;
            item.parsing_name = result.full_path;
            item.kind_label = BuildSearchResultSubtitle(item.path, root);
            items.push_back(std::move(item));
        }

        return items;
    }
}
