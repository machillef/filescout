#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace FileScoutNative::Domain
{
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
