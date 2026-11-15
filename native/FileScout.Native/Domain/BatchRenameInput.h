#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace FileScoutNative::Domain
{
    // Parses a batch-rename number box (start number, zero padding): ASCII digits only after
    // trimming blanks, no sign, value 1..UINT32_MAX. wcstoul is not used because it accepts a
    // leading '+' and wraps "-1" to 4294967295.
    inline std::optional<uint32_t> ParseBatchRenamePositiveInteger(std::wstring_view value) noexcept
    {
        constexpr std::wstring_view blanks = L" \t\r\n";
        auto const begin = value.find_first_not_of(blanks);
        if (begin == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        value = value.substr(begin, value.find_last_not_of(blanks) - begin + 1);

        uint64_t parsed = 0;
        for (wchar_t const ch : value)
        {
            if (ch < L'0' || ch > L'9')
            {
                return std::nullopt;
            }

            parsed = parsed * 10 + static_cast<uint64_t>(ch - L'0');
            if (parsed > (std::numeric_limits<uint32_t>::max)())
            {
                return std::nullopt;
            }
        }

        if (parsed == 0)
        {
            return std::nullopt;
        }

        return static_cast<uint32_t>(parsed);
    }
}
