#pragma once

#include <cstddef>

namespace FileScoutNative::Search
{
    // Most matches a search shows: the in-folder filter's cut, the recursive walk's heap size
    // and the "Showing top N matches" status all use this.
    inline constexpr std::size_t k_max_search_results = 200;
}
