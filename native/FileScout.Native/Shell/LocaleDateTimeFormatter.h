#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace FileScoutNative::Shell
{
    // Formats file times as "<short date>  <time without seconds>" in one locale (the user's by
    // default), the same text Explorer shows. Day and minute strings are cached, so a folder of
    // 100k items costs a few hundred NLS calls instead of 400k. Not thread-safe: use one instance
    // per enumeration.
    class LocaleDateTimeFormatter
    {
    public:
        // localeName: a BCP 47 name such as L"el-GR"; nullopt means LOCALE_NAME_USER_DEFAULT.
        // timeZone: nullopt means the current time zone (tests pass an explicit one).
        explicit LocaleDateTimeFormatter(
            std::optional<std::wstring> localeName = std::nullopt,
            std::optional<TIME_ZONE_INFORMATION> timeZone = std::nullopt);

        // UTC FILETIME -> local time zone -> text. Empty for a zero or invalid time.
        [[nodiscard]] std::wstring FormatFileTime(FILETIME const& utc);
        // An already-local SYSTEMTIME -> text.
        [[nodiscard]] std::wstring FormatLocal(SYSTEMTIME const& local);

    private:
        [[nodiscard]] wchar_t const* LocaleName() const noexcept;
        std::wstring const& DateText(SYSTEMTIME const& local);
        std::wstring const& TimeText(SYSTEMTIME const& local);
        // UTC -> local offset (100 ns ticks) at utcTicks. Cached per UTC hour when the offset is the
        // same at both ends of the hour (one transition at most per hour); an hour that contains a
        // transition (e.g. a UTC+09:30 zone switching at :30) is cached per minute instead.
        [[nodiscard]] std::optional<std::int64_t> LocalOffsetAt(std::uint64_t utcTicks);
        [[nodiscard]] std::optional<std::int64_t> ConvertOffset(std::uint64_t utcTicks) const;

        std::optional<std::wstring> m_localeName;
        std::unordered_map<std::uint32_t, std::wstring> m_dates;
        std::array<std::optional<std::wstring>, 24 * 60> m_times;
        std::optional<TIME_ZONE_INFORMATION> m_timeZone;
        std::unordered_map<std::uint64_t, std::int64_t> m_hourOffsets;
        std::unordered_map<std::uint64_t, std::int64_t> m_minuteOffsets;
        std::uint64_t m_lastHour = ~std::uint64_t{ 0 };
        std::int64_t m_lastOffset = 0;
    };
}
