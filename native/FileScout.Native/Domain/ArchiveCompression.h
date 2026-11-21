#pragma once

#include "NavigationHistory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FileScoutNative::Domain
{
    // Quotes a single Windows command-line argument according to CommandLineToArgvW rules.
    [[nodiscard]] inline std::wstring QuoteWindowsCommandArg(std::wstring_view const arg)
    {
        if (arg.empty())
        {
            return L"\"\"";
        }

        bool const needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
        if (!needsQuotes)
        {
            return std::wstring{ arg };
        }

        std::wstring result;
        result.reserve(arg.size() + 4);
        result.push_back(L'"');

        std::size_t backslashes = 0;
        for (wchar_t const ch : arg)
        {
            if (ch == L'\\')
            {
                ++backslashes;
            }
            else if (ch == L'"')
            {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(L'"');
                backslashes = 0;
            }
            else
            {
                result.append(backslashes, L'\\');
                backslashes = 0;
                result.push_back(ch);
            }
        }

        // Double trailing backslashes so they do not escape the closing quote.
        result.append(backslashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }

    // Chooses a collision-free `.zip` output path in `parentDirectory` for the given selection.
    [[nodiscard]] inline std::filesystem::path SuggestZipArchivePath(
        std::filesystem::path const& parentDirectory,
        std::span<std::filesystem::path const> items,
        std::function<bool(std::filesystem::path const&)> const& existsFn,
        bool const singleItemIsFolder = false)
    {
        std::wstring baseName;
        if (items.size() == 1)
        {
            auto const& only = items.front();
            auto const filename = only.filename().wstring();
            if (singleItemIsFolder)
            {
                baseName = filename;
            }
            else
            {
                auto const stem = only.stem().wstring();
                baseName = (!stem.empty() && stem != L".") ? stem : filename;
            }
        }
        else if (!parentDirectory.empty())
        {
            baseName = parentDirectory.filename().wstring();
        }

        if (baseName.empty() || baseName == L"." || baseName == L"..")
        {
            baseName = L"Archive";
        }

        auto candidate = parentDirectory / (baseName + L".zip");
        if (!existsFn || !existsFn(candidate))
        {
            return candidate;
        }

        for (int suffix = 2; suffix < 10000; ++suffix)
        {
            candidate = parentDirectory / (baseName + L" (" + std::to_wstring(suffix) + L").zip");
            if (!existsFn(candidate))
            {
                return candidate;
            }
        }

        return parentDirectory / (baseName + L".zip");
    }

    // Builds the `tar.exe -a -c -f <zip> -- <entries...>` command line.
    // The `--` separator prevents filenames beginning with `-` from being parsed as tar options.
    [[nodiscard]] inline std::wstring BuildTarZipCommandLine(
        std::filesystem::path const& tarExePath,
        std::filesystem::path const& outputZipPath,
        std::span<std::wstring const> relativeNames)
    {
        std::wstring cmd = QuoteWindowsCommandArg(tarExePath.wstring());
        cmd += L" -a -c -f ";
        cmd += QuoteWindowsCommandArg(outputZipPath.wstring());
        cmd += L" --";
        for (auto const& name : relativeNames)
        {
            cmd.push_back(L' ');
            cmd += QuoteWindowsCommandArg(name);
        }
        return cmd;
    }

    // Reconstructs a Recycle Bin item's original full path from PKEY_Recycle_DeletedFrom and display name.
    [[nodiscard]] inline std::filesystem::path BuildRecycleBinOriginalPath(
        std::wstring_view const deletedFromDir,
        std::wstring_view const displayName)
    {
        if (deletedFromDir.empty() || displayName.empty())
        {
            return {};
        }
        return (std::filesystem::path{ deletedFromDir } / std::filesystem::path{ displayName }).lexically_normal();
    }

    struct RecycleBinCandidate
    {
        std::wstring parsing_name;
        std::filesystem::path original_path;
        uint64_t deleted_time_filetime{ 0 };
    };

    // Selects the most recently deleted Recycle Bin parsing name matching each target original path.
    // Each Recycle Bin item is matched at most once.
    [[nodiscard]] inline std::vector<std::wstring> SelectNewestRecycleBinMatches(
        std::span<RecycleBinCandidate const> candidates,
        std::span<std::filesystem::path const> targetOriginalPaths)
    {
        std::vector<std::wstring> matched;
        std::vector<bool> used(candidates.size(), false);

        for (auto const& target : targetOriginalPaths)
        {
            auto const normTarget = NormalizeFolderForCompare(target);
            std::size_t bestIndex = candidates.size();
            uint64_t bestTime = 0;

            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                if (used[i] || candidates[i].parsing_name.empty() || candidates[i].original_path.empty())
                {
                    continue;
                }
                if (!PathsEqualIgnoringCase(NormalizeFolderForCompare(candidates[i].original_path), normTarget))
                {
                    continue;
                }
                if (bestIndex == candidates.size() || candidates[i].deleted_time_filetime >= bestTime)
                {
                    bestIndex = i;
                    bestTime = candidates[i].deleted_time_filetime;
                }
            }

            if (bestIndex < candidates.size())
            {
                used[bestIndex] = true;
                matched.push_back(candidates[bestIndex].parsing_name);
            }
        }

        return matched;
    }
}
