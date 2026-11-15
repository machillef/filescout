#include "pch.h"
#include "Shell/LocaleDateTimeFormatter.h"

namespace FileScoutNative::Shell
{
    LocaleDateTimeFormatter::LocaleDateTimeFormatter(
        std::optional<std::wstring> localeName,
        std::optional<TIME_ZONE_INFORMATION> timeZone)
        : m_localeName(std::move(localeName)), m_timeZone(timeZone)
    {
    }

    wchar_t const* LocaleDateTimeFormatter::LocaleName() const noexcept
    {
        return m_localeName ? m_localeName->c_str() : LOCALE_NAME_USER_DEFAULT;
    }

    namespace
    {
        constexpr std::uint64_t ticksPerHour = 10'000'000ULL * 60 * 60;

        std::uint64_t ToTicks(FILETIME const& ft) noexcept
        {
            return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        }

        FILETIME ToFileTime(std::uint64_t const ticks) noexcept
        {
            return FILETIME{ static_cast<DWORD>(ticks & 0xFFFFFFFFULL), static_cast<DWORD>(ticks >> 32) };
        }
    }

    std::optional<std::int64_t> LocaleDateTimeFormatter::ConvertOffset(std::uint64_t const utcTicks) const
    {
        auto const utcFileTime = ToFileTime(utcTicks);
        SYSTEMTIME utcTime{};
        SYSTEMTIME localTime{};
        FILETIME localFileTime{};
        if (!FileTimeToSystemTime(&utcFileTime, &utcTime) ||
            !SystemTimeToTzSpecificLocalTime(m_timeZone ? &*m_timeZone : nullptr, &utcTime, &localTime) ||
            !SystemTimeToFileTime(&localTime, &localFileTime))
        {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(ToTicks(localFileTime)) - static_cast<std::int64_t>(utcTicks);
    }

    std::optional<std::int64_t> LocaleDateTimeFormatter::LocalOffsetAt(std::uint64_t const utcTicks)
    {
        constexpr std::uint64_t ticksPerMinute = 60ULL * 10'000'000ULL;
        auto const hour = utcTicks / ticksPerHour;
        if (hour == m_lastHour)
        {
            return m_lastOffset;
        }
        if (auto const it = m_hourOffsets.find(hour); it != m_hourOffsets.end())
        {
            m_lastHour = hour;
            m_lastOffset = it->second;
            return it->second;
        }

        auto const minute = utcTicks / ticksPerMinute;
        if (auto const it = m_minuteOffsets.find(minute); it != m_minuteOffsets.end())
        {
            return it->second;
        }

        auto const start = ConvertOffset(hour * ticksPerHour);
        auto const end = ConvertOffset(hour * ticksPerHour + ticksPerHour - ticksPerMinute);
        if (!start || !end)
        {
            return std::nullopt;
        }
        if (*start == *end)
        {
            m_hourOffsets.emplace(hour, *start);
            m_lastHour = hour;
            m_lastOffset = *start;
            return *start;
        }

        // A transition inside this UTC hour: resolve per minute (transitions are whole minutes).
        auto const exact = ConvertOffset(minute * ticksPerMinute);
        if (exact)
        {
            m_minuteOffsets.emplace(minute, *exact);
        }
        return exact;
    }

    std::wstring LocaleDateTimeFormatter::FormatFileTime(FILETIME const& utc)
    {
        auto const utcTicks = ToTicks(utc);
        if (utcTicks == 0)
        {
            return {};
        }

        auto const offset = LocalOffsetAt(utcTicks);
        if (!offset)
        {
            return {};
        }

        auto const localFileTime = ToFileTime(static_cast<std::uint64_t>(static_cast<std::int64_t>(utcTicks) + *offset));
        SYSTEMTIME local{};
        if (!FileTimeToSystemTime(&localFileTime, &local))
        {
            return {};
        }

        return FormatLocal(local);
    }

    std::wstring LocaleDateTimeFormatter::FormatLocal(SYSTEMTIME const& local)
    {
        auto const& date = DateText(local);
        auto const& time = TimeText(local);
        std::wstring text;
        text.reserve(date.size() + 2 + time.size());
        text += date;
        text += L"  ";
        text += time;
        return text;
    }

    std::wstring const& LocaleDateTimeFormatter::DateText(SYSTEMTIME const& local)
    {
        auto const key = (static_cast<std::uint32_t>(local.wYear) << 9) |
                         (static_cast<std::uint32_t>(local.wMonth) << 5) |
                         static_cast<std::uint32_t>(local.wDay);
        if (auto const it = m_dates.find(key); it != m_dates.end())
        {
            return it->second;
        }

        SYSTEMTIME day{};
        day.wYear = local.wYear;
        day.wMonth = local.wMonth;
        day.wDay = local.wDay;
        wchar_t buffer[80]{};
        if (GetDateFormatEx(LocaleName(), DATE_SHORTDATE, &day, nullptr, buffer,
                static_cast<int>(std::size(buffer)), nullptr) == 0)
        {
            buffer[0] = L'\0';
        }

        return m_dates.emplace(key, buffer).first->second;
    }

    std::wstring const& LocaleDateTimeFormatter::TimeText(SYSTEMTIME const& local)
    {
        auto const index = static_cast<std::size_t>(local.wHour % 24) * 60 + (local.wMinute % 60);
        auto& slot = m_times[index];
        if (!slot)
        {
            SYSTEMTIME minute{};
            minute.wYear = 2000;
            minute.wMonth = 1;
            minute.wDay = 1;
            minute.wHour = local.wHour % 24;
            minute.wMinute = local.wMinute % 60;
            wchar_t buffer[80]{};
            if (GetTimeFormatEx(LocaleName(), TIME_NOSECONDS, &minute, nullptr, buffer,
                    static_cast<int>(std::size(buffer))) == 0)
            {
                buffer[0] = L'\0';
            }

            slot = std::wstring{ buffer };
        }

        return *slot;
    }
}
