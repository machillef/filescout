#include "pch.h"
#include <gtest/gtest.h>
#include "TraceHelper.h"

// ScopedTimer is header-only; the sink overload allows test capture without file I/O.

TEST(ScopedTimer, SinkCalledExactlyOnce)
{
    int callCount = 0;
    {
        ScopedTimer timer{ L"op", [&](std::wstring const&) { ++callCount; } };
    }
    EXPECT_EQ(callCount, 1);
}

TEST(ScopedTimer, OutputStartsWithTimerTag)
{
    std::wstring captured;
    {
        ScopedTimer timer{ L"MyLabel", [&](std::wstring const& line) { captured = line; } };
    }
    EXPECT_TRUE(captured.starts_with(L"[TIMER] MyLabel: done in "));
}

TEST(ScopedTimer, OutputEndsWithMs)
{
    std::wstring captured;
    {
        ScopedTimer timer{ L"x", [&](std::wstring const& line) { captured = line; } };
    }
    EXPECT_TRUE(captured.ends_with(L"ms"));
}

TEST(ScopedTimer, ElapsedValueIsNumeric)
{
    std::wstring captured;
    {
        ScopedTimer timer{ L"parse", [&](std::wstring const& line) { captured = line; } };
    }
    // Extract the number between "done in " and "ms".
    auto const prefix = std::wstring{ L"[TIMER] parse: done in " };
    auto const suffix = std::wstring{ L"ms" };
    ASSERT_TRUE(captured.starts_with(prefix));
    ASSERT_TRUE(captured.ends_with(suffix));
    auto const numStr = captured.substr(prefix.size(), captured.size() - prefix.size() - suffix.size());
    EXPECT_FALSE(numStr.empty());
    // Must be a valid non-negative integer.
    for (wchar_t ch : numStr)
    {
        EXPECT_TRUE(ch >= L'0' && ch <= L'9') << "Non-digit character in elapsed value: " << (int)ch;
    }
}

TEST(ScopedTimer, ElapsedIsNonNegative)
{
    long long elapsed = -1;
    {
        ScopedTimer timer{ L"t", [&](std::wstring const& line)
        {
            auto const prefix = std::wstring{ L"[TIMER] t: done in " };
            auto const suffix = std::wstring{ L"ms" };
            auto const numStr = line.substr(prefix.size(), line.size() - prefix.size() - suffix.size());
            elapsed = std::stoll(numStr);
        }};
    }
    EXPECT_GE(elapsed, 0LL);
}
