#include "pch.h"
#include <gtest/gtest.h>
#include "Shell/LocaleDateTimeFormatter.h"
#include "Shell/ShellInteropService.h"

using FileScoutNative::Shell::LocaleDateTimeFormatter;

namespace
{
    SYSTEMTIME MakeLocal(WORD year, WORD month, WORD day, WORD hour, WORD minute)
    {
        SYSTEMTIME st{};
        st.wYear = year;
        st.wMonth = month;
        st.wDay = day;
        st.wHour = hour;
        st.wMinute = minute;
        return st;
    }

    FILETIME ToFileTime(SYSTEMTIME const& st)
    {
        FILETIME ft{};
        SystemTimeToFileTime(&st, &ft);
        return ft;
    }
}

// S-08: the fast enumeration path printed a fixed en-US "M/D/YYYY H:MM AM/PM" whatever the locale.
TEST(LocaleDateTimeFormatterTests, GreekLocaleIsDayFirst)
{
    LocaleDateTimeFormatter formatter{ L"el-GR" };
    auto const text = formatter.FormatLocal(MakeLocal(2026, 3, 4, 13, 5));
    EXPECT_TRUE(text.starts_with(L"4/3/2026")) << winrt::to_string(text);
    EXPECT_EQ(text.find(L"PM"), std::wstring::npos) << winrt::to_string(text);
}

TEST(LocaleDateTimeFormatterTests, UsLocaleIsMonthFirst)
{
    LocaleDateTimeFormatter formatter{ L"en-US" };
    EXPECT_EQ(formatter.FormatLocal(MakeLocal(2026, 3, 4, 13, 5)), L"3/4/2026  1:05 PM");
}

TEST(LocaleDateTimeFormatterTests, CachedResultsMatchUncached)
{
    LocaleDateTimeFormatter cached{ L"el-GR" };
    for (WORD minute = 0; minute < 60; minute += 7)
    {
        auto const local = MakeLocal(2025, 12, 31, 23, minute);
        auto const first = cached.FormatLocal(local);
        EXPECT_EQ(cached.FormatLocal(local), first);
        EXPECT_EQ(LocaleDateTimeFormatter{ L"el-GR" }.FormatLocal(local), first);
    }
}

TEST(LocaleDateTimeFormatterTests, ZeroFileTimeIsEmpty)
{
    LocaleDateTimeFormatter formatter{ L"en-US" };
    EXPECT_TRUE(formatter.FormatFileTime(FILETIME{}).empty());
}

TEST(LocaleDateTimeFormatterTests, FileTimeUsesUserTimeZone)
{
    auto const utc = MakeLocal(2026, 7, 1, 10, 30);
    SYSTEMTIME local{};
    ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));

    LocaleDateTimeFormatter formatter{ L"en-US" };
    EXPECT_EQ(formatter.FormatFileTime(ToFileTime(utc)), formatter.FormatLocal(local));
}

// The per-hour offset cache must agree with a direct conversion all year round (DST included).
TEST(LocaleDateTimeFormatterTests, CachedTimeZoneOffsetMatchesDirectConversion)
{
    LocaleDateTimeFormatter cached{ L"en-US" };
    ULARGE_INTEGER t{};
    auto const base = ToFileTime(MakeLocal(2026, 1, 1, 0, 0));
    t.LowPart = base.dwLowDateTime;
    t.HighPart = base.dwHighDateTime;
    constexpr ULONGLONG step = 10'000'000ULL * 60 * 97; // 97 minutes: walks through every hour and minute

    for (int i = 0; i < 6000; ++i)
    {
        FILETIME const ft{ t.LowPart, t.HighPart };
        SYSTEMTIME utc{}, local{};
        ASSERT_TRUE(FileTimeToSystemTime(&ft, &utc));
        ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
        ASSERT_EQ(cached.FormatFileTime(ft), LocaleDateTimeFormatter{ L"en-US" }.FormatLocal(local)) << i;
        t.QuadPart += step;
    }
}

// 100k items, two dates each, spread over three years: a folder listing must not wait on dates.
TEST(LocaleDateTimeFormatterTests, FormatsTwoHundredThousandDatesQuickly)
{
    LocaleDateTimeFormatter formatter;
    ULARGE_INTEGER start{};
    auto const base = ToFileTime(MakeLocal(2023, 1, 1, 0, 0));
    start.LowPart = base.dwLowDateTime;
    start.HighPart = base.dwHighDateTime;
    constexpr ULONGLONG step = 10'000'000ULL * 60 * 7; // 7 minutes in 100 ns ticks

    std::size_t totalLength = 0;
    auto const t0 = std::chrono::steady_clock::now();
    for (ULONGLONG i = 0; i < 200'000; ++i)
    {
        ULARGE_INTEGER value{};
        value.QuadPart = start.QuadPart + i * step;
        FILETIME ft{ value.LowPart, value.HighPart };
        totalLength += formatter.FormatFileTime(ft).size();
    }
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    std::cout << "[ perf     ] 200k FormatFileTime: " << ms << " ms\n";
    EXPECT_GT(totalLength, 0u);
    // Release on win1: 46 ms for 200k. Debug runs 10-15x slower and varies with VM load, so this
    // bound only catches a lost cache (one NLS + time-zone call per date), not small regressions.
    EXPECT_LT(ms, 3000);
}

// The fast enumeration path (FindFirstFileW) must use the same formatter as everything else.
TEST(LocaleDateTimeFormatterTests, EnumerateDirectoryUsesUserLocaleFormatter)
{
    auto const dir = std::filesystem::temp_directory_path() / L"FileScoutTests_LocaleDates";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    std::ofstream{ dir / L"a.txt" } << "x";

    WIN32_FILE_ATTRIBUTE_DATA data{};
    ASSERT_TRUE(GetFileAttributesExW((dir / L"a.txt").c_str(), GetFileExInfoStandard, &data));

    FileScoutNative::Shell::ShellInteropService service;
    auto const items = service.EnumerateDirectory(dir);
    std::filesystem::remove_all(dir, ec);

    ASSERT_EQ(items.size(), 1u);
    LocaleDateTimeFormatter formatter;
    EXPECT_EQ(items[0].date_modified_str, formatter.FormatFileTime(data.ftLastWriteTime));
    EXPECT_EQ(items[0].date_created_str, formatter.FormatFileTime(data.ftCreationTime));
}
