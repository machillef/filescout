#pragma once
#include <string_view>
#include <cwctype>
#include "Domain/TextFold.h"

namespace FileScoutNative::Search
{
    inline int FuzzyScore(std::wstring_view query, std::wstring_view filename)
    {
        if (query.size() < 2)
            return -1;

        int score = 0;
        std::size_t fi = 0;
        std::size_t qi = 0;
        int consecutiveRun = 0;

        while (qi < query.size() && fi < filename.size())
        {
            wchar_t qc = ::FileScoutNative::Domain::FoldChar(query[qi]);
            wchar_t fc = ::FileScoutNative::Domain::FoldChar(filename[fi]);

            if (qc == fc)
            {
                if (qi == 0 && fi == 0)
                    score += 20;

                if (consecutiveRun > 0)
                {
                    score += 15;
                }
                consecutiveRun++;

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

        if (qi < query.size())
            return -1;

        int const ratio = static_cast<int>(
            (filename.size() - query.size()) * 2 / (filename.size() + 1));
        score -= ratio;

        return (score < 0) ? 0 : score;
    }

}
