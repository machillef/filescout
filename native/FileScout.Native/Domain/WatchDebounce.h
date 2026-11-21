#pragma once

#include "NavigationHistory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace FileScoutNative::Domain
{
    inline constexpr uint64_t kDefaultWatchDebounceMs = 300;
    inline constexpr uint64_t kDefaultWatchMaxWaitMs = 1500;
    inline constexpr uint64_t kDefaultWatchSelfSuppressMs = 600;

    [[nodiscard]] constexpr bool ShouldRestoreSelectionInLoadedPane(
        int32_t const loadedPaneIndex,
        int32_t const activePaneIndex,
        bool const preserveInactivePaneSelection) noexcept
    {
        return loadedPaneIndex == activePaneIndex || preserveInactivePaneSelection;
    }

    [[nodiscard]] inline bool IsWatchEventCoveredBySnapshot(
        uint64_t const eventTickMs,
        uint64_t const snapshotStartTickMs) noexcept
    {
        return eventTickMs > 0 && snapshotStartTickMs > 0 && eventTickMs < snapshotStartTickMs;
    }

    [[nodiscard]] inline bool ShouldWatchLocation(
        std::filesystem::path const& path,
        bool const isDrivesView,
        bool const isArchiveView,
        bool const isRecycleBinView)
    {
        if (isDrivesView || isArchiveView || isRecycleBinView || path.empty())
        {
            return false;
        }
        return !IsRecycleBinLocation(path) && !IsDrivesViewLocation(path);
    }

    class DirectoryWatchCoordinator
    {
    public:
        void Reset() noexcept
        {
            m_pending = false;
            m_deferredDuringSuppression = false;
            m_firstPendingTickMs = 0;
            m_lastEventTickMs = 0;
            m_suppressedUntilTickMs = 0;
        }

        void SuppressUntil(uint64_t const untilTickMs) noexcept
        {
            m_suppressedUntilTickMs = (std::max)(m_suppressedUntilTickMs, untilTickMs);
        }

        void SuppressFor(
            uint64_t const nowTickMs,
            uint64_t const durationMs = kDefaultWatchSelfSuppressMs) noexcept
        {
            SuppressUntil(nowTickMs + durationMs);
        }

        [[nodiscard]] bool IsSuppressed(uint64_t const nowTickMs) const noexcept
        {
            return m_suppressedUntilTickMs > 0 && nowTickMs < m_suppressedUntilTickMs;
        }

        void NoteChange(uint64_t const nowTickMs) noexcept
        {
            m_lastEventTickMs = nowTickMs;
            if (IsSuppressed(nowTickMs))
            {
                m_deferredDuringSuppression = true;
                return;
            }

            if (!m_pending)
            {
                m_pending = true;
                m_firstPendingTickMs = nowTickMs;
            }
        }

        [[nodiscard]] uint64_t LastEventTickMs() const noexcept
        {
            return m_lastEventTickMs;
        }

        [[nodiscard]] bool HasPending() const noexcept
        {
            return m_pending || m_deferredDuringSuppression;
        }

        [[nodiscard]] bool HasDeferredDuringSuppression() const noexcept
        {
            return m_deferredDuringSuppression;
        }

        [[nodiscard]] bool ShouldFlush(
            uint64_t const nowTickMs,
            uint64_t const debounceMs = kDefaultWatchDebounceMs,
            uint64_t const maxWaitMs = kDefaultWatchMaxWaitMs) const noexcept
        {
            if (IsSuppressed(nowTickMs))
            {
                return false;
            }

            if (m_deferredDuringSuppression)
            {
                return true;
            }

            if (!m_pending)
            {
                return false;
            }

            uint64_t const sinceLast = nowTickMs >= m_lastEventTickMs
                ? (nowTickMs - m_lastEventTickMs)
                : 0;
            if (sinceLast >= debounceMs)
            {
                return true;
            }

            if (maxWaitMs > 0)
            {
                uint64_t const sinceFirst = nowTickMs >= m_firstPendingTickMs
                    ? (nowTickMs - m_firstPendingTickMs)
                    : 0;
                if (sinceFirst >= maxWaitMs)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] uint64_t RemainingWaitMs(
            uint64_t const nowTickMs,
            uint64_t const debounceMs = kDefaultWatchDebounceMs,
            uint64_t const maxWaitMs = kDefaultWatchMaxWaitMs) const noexcept
        {
            if (!HasPending())
            {
                return 0;
            }

            if (IsSuppressed(nowTickMs))
            {
                return m_suppressedUntilTickMs - nowTickMs;
            }

            if (m_deferredDuringSuppression)
            {
                return 0;
            }

            uint64_t const sinceLast = nowTickMs >= m_lastEventTickMs
                ? (nowTickMs - m_lastEventTickMs)
                : 0;
            uint64_t const debounceRemaining = sinceLast >= debounceMs ? 0 : (debounceMs - sinceLast);

            if (maxWaitMs == 0)
            {
                return debounceRemaining;
            }

            uint64_t const sinceFirst = nowTickMs >= m_firstPendingTickMs
                ? (nowTickMs - m_firstPendingTickMs)
                : 0;
            uint64_t const maxRemaining = sinceFirst >= maxWaitMs ? 0 : (maxWaitMs - sinceFirst);

            return (std::min)(debounceRemaining, maxRemaining);
        }

        void MarkFlushed() noexcept
        {
            m_pending = false;
            m_deferredDuringSuppression = false;
            m_firstPendingTickMs = 0;
            m_lastEventTickMs = 0;
        }

    private:
        bool m_pending{ false };
        bool m_deferredDuringSuppression{ false };
        uint64_t m_firstPendingTickMs{ 0 };
        uint64_t m_lastEventTickMs{ 0 };
        uint64_t m_suppressedUntilTickMs{ 0 };
    };
}
