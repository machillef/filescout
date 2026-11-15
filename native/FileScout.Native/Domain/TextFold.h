#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace FileScoutNative::Domain
{
    // Unicode-aware case folding that does not depend on the C locale. The app never calls
    // setlocale, so towlower/towupper/_wcsicmp fold ASCII only and Greek (or any non-ASCII)
    // names compared case-sensitively. These helpers use the invariant NLS tables instead, the
    // same simple per-UTF-16-unit mapping NTFS uses for case-insensitive names.
    namespace detail
    {
        using CaseTable = std::array<wchar_t, 0x10000>;

        inline std::unique_ptr<CaseTable> BuildCaseTable(DWORD const mapFlags)
        {
            auto table = std::make_unique<CaseTable>();
            for (std::size_t index = 0; index < table->size(); ++index)
            {
                (*table)[index] = static_cast<wchar_t>(index);
            }

            // One call over the whole BMP; surrogate halves map to themselves.
            CaseTable mapped{};
            auto const written = LCMapStringEx(
                LOCALE_NAME_INVARIANT,
                mapFlags,
                table->data(),
                static_cast<int>(table->size()),
                mapped.data(),
                static_cast<int>(mapped.size()),
                nullptr,
                nullptr,
                0);
            if (written == static_cast<int>(mapped.size()))
            {
                *table = mapped;
            }

            return table;
        }

        inline CaseTable const& LowerTable()
        {
            static auto const table = BuildCaseTable(LCMAP_LOWERCASE);
            return *table;
        }

        // Case folding = lowercase plus the one Greek special case of Unicode simple folding:
        // final sigma (U+03C2) folds like sigma (U+03C3), so a name typed in capitals matches.
        // Also covers the other BMP entries of Unicode CaseFolding.txt (status C/S) whose target is
        // not the lowercase mapping: Greek/Latin letter variants fold to the letter a user types.
        inline std::unique_ptr<CaseTable> BuildFoldTable()
        {
            auto table = BuildCaseTable(LCMAP_LOWERCASE);
            static constexpr std::array<std::pair<wchar_t, wchar_t>, 16> k_simpleFolds{ {
                { 0x00B5, 0x03BC },  // micro sign -> mu
                { 0x017F, 0x0073 },  // long s -> s
                { 0x0345, 0x03B9 },  // combining ypogegrammeni -> iota
                { 0x03C2, 0x03C3 },  // final sigma -> sigma
                { 0x03D0, 0x03B2 },  // beta symbol
                { 0x03D1, 0x03B8 },  // theta symbol
                { 0x03D5, 0x03C6 },  // phi symbol
                { 0x03D6, 0x03C0 },  // pi symbol
                { 0x03F0, 0x03BA },  // kappa symbol
                { 0x03F1, 0x03C1 },  // rho symbol
                { 0x03F5, 0x03B5 },  // lunate epsilon symbol
                { 0x1E9B, 0x1E61 },  // long s with dot above
                { 0x1FBE, 0x03B9 },  // Greek prosgegrammeni
                { 0x2126, 0x03C9 },  // ohm sign -> omega
                { 0x212A, 0x006B },  // Kelvin sign -> k
                { 0x212B, 0x00E5 },  // angstrom sign -> a ring
            } };
            for (auto const& [from, to] : k_simpleFolds)
            {
                (*table)[static_cast<std::size_t>(from)] = to;
            }
            return table;
        }

        inline CaseTable const& FoldTable()
        {
            static auto const table = BuildFoldTable();
            return *table;
        }

        inline CaseTable const& UpperTable()
        {
            static auto const table = BuildCaseTable(LCMAP_UPPERCASE);
            return *table;
        }
    }

    // For comparisons and keys only (final sigma becomes sigma); use LowerChar for display text.
    [[nodiscard]] inline wchar_t FoldChar(wchar_t const ch) noexcept
    {
        return detail::FoldTable()[static_cast<std::size_t>(ch)];
    }

    [[nodiscard]] inline wchar_t LowerChar(wchar_t const ch) noexcept
    {
        return detail::LowerTable()[static_cast<std::size_t>(ch)];
    }

    [[nodiscard]] inline wchar_t UpperChar(wchar_t const ch) noexcept
    {
        return detail::UpperTable()[static_cast<std::size_t>(ch)];
    }

    // Folded copy, for case-insensitive keys (paths, names, ids).
    [[nodiscard]] inline std::wstring FoldCase(std::wstring_view const value)
    {
        std::wstring folded{ value };
        for (auto& ch : folded)
        {
            ch = FoldChar(ch);
        }
        return folded;
    }

    // Lowercase copy for text the user sees (the lowercase rename).
    [[nodiscard]] inline std::wstring LowerCase(std::wstring_view const value)
    {
        std::wstring lower{ value };
        for (auto& ch : lower)
        {
            ch = LowerChar(ch);
        }
        return lower;
    }

    [[nodiscard]] inline std::wstring UpperCase(std::wstring_view const value)
    {
        std::wstring upper{ value };
        for (auto& ch : upper)
        {
            ch = UpperChar(ch);
        }
        return upper;
    }

    // Letter or digit in any script (GetStringTypeW, locale independent).
    [[nodiscard]] inline bool IsLetterOrDigit(wchar_t const ch) noexcept
    {
        WORD type = 0;
        return GetStringTypeW(CT_CTYPE1, &ch, 1, &type) != FALSE &&
            (type & (C1_ALPHA | C1_DIGIT)) != 0;
    }

    // -1, 0 or 1 comparing folded code units (ordinal, no linguistic reordering; same order as
    // _wcsicmp for ASCII).
    [[nodiscard]] inline int CompareIgnoreCase(std::wstring_view const left, std::wstring_view const right) noexcept
    {
        auto const common = (std::min)(left.size(), right.size());
        for (std::size_t index = 0; index < common; ++index)
        {
            auto const l = FoldChar(left[index]);
            auto const r = FoldChar(right[index]);
            if (l != r)
            {
                return l < r ? -1 : 1;
            }
        }

        return left.size() == right.size() ? 0 : (left.size() < right.size() ? -1 : 1);
    }

    [[nodiscard]] inline bool EqualsIgnoreCase(std::wstring_view const left, std::wstring_view const right) noexcept
    {
        return left.size() == right.size() && CompareIgnoreCase(left, right) == 0;
    }

    // The letter a name is grouped under: folded (so final sigma groups with sigma) then upper-cased
    // with the same invariant tables the name sort uses; '#' for names not starting with a letter
    // or digit.
    [[nodiscard]] inline wchar_t NameGroupInitial(std::wstring_view const name) noexcept
    {
        if (name.empty())
        {
            return L'#';
        }
        wchar_t const upper = UpperChar(FoldChar(name.front()));
        return ::IsCharAlphaNumericW(upper) ? upper : L'#';
    }
}
