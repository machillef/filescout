#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace FileScoutNative::Shell
{
    // Its caches are not thread-safe: use one instance per worker thread.
    class LocaleDateTimeFormatter
    {
    public:
        explicit LocaleDateTimeFormatter(
            std::optional<std::wstring> localeName = std::nullopt,
            std::optional<TIME_ZONE_INFORMATION> timeZone = std::nullopt);

        [[nodiscard]] std::wstring FormatFileTime(FILETIME const& utc);
        [[nodiscard]] std::wstring FormatLocal(SYSTEMTIME const& local);

        [[nodiscard]] std::size_t CachedHourCount() const noexcept { return m_hourOffsets.size(); }

    private:
        [[nodiscard]] wchar_t const* LocaleName() const noexcept;
        std::wstring const& DateText(SYSTEMTIME const& local);
        std::wstring const& TimeText(SYSTEMTIME const& local);
        [[nodiscard]] std::optional<std::int64_t> LocalOffsetAt(std::uint64_t utcTicks);
        [[nodiscard]] std::optional<std::int64_t> ConvertOffset(std::uint64_t utcTicks) const;

        std::optional<std::wstring> m_localeName;
        std::unordered_map<std::uint32_t, std::wstring> m_dates;
        std::array<std::optional<std::wstring>, 24 * 60> m_times;
        std::optional<TIME_ZONE_INFORMATION> m_timeZone;
        std::unordered_map<std::uint64_t, std::int64_t> m_hourOffsets;
        std::uint64_t m_lastHour = ~std::uint64_t{ 0 };
        std::int64_t m_lastOffset = 0;
    };
}
