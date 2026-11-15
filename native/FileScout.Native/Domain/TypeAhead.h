#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    // Explorer-like type-ahead in the file list (UB-18): characters typed within kTypeAheadResetMs of
    // each other build a prefix; repeating one character cycles through the rows that start with it.
    inline constexpr std::uint64_t kTypeAheadResetMs = 1000;

    struct TypeAheadBuffer
    {
        std::wstring text;
        std::uint64_t last_key_ms{ 0 };
    };

    struct TypeAheadQuery
    {
        std::wstring prefix;
        // True for a new or cycling single-character query (move to the next match), false while the
        // prefix grows (stay on the focused row if it still matches).
        bool start_after_focused{ true };
    };

    [[nodiscard]] inline bool IsTypeAheadIdle(TypeAheadBuffer const& buffer, std::uint64_t const nowMs)
    {
        return buffer.text.empty() || nowMs < buffer.last_key_ms || nowMs - buffer.last_key_ms > kTypeAheadResetMs;
    }

    // Any printable character (letters and digits of every script, punctuation, inner spaces). A
    // leading space is left to the list, where it toggles the selection.
    [[nodiscard]] inline bool AcceptsTypeAheadCharacter(wchar_t const ch, bool const startsNewPrefix)
    {
        if (ch < 0x20 || (ch >= 0x7F && ch <= 0x9F))
        {
            return false;
        }
        return !(ch == L' ' && startsNewPrefix);
    }

    // Ordinal, case-insensitive prefix test through the OS uppercase table (Greek, Cyrillic, ...);
    // towlower/_wcsicmp fold ASCII only because the app never calls setlocale. Switch to
    // Domain/TextFold.h once D-22 lands.
    [[nodiscard]] inline bool StartsWithIgnoringCase(std::wstring_view const name, std::wstring_view const prefix)
    {
        if (prefix.empty() || name.size() < prefix.size())
        {
            return false;
        }
        auto const length = static_cast<int>(prefix.size());
        return ::CompareStringOrdinal(name.data(), length, prefix.data(), length, TRUE) == CSTR_EQUAL;
    }

    inline TypeAheadQuery AppendTypeAheadCharacter(TypeAheadBuffer& buffer, wchar_t const ch, std::uint64_t const nowMs)
    {
        if (IsTypeAheadIdle(buffer, nowMs))
        {
            buffer.text.clear();
        }
        buffer.text.push_back(ch);
        buffer.last_key_ms = nowMs;

        auto const first = std::wstring_view{ buffer.text }.substr(0, 1);
        bool const sameCharacter = std::all_of(buffer.text.begin(), buffer.text.end(), [&](wchar_t const c)
            {
                return StartsWithIgnoringCase(std::wstring_view{ &c, 1 }, first);
            });
        if (sameCharacter)
        {
            return TypeAheadQuery{ std::wstring{ first }, true };
        }
        return TypeAheadQuery{ buffer.text, false };
    }

    // Returns the first displayed row (by index, wrapping) whose name starts with the query prefix,
    // case-insensitively. `nameAt(i)` gives the display name of displayed row i.
    template <typename NameAt>
    [[nodiscard]] std::optional<std::size_t> FindTypeAheadMatch(
        std::size_t const count,
        NameAt&& nameAt,
        std::optional<std::size_t> const focused,
        TypeAheadQuery const& query)
    {
        if (count == 0 || query.prefix.empty())
        {
            return std::nullopt;
        }

        std::size_t start = 0;
        if (focused.has_value() && *focused < count)
        {
            start = query.start_after_focused ? (*focused + 1) % count : *focused;
        }

        for (std::size_t offset = 0; offset < count; ++offset)
        {
            auto const index = (start + offset) % count;
            if (StartsWithIgnoringCase(nameAt(index), query.prefix))
            {
                return index;
            }
        }
        return std::nullopt;
    }
}
