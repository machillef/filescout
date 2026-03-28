#include "pch.h"
#include "Domain/RowFormatting.h"
#include <array>
#include <iomanip>
#include <sstream>

namespace FileScoutNative::Domain
{
    std::wstring FormatFileSize(std::uintmax_t const bytes)
    {
        static constexpr std::array<std::wstring_view, 5> suffixes{
            L"B", L"KB", L"MB", L"GB", L"TB",
        };

        double value = static_cast<double>(bytes);
        std::size_t suffixIndex = 0;

        while (value >= 1024.0 && suffixIndex + 1 < suffixes.size())
        {
            value /= 1024.0;
            ++suffixIndex;
        }

        std::wstringstream ss;
        ss << std::fixed
           << std::setprecision(value >= 10.0 || suffixIndex == 0 ? 0 : 1)
           << value;
        return ss.str() + L" " + std::wstring(suffixes[suffixIndex]);
    }

    std::wstring BuildDetailText(std::wstring_view const kindLabel,
                                  std::uintmax_t const sizeBytes,
                                  bool const sizeAvailable)
    {
        std::wstring detail{ kindLabel };
        if (sizeAvailable)
        {
            detail += L"  |  ";
            detail += FormatFileSize(sizeBytes);
        }
        return detail;
    }
}
