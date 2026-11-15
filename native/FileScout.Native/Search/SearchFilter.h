#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>
#include "Domain/ShellItemRef.h"
#include "Search/FuzzyMatcher.h"
#include "Search/SearchLimits.h"

namespace FileScoutNative::Search
{
    inline std::vector<::FileScoutNative::Domain::ShellItemRef const*> BuildFilterResults(
        std::wstring_view const query,
        std::span<::FileScoutNative::Domain::ShellItemRef const> snapshot,
        std::size_t const max_results = k_max_search_results)
    {
        std::vector<::FileScoutNative::Domain::ShellItemRef const*> results;
        if (snapshot.empty() || max_results == 0)
        {
            return results;
        }

        if (query.size() < 2)
        {
            auto const limit = (std::min)(snapshot.size(), max_results);
            results.reserve(limit);
            for (std::size_t index = 0; index < limit; ++index)
            {
                results.push_back(&snapshot[index]);
            }
            return results;
        }

        struct RankedMatch
        {
            int score{ -1 };
            std::size_t index{ 0 };
        };

        std::vector<RankedMatch> ranked;
        ranked.reserve(snapshot.size());

        for (std::size_t index = 0; index < snapshot.size(); ++index)
        {
            auto const score = FuzzyScore(query, snapshot[index].display_name);
            if (score >= 0)
            {
                ranked.push_back(RankedMatch{ score, index });
            }
        }

        std::stable_sort(
            ranked.begin(),
            ranked.end(),
            [](RankedMatch const& left, RankedMatch const& right)
            {
                return left.score > right.score;
            });

        auto const limit = (std::min)(ranked.size(), max_results);
        results.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index)
        {
            results.push_back(&snapshot[ranked[index].index]);
        }

        return results;
    }
} // namespace FileScoutNative::Search
