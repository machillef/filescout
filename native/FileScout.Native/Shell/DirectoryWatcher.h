#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <windows.h>

namespace FileScoutNative::Shell
{
    class DirectoryWatcher
    {
    public:
        DirectoryWatcher() = default;
        ~DirectoryWatcher();
        DirectoryWatcher(DirectoryWatcher const&) = delete;
        DirectoryWatcher& operator=(DirectoryWatcher const&) = delete;

        bool Start(
            std::filesystem::path directory,
            std::function<void(uint64_t)> onChanged,
            uint32_t debounceMs = 300,
            uint32_t maxWaitMs = 1500,
            std::function<void(bool)> onReady = nullptr);

        // Hung network I/O cannot be cancelled, so Stop() waits a bounded time and may return while a
        // callback still runs: state captured by callbacks must outlive it (shared or weak ownership).
        void Stop();

        [[nodiscard]] bool WaitUntilArmed(uint32_t timeoutMs) const;

        [[nodiscard]] bool IsWatching() const noexcept;
        [[nodiscard]] std::filesystem::path WatchedPath() const;

    private:
        struct Session;
        static void RunSession(
            std::shared_ptr<Session> session,
            uint32_t debounceMs,
            uint32_t maxWaitMs);

        mutable std::mutex m_stateMutex;
        std::filesystem::path m_watchedPath;
        std::shared_ptr<Session> m_session;
        std::thread m_thread;
        std::atomic<bool> m_watching{ false };
    };
}
