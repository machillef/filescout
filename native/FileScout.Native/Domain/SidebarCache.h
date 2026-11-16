#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace FileScoutNative::Domain
{
    // Sidebar data that is slow to read (drive enumeration with free space per drive) and
    // changes rarely. The sidebar is rebuilt on every navigation that records a recent folder,
    // so it reads the value once and reuses it until something invalidates it (UB-10).
    template <typename T>
    class CachedValue
    {
    public:
        template <typename Load>
        T const& Get(Load&& load)
        {
            if (!m_value)
            {
                m_value.emplace(std::forward<Load>(load)());
            }
            return *m_value;
        }

        void Invalidate() noexcept { m_value.reset(); }

    private:
        std::optional<T> m_value;
    };

    // WM_DEVICECHANGE with DBT_DEVICEARRIVAL or DBT_DEVICEREMOVECOMPLETE: a volume came or went,
    // so the cached drive list is stale.
    [[nodiscard]] constexpr bool IsDriveListChange(
        std::uint32_t const message,
        std::uintptr_t const wParam) noexcept
    {
        constexpr std::uint32_t wmDeviceChange = 0x0219;
        constexpr std::uintptr_t dbtDeviceArrival = 0x8000;
        constexpr std::uintptr_t dbtDeviceRemoveComplete = 0x8004;
        return message == wmDeviceChange &&
            (wParam == dbtDeviceArrival || wParam == dbtDeviceRemoveComplete);
    }
}
