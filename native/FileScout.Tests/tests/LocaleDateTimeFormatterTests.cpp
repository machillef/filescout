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

    std::wstring DirectFormat(wchar_t const* locale, SYSTEMTIME const& local)
    {
        wchar_t date[128]{};
        wchar_t time[128]{};
        GetDateFormatEx(locale, DATE_SHORTDATE, &local, nullptr, date, 128, nullptr);
        GetTimeFormatEx(locale, TIME_NOSECONDS, &local, nullptr, time, 128);
        return std::wstring{ date } + L"  " + time;
    }
}

TEST(LocaleDateTimeFormatterTests, GreekLocaleIsDayFirst)
{
    LocaleDateTimeFormatter formatter{ L"el-GR" };
    auto const local = MakeLocal(2026, 3, 4, 13, 5);
    EXPECT_EQ(formatter.FormatLocal(local), DirectFormat(L"el-GR", local));
    if (DirectFormat(L"el-GR", local) != L"3/4/2026  1:05 PM")
    {
        EXPECT_NE(formatter.FormatLocal(local), L"3/4/2026  1:05 PM");
    }
}

TEST(LocaleDateTimeFormatterTests, UsLocaleIsMonthFirst)
{
    LocaleDateTimeFormatter formatter{ L"en-US" };
    auto const local = MakeLocal(2026, 3, 4, 13, 5);
    EXPECT_EQ(formatter.FormatLocal(local), DirectFormat(L"en-US", local));
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

TEST(LocaleDateTimeFormatterTests, CachedTimeZoneOffsetMatchesDirectConversion)
{
    LocaleDateTimeFormatter cached{ L"en-US" };
    ULARGE_INTEGER t{};
    auto const base = ToFileTime(MakeLocal(2026, 1, 1, 0, 0));
    t.LowPart = base.dwLowDateTime;
    t.HighPart = base.dwHighDateTime;
    constexpr ULONGLONG step = 10'000'000ULL * 60 * 97;

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

TEST(LocaleDateTimeFormatterTests, FormatsTwoHundredThousandDatesQuickly)
{
    LocaleDateTimeFormatter formatter;
    ULARGE_INTEGER start{};
    auto const base = ToFileTime(MakeLocal(2023, 1, 1, 0, 0));
    start.LowPart = base.dwLowDateTime;
    start.HighPart = base.dwHighDateTime;
    constexpr ULONGLONG step = 10'000'000ULL * 60 * 7;

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
    EXPECT_LT(ms, 3000);
}

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

TEST(LocaleDateTimeFormatterTests, TransitionInsideAUtcHourUsesTheExactOffset)
{
    TIME_ZONE_INFORMATION zone{};
    zone.Bias = -570;                 // UTC+09:30
    zone.StandardBias = 0;
    zone.DaylightBias = -60;
    zone.StandardDate.wMonth = 4;     // first Sunday of April, 03:00 local
    zone.StandardDate.wDayOfWeek = 0;
    zone.StandardDate.wDay = 1;
    zone.StandardDate.wHour = 3;
    zone.DaylightDate.wMonth = 10;    // first Sunday of October, 02:00 local
    zone.DaylightDate.wDayOfWeek = 0;
    zone.DaylightDate.wDay = 1;
    zone.DaylightDate.wHour = 2;

    LocaleDateTimeFormatter formatter{ L"en-US", zone };
    for (WORD const minute : { WORD{ 15 }, WORD{ 45 } })
    {
        auto const utc = MakeLocal(2026, 10, 3, 16, minute);
        SYSTEMTIME expectedLocal{};
        ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(&zone, &utc, &expectedLocal));
        EXPECT_EQ(formatter.FormatFileTime(ToFileTime(utc)), formatter.FormatLocal(expectedLocal))
            << "minute " << minute;
    }
}

TEST(LocaleDateTimeFormatterTests, SubMinuteTransitionMatchesDirectConversion)
{
    TIME_ZONE_INFORMATION zone{};
    zone.Bias = -60;                  // UTC+01:00
    zone.DaylightBias = -60;
    zone.StandardDate.wMonth = 10;
    zone.StandardDate.wDayOfWeek = 0;
    zone.StandardDate.wDay = 5;
    zone.StandardDate.wHour = 3;
    zone.DaylightDate.wMonth = 3;     // last Sunday of March, 01:59:30 local
    zone.DaylightDate.wDayOfWeek = 0;
    zone.DaylightDate.wDay = 5;
    zone.DaylightDate.wHour = 1;
    zone.DaylightDate.wMinute = 59;
    zone.DaylightDate.wSecond = 30;

    LocaleDateTimeFormatter formatter{ L"en-US", zone };
    for (WORD const second : { WORD{ 0 }, WORD{ 29 }, WORD{ 31 }, WORD{ 59 } })
    {
        auto utc = MakeLocal(2026, 3, 29, 0, 59);
        utc.wSecond = second;
        SYSTEMTIME expectedLocal{};
        ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(&zone, &utc, &expectedLocal));
        EXPECT_EQ(formatter.FormatFileTime(ToFileTime(utc)), formatter.FormatLocal(expectedLocal))
            << "second " << second;
    }
}

TEST(LocaleDateTimeFormatterTests, HourWithoutTransitionIsCachedAndReused)
{
    TIME_ZONE_INFORMATION zone{};
    zone.Bias = -120;  // UTC+02:00, no daylight time
    LocaleDateTimeFormatter formatter{ L"en-US", zone };

    auto first = MakeLocal(2026, 7, 1, 10, 5);
    auto second = MakeLocal(2026, 7, 1, 10, 50);
    auto const a = formatter.FormatFileTime(ToFileTime(first));
    auto const b = formatter.FormatFileTime(ToFileTime(second));

    EXPECT_FALSE(a.empty());
    EXPECT_FALSE(b.empty());
    EXPECT_EQ(formatter.CachedHourCount(), 1u);
}
