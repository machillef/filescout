#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace FileScoutNative::Domain
{
    inline constexpr std::uint64_t kTypeAheadResetMs = 1000;

    struct TypeAheadBuffer
    {
        std::wstring text;
        std::uint64_t last_key_ms{ 0 };
    };

    struct TypeAheadQuery
    {
        std::wstring prefix;
        bool start_after_focused{ true };
    };

    inline void ResetTypeAhead(TypeAheadBuffer& buffer) noexcept
    {
        buffer.text.clear();
        buffer.last_key_ms = 0;
    }

    [[nodiscard]] inline bool IsTypeAheadIdle(TypeAheadBuffer const& buffer, std::uint64_t const nowMs)
    {
        return buffer.text.empty() || nowMs < buffer.last_key_ms || nowMs - buffer.last_key_ms > kTypeAheadResetMs;
    }

    [[nodiscard]] inline bool AcceptsTypeAheadCharacter(wchar_t const ch, bool const startsNewPrefix)
    {
        if (ch < 0x20 || (ch >= 0x7F && ch <= 0x9F))
        {
            return false;
        }
        return !(ch == L' ' && startsNewPrefix);
    }

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
