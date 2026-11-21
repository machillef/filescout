#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <windows.h>

namespace FileScoutNative::Shell
{
    // Watches a single filesystem directory (non-recursively) using overlapped ReadDirectoryChangesW
    // on a dedicated background thread. Coalesces rapid bursts via DirectoryWatchCoordinator before
    // invoking the callback on the watcher thread.
    class DirectoryWatcher
    {
    public:
        DirectoryWatcher() = default;
        ~DirectoryWatcher();
        DirectoryWatcher(DirectoryWatcher const&) = delete;
        DirectoryWatcher& operator=(DirectoryWatcher const&) = delete;

        // Starts watching `directory`. Stops any previous watch first.
        // Returns false if the directory handle cannot be opened or the stop event cannot be created.
        bool Start(
            std::filesystem::path directory,
            std::function<void()> onChanged,
            uint32_t debounceMs = 300,
            uint32_t maxWaitMs = 1500);

        // Cancels pending I/O and joins the background thread. Safe to call repeatedly.
        void Stop();

        [[nodiscard]] bool IsWatching() const noexcept;
        [[nodiscard]] std::filesystem::path WatchedPath() const;

    private:
        void WatchLoop(
            HANDLE directoryHandle,
            HANDLE stopEvent,
            std::function<void()> onChanged,
            uint32_t debounceMs,
            uint32_t maxWaitMs);

        mutable std::mutex m_stateMutex;
        std::filesystem::path m_watchedPath;
        HANDLE m_stopEvent{ nullptr };
        std::thread m_thread;
        std::atomic<bool> m_watching{ false };
    };
}
