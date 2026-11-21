#include "pch.h"
#include <gtest/gtest.h>
#include "Domain/TypeAhead.h"

#include <string>
#include <vector>

using namespace FileScoutNative::Domain;

namespace
{
    std::optional<std::size_t> Type(
        TypeAheadBuffer& buffer,
        std::vector<std::wstring> const& names,
        std::optional<std::size_t> focused,
        wchar_t const ch,
        std::uint64_t const nowMs)
    {
        auto const query = AppendTypeAheadCharacter(buffer, ch, nowMs);
        return FindTypeAheadMatch(
            names.size(), [&](std::size_t i) -> std::wstring_view { return names[i]; }, focused, query);
    }
}

TEST(TypeAhead, CharactersWithinTheTimeout_BuildAPrefix)
{
    std::vector<std::wstring> const names{ L"alpha", L"beta", L"bravo", L"charlie" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'b', 1000), 1u);
    EXPECT_EQ(Type(buffer, names, 1, L'r', 1300), 2u);
    EXPECT_EQ(buffer.text, L"br");
}

TEST(TypeAhead, PauseLongerThanTheTimeout_StartsANewPrefix)
{
    std::vector<std::wstring> const names{ L"alpha", L"beta", L"bravo", L"rome" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'b', 1000), 1u);
    EXPECT_EQ(Type(buffer, names, 1, L'r', 1000 + kTypeAheadResetMs + 1), 3u);
    EXPECT_EQ(buffer.text, L"r");
}

TEST(TypeAhead, RepeatedSameLetter_CyclesThroughMatches)
{
    std::vector<std::wstring> const names{ L"b1", L"a", L"b2", L"b3" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'b', 0), 0u);
    EXPECT_EQ(Type(buffer, names, 0, L'b', 100), 2u);
    EXPECT_EQ(Type(buffer, names, 2, L'b', 200), 3u);
    EXPECT_EQ(Type(buffer, names, 3, L'b', 300), 0u);
}

TEST(TypeAhead, SingleLetter_StartsAfterTheFocusedRow)
{
    std::vector<std::wstring> const names{ L"c1", L"c2", L"c3" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, 1, L'c', 0), 2u);
}

TEST(TypeAhead, ExtendingThePrefix_KeepsTheFocusedRowWhenItStillMatches)
{
    std::vector<std::wstring> const names{ L"foo", L"food" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'f', 0), 0u);
    EXPECT_EQ(Type(buffer, names, 0, L'o', 100), 0u);
}

TEST(TypeAhead, CaseInsensitive_GreekAndDigits)
{
    std::vector<std::wstring> const names{ L"2024 report", L"\u0388\u03B3\u03B3\u03C1\u03B1\u03C6\u03B1", L"\u03A6\u03C9\u03C4\u03BF\u03B3\u03C1\u03B1\u03C6\u03AF\u03B5\u03C2" };
    TypeAheadBuffer buffer;
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'\u03C6', 0), 2u);
    EXPECT_EQ(Type(buffer, names, 2, L'\u03C9', 100), 2u);
    TypeAheadBuffer digits;
    EXPECT_EQ(Type(digits, names, 2, L'2', 0), 0u);
}

TEST(TypeAhead, NoMatch_ReturnsNothing)
{
    std::vector<std::wstring> const names{ L"alpha" };
    TypeAheadBuffer buffer;
    EXPECT_FALSE(Type(buffer, names, std::nullopt, L'z', 0).has_value());
    EXPECT_FALSE(FindTypeAheadMatch(0, [](std::size_t) { return std::wstring_view{}; }, std::nullopt, TypeAheadQuery{ L"a", true }).has_value());
}

TEST(TypeAhead, AcceptsPrintableCharactersButNotControlOrLeadingSpace)
{
    EXPECT_TRUE(AcceptsTypeAheadCharacter(L'a', true));
    EXPECT_TRUE(AcceptsTypeAheadCharacter(L'\u03A9', true));
    EXPECT_TRUE(AcceptsTypeAheadCharacter(L'7', true));
    EXPECT_TRUE(AcceptsTypeAheadCharacter(L' ', false));
    EXPECT_FALSE(AcceptsTypeAheadCharacter(L' ', true));
    EXPECT_FALSE(AcceptsTypeAheadCharacter(L'\r', true));
    EXPECT_FALSE(AcceptsTypeAheadCharacter(L'\b', false));
}

TEST(TypeAhead, ResetStartsANewPrefixWithinTheTimeout)
{
    std::vector<std::wstring> const names{ L"alpha", L"bar" };
    TypeAheadBuffer buffer;
    (void)AppendTypeAheadCharacter(buffer, L'f', 1000);
    ResetTypeAhead(buffer);
    EXPECT_EQ(Type(buffer, names, std::nullopt, L'b', 1200), 1u);
    EXPECT_EQ(buffer.text, L"b");
}
