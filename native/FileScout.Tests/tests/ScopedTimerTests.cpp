#include "pch.h"
#include <gtest/gtest.h>
#include "TraceHelper.h"

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
    auto const prefix = std::wstring{ L"[TIMER] parse: done in " };
    auto const suffix = std::wstring{ L"ms" };
    ASSERT_TRUE(captured.starts_with(prefix));
    ASSERT_TRUE(captured.ends_with(suffix));
    auto const numStr = captured.substr(prefix.size(), captured.size() - prefix.size() - suffix.size());
    EXPECT_FALSE(numStr.empty());
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

TEST(TraceLogFile, AppendsLinesAsUtf8)
{
    auto const path = std::filesystem::temp_directory_path() / L"FileScoutTests_utf8_log.txt";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    AppendUtf8LogLine(path, L"\u03c3\u03c6\u03ac\u03bb\u03bc\u03b1 \u9519\u8bef 0x80004005");
    AppendUtf8LogLine(path, L"second");

    std::ifstream stream{ path, std::ios::binary };
    std::string const bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    EXPECT_EQ(bytes,
        "\xcf\x83\xcf\x86\xce\xac\xce\xbb\xce\xbc\xce\xb1 \xe9\x94\x99\xe8\xaf\xaf 0x80004005\n"
        "second\n");
    stream.close();
    std::filesystem::remove(path, ec);
}

TEST(TraceLogFile, UnwritablePathDoesNotThrow)
{
    EXPECT_NO_THROW(AppendUtf8LogLine(L"Z:\\no\\such\\dir\\log.txt", L"line"));
}

TEST(TraceLogFile, TraceLogAndAppendCannotThrow)
{
    static_assert(noexcept(TraceLog(std::wstring{})));
    static_assert(noexcept(AppendUtf8LogLine(std::filesystem::path{}, std::wstring_view{})));
    EXPECT_NO_THROW(TraceLog(L"TraceLogAndAppendCannotThrow"));
}
