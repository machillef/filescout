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

    // Virtual locations (This PC, Recycle Bin, archive views, empty paths) cannot be watched
    // with ReadDirectoryChangesW.
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

    // Pure debounce and self-mutation suppression coordinator for directory change notifications.
    // Coalesces bursts of filesystem events until `debounceMs` of quiet, while capping total wait
    // at `maxWaitMs` so continuous background writes cannot starve refreshes indefinitely.
    class DirectoryWatchCoordinator
    {
    public:
        void Reset() noexcept
        {
            m_pending = false;
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
            if (IsSuppressed(nowTickMs))
            {
                return;
            }

            if (!m_pending)
            {
                m_pending = true;
                m_firstPendingTickMs = nowTickMs;
            }
            m_lastEventTickMs = nowTickMs;
        }

        [[nodiscard]] bool HasPending() const noexcept
        {
            return m_pending;
        }

        [[nodiscard]] bool ShouldFlush(
            uint64_t const nowTickMs,
            uint64_t const debounceMs = kDefaultWatchDebounceMs,
            uint64_t const maxWaitMs = kDefaultWatchMaxWaitMs) const noexcept
        {
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

        // Milliseconds until the pending change should flush (0 if ready now or nothing pending).
        [[nodiscard]] uint64_t RemainingWaitMs(
            uint64_t const nowTickMs,
            uint64_t const debounceMs = kDefaultWatchDebounceMs,
            uint64_t const maxWaitMs = kDefaultWatchMaxWaitMs) const noexcept
        {
            if (!m_pending)
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
            m_firstPendingTickMs = 0;
            m_lastEventTickMs = 0;
        }

    private:
        bool m_pending{ false };
        uint64_t m_firstPendingTickMs{ 0 };
        uint64_t m_lastEventTickMs{ 0 };
        uint64_t m_suppressedUntilTickMs{ 0 };
    };
}
