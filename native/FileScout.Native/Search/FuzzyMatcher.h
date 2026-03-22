#pragma once
#include <string_view>
#include <cwctype>

namespace FileScoutNative::Search
{
    // Returns a score >= 0 if query matches filename as a fuzzy subsequence, -1 if no match.
    // Query length < 2 always returns -1.
    // Characters *, ?, [ are treated as literals — no glob expansion ever occurs.
    // Stack-only: no heap allocation per call.
    inline int FuzzyScore(std::wstring_view query, std::wstring_view filename)
    {
        if (query.size() < 2)
            return -1;

        int score = 0;
        std::size_t fi = 0;     // current position in filename
        std::size_t qi = 0;     // current position in query
        int consecutiveRun = 0; // length of current consecutive match run

        while (qi < query.size() && fi < filename.size())
        {
            wchar_t qc = std::towlower(query[qi]);
            wchar_t fc = std::towlower(filename[fi]);

            if (qc == fc)
            {
                // Prefix bonus: first query char matches at filename index 0
                if (qi == 0 && fi == 0)
                    score += 20;

                // Consecutive bonus: this match immediately follows the previous match
                if (consecutiveRun > 0)
                {
                    score += 15;
                }
                consecutiveRun++;

                // Word-boundary bonus: match starts after a separator or case transition
                if (fi > 0)
                {
                    wchar_t prev = filename[fi - 1];
                    bool afterSeparator = (prev == L'.' || prev == L'_' ||
                                           prev == L'-' || prev == L' ');
                    bool afterCaseChange = (std::iswlower(prev) && std::iswupper(filename[fi]));
                    if (afterSeparator || afterCaseChange)
                        score += 8;
                }

                ++qi;
            }
            else
            {
                consecutiveRun = 0;
            }
            ++fi;
        }

        // All query characters must be found as a subsequence
        if (qi < query.size())
            return -1;

        // Match ratio penalty: penalise when query is very short vs filename
        int const ratio = static_cast<int>(
            (filename.size() - query.size()) * 2 / (filename.size() + 1));
        score -= ratio;

        return (score < 0) ? 0 : score;
    }

} // namespace FileScoutNative::Search
